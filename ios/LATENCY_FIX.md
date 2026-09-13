# iOS 打击音效延迟修复日志

日期：2026-09-12。

## 现象与结论

用户使用设备自带扬声器游玩：画面和音乐已校准，但敲击后鼓声仍明显滞后，体感约 50ms，需要提前打击。缩短 iOS 音频排队并请求较短的系统音频周期后，用户在 iPad 上反馈“完全没感觉到延迟了”。

这次证据支持音频缓冲是主要来源。50ms 是用户体感估计，并非外部仪器测量；两项音频设置同时改变，因此没有单独量化各自贡献。没有修改判定窗口、输入补偿、歌曲偏移或歌曲同步时钟。

## 代码中发现的问题

项目请求 128 帧缓冲，但 SDL 3.4.4 的 CoreAudio / AudioQueue 后端会额外排入多块缓冲。原算法位于 SDL 源码 `src/audio/coreaudio/SDL_coreaudio.m` 的 `PrepareAudioQueue`：

```text
单块时长 = sample_frames / sample_rate × 1000
默认块数 = 3
单块时长 < 15ms 时：块数 = ceil(15 / 单块时长) × 2
```

实机报告 48kHz、128 帧，即每块约 2.667ms，原算法分配 12 块，总容量为 32ms。因此，仅减小 `config.toml` 中的 `buffer_size`，不能按相同比例减小总队列。

另外，鼓声在 `Player::handle_input()` 消费按键时触发；游戏只缓存按键，不使用触摸事件原始时间做判定。`AudioEngine::get_sound_time_played()` 返回混音进度，也不是扬声器实际播放位置。这两项仍值得在问题复发时检查，但本次未对它们做补偿或重构。

## 保留的正式修复

- `cmake/ios_audio.cmake`：通过 iOS 专属编译定义调整 SDL 播放队列，默认 `IOS_AUDIO_QUEUE_MIN_MS=4`。对于这台 48kHz / 128 帧设备，队列变为 4 块，容量约 10.67ms，比原配置少约 21.33ms。队列容量不等于实际端到端延迟。
- `src/platform/ios.mm` 的 `ios_request_audio_buffer()`：请求 `AVAudioSession` 的首选 I/O 周期为 5ms；系统可以选择其他实际值。
- `src/libs/audio.cpp`：在打开 SDL 音频设备前提出上述请求。
- 补丁仅改变 iOS 播放路径，保留其他平台和录音路径的原有行为。配置脚本可更新已有 SDL 补丁，并清除旧开发版本残留的统计代码。

`IOS_AUDIO_QUEUE_MIN_MS=15` 可恢复原 SDL 队列算法以作对照，但不会撤销 5ms 系统周期请求。若要完整对照旧版，需要同时暂时移除该请求。缓冲更短可能降低抗卡顿能力，仍需检查长时间游玩和不同音频路由。

## 实机验证记录

设备：iPad Pro 11 英寸（第 3 代），内置扬声器。真机 Release 构建、签名和安装通过。

| 观测项 | 结果 |
| --- | --- |
| 实际采样率 | 48,000Hz（游戏请求 44,100Hz） |
| SDL 设备缓冲 | 128 帧 |
| 系统实际 I/O 周期 | 5.33ms |
| 系统报告的输出延迟 | 5.50ms |
| 160 次手动敲击，每 32 次一组：事件送达平均 | 各组约 1.3–1.6ms |
| 事件送达后等待游戏消费的平均 | 各组约 0.3ms |
| 各组事件至游戏消费的最大耗时 | 约 2.5–3.6ms |
| 用户反馈 | 已无可感知延迟 |

软件时间戳统计不包含物理触摸采样、显示扫描或实际声音传播，也不是输入到声音的总延迟测量。系统报告的输出延迟也不能直接当作完整的应用端到端延迟。iPhone、其他输出设备和长时间无爆音表现仍需单独验证。

## 问题复发时如何重建临时调试代码

当前代码已删除统计存储、日志函数、调用点和调试开关。不要通过修改判定时间来代替测量。

1. 固定对照条件：同一设备、歌曲、输出设备、音量和校准设置。记录是否使用蓝牙，以及帧率、系统版本、Xcode / SDL 版本。
2. 在 `AudioEngine::init_sdl3_device()` 打开设备后，记录 `SDL_GetAudioDeviceFormat()` 返回的采样率和帧数。在 iOS 平台函数中读取 `AVAudioSession.sampleRate`、`IOBufferDuration`、`outputLatency` 和 `currentRoute.outputs`；前台恢复后再记录一次。不要用首选值冒充实际值。
3. 在 SDL `PrepareAudioQueue` 计算块数后，临时打印 `numAudioBuffers`、单块时长和两者乘积。不要在每个音频回调内同步写日志。
4. 在 `src/libs/input.cpp` 的 `SDL_EVENT_FINGER_DOWN` 路径记录事件时间和收到时间。SDL 时间戳是纳秒，来自 `SDL_GetTicksNS()` 时钟；游戏时钟扣除了后台暂停时长，不能直接将两者相减。可先求事件年龄，再映射到游戏时钟：

   ```text
   now_ns = SDL_GetTicksNS()
   received_ms = get_current_ms()
   age_ms = timestamp 有效且 <= now_ns ? (now_ns - timestamp) / 1e6 : 0
   event_ms = received_ms - age_ms
   ```

5. 使用“虚拟按键 → FIFO 时间记录”的队列，保存 `{event_ms, received_ms}`。同键连打必须保留多次记录，不能只用一个全局最后时间。入队和按键缓冲共用锁；`check_key_pressed()` 成功消费对应按键时取出同一条时间记录，非触摸输入不得沿用上一条记录。`clear_input_buffers()` 和后台清理也必须清空统计队列。
6. 在 `src/objects/game/player.cpp` 的 `Player::handle_input()` 成功消费敲击后，记录处理时刻 `handled_ms`。分别统计 `received_ms - event_ms`（送达）、`handled_ms - received_ms`（游戏等待）、`handled_ms - event_ms`（总处理）。每 32 次汇总均值和最大值，随后清零；不要每次触摸同步打印。
7. 时间记录只用于诊断，不传给 `check_note()`、不修改 `input_log` 或音效调度。若需要真正修复时间戳判定，必须连同提前移除超时音符、连打尾部和暂停状态一起验证，不能仅将 `ms_from_start` 减去一个常数。
8. 用 Xcode 控制台或设备应用容器里的 `Documents/latest.log` 读取结果。确认用户反馈后删除临时代码，将新测量及改动更新到本日志。

如果输入处理仍为数毫秒，但声音滞后，优先检查队列、音频路由和混音进度；若游戏等待明显增加，再检查帧耗时和事件消费顺序。需要绝对端到端数值时，应另做外部录音或高速摄影测量。
