# iOS port

The iOS target shares the C++ game, SDL3 renderer/input/audio, OpenGL ES 3 shaders,
FFmpeg decoding, and touch drum controls with Android. It builds a landscape app
for iPhone and iPad running iOS 16.3 or later. This is a source port; physical-device
latency and distribution signing still need validation.

## Build on a Mac

Install Xcode (including the iOS SDK) and CMake 3.24 or newer. Select the full Xcode
installation with `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`
if the command-line tools are selected instead. Python 3, Git, and curl are also
required. The first build downloads and compiles dependencies.

Populate the skin submodules before building on a fresh checkout:

```sh
git submodule update --init --recursive
```

Do not run that command over skin changes you want to keep. If your assets live
elsewhere, pass `-DYATAIDON_SKINS_DIR=/absolute/path/to/Skins` to the build script.
Use the complete `Skins` directory from the matching submodule revisions, including
`PyTaikoGreen` and `YataiDONNijiiro`: texture IDs are generated across skins. All
skins placed there are included. `-DIOS_SONGS_DIR=/absolute/path/to/Songs` changes the bundled
song library. Large skin videos increase both app size and first-launch copy time.

### Simulator

```sh
./build_ios.sh simulator
open build-ios-simulator/YataiDON.xcodeproj
```

Select the YataiDON scheme and an installed iPhone or iPad Simulator, then Run.
The script uses your Mac's architecture; set `IOS_ARCH=x86_64` on Intel if needed.
No Apple development team is required for the unsigned Simulator build.

### iPhone or iPad

```sh
IOS_DEVELOPMENT_TEAM=YOUR_TEAM_ID \
IOS_BUNDLE_IDENTIFIER=com.yourname.yataidon \
./build_ios.sh device
open build-ios-device/YataiDON.xcodeproj
```

Select your connected device and the YataiDON scheme. Check Signing & Capabilities
and select your Apple team, then Run. Enable Developer Mode on the device when
Xcode requests it. Without a team, the script builds an unsigned `.app` for compile
checks; it cannot be installed on a physical device until it is signed.
Signing is disabled only for that command-line build, not in the generated Xcode
project. Select your team and a unique Bundle Identifier in Xcode before running.
Pass these same values to the script on subsequent builds so regeneration keeps
your signing configuration. If an older generated project reports “No code
signature found”, regenerate it and rebuild with signing enabled.

The build does not publish to TestFlight or the App Store. Distribution requires
appropriate signing, artwork, and rights to the assets you include.

## Songs, skins, and saves

On first launch, bundled resources are copied into the app's Documents directory.
Open **Files → On My iPhone/iPad → YataiDON**, or use Finder's device File Sharing.
Add song folders under `Songs`, and skins under `Skins`. TJA files and their audio
files must stay together. Restart the app to rescan new content. The shared folder
also contains `config.toml`, score databases, caches, and `latest.log`.

Existing files, including settings and scores, are preserved on app upgrades.
Missing bundled files are restored at launch; bundled shaders are refreshed to
match the executable. Uninstalling the app deletes its data container, so copy
out any songs and scores you want to keep first.

Touch input is enabled in the bundled default config. Tap inside the drum for Don
and outside for Kat; left and right halves retain the Android mappings. The top
**Back** control replaces Android's system Back button. **Pause** toggles pause in
single-player and two-player gameplay. Song search and text settings use the iOS
keyboard. SDL also handles supported game controllers.

The UIKit animation callback drives rendering. On backgrounding, the game clock
and SDL audio stream pause; foregrounding resumes them together. Frame rate follows
the display callback rather than the desktop FPS limiter.

## Dependency builds and options

`build_ios.sh` builds FFmpeg automatically when its static libraries are absent.
Device and Simulator libraries are separate even when both use ARM64:

```sh
IOS_SDK=iphoneos tools/build_ffmpeg_ios.sh
IOS_SDK=iphonesimulator tools/build_ffmpeg_ios.sh
cmake --preset ios-simulator
cmake --build --preset ios-simulator
```

The presets assume ARM64. Override `CMAKE_OSX_ARCHITECTURES` and
`IOS_FFMPEG_PREFIX` together for Intel Simulators. The scripts accept
`IOS_DEPLOYMENT_TARGET` (default `16.3`), `IOS_FFMPEG_PREFIX`, `JOBS`, `CMAKE`, and
`CONFIGURATION` (default `Release`). Use a separate build directory when changing
SDK or architecture. `CONFIGURATION=Debug ./build_ios.sh simulator` builds symbols
without the desktop sanitizer flags.
After an Xcode upgrade, configuration automatically clears cached dependency paths
inside removed SDK directories so they are discovered in the current SDK.

## Latency trial on physical devices

The iOS SDL AudioQueue build uses `IOS_AUDIO_QUEUE_MIN_MS=4` by default. For a
48 kHz device with 128-frame buffers, this reduces the queue from 12 buffers
(32 ms capacity) to 4 (10.67 ms). This is queue capacity, not a measured
touch-to-speaker delay. The app also requests a 5 ms hardware I/O cycle; iOS may
choose a different duration. Other platforms retain SDL's original behavior.

Startup and foreground logs prefixed `iOS latency:` report the actual audio
session rate, I/O cycle, output latency and route. SDL prints the AudioQueue
capacity to the Xcode console. During manual gameplay, each group of 32 touches
logs average OS event delivery, average wait until gameplay consumes the event,
and maximum total processing delay. These timings exclude physical touch sensing,
audio output and display scanout. Event timestamps are currently diagnostic only;
this trial does not change judgment offsets or the chart's audio clock.

Compare the same chart, speaker output and calibration settings before/after.
Check both timing feel and crackles/dropouts, including after background/resume.
To compare with SDL's original queue sizing, reconfigure with
`-DIOS_AUDIO_QUEUE_MIN_MS=15` (the hardware I/O preference remains 5 ms).

Physical-device check (2026-09-12): on an iPad Pro 11-inch (3rd generation)
using its speaker, the session reported 48 kHz, a 5.33 ms I/O cycle and 5.50 ms
output latency. Across 160 manual hits, grouped average event delivery was
1.3–1.6 ms and gameplay processing wait about 0.3 ms. The tester reported no
perceptible delay with this build. This is a subjective gameplay result plus
software timing, not an external measurement of total input-to-sound latency;
iPhone, other routes and long-session dropout behavior still need testing.

## Online services

iOS uses the existing Hiroba client for registration, profile and score sync,
remote song selection, and online/version indicators. CPR and its pinned curl
are built separately for Device and Simulator. HTTPS uses Apple's Secure Transport
and system trust store, with certificate verification enabled; no Android CA
bundle or host macOS OpenSSL installation is needed.

Configure the same backend values used by the other platforms in an untracked
repository-root `.env` file:

```dotenv
NETWORK_URL=https://your-test-backend.example
NETWORK_AUTH_KEY=your-backend-key
```

Reconfigure after changing these values. CMake caches them, so clear just those
two entries to reload `.env` (preserve your signing team and Bundle ID):

```sh
IOS_DEVELOPMENT_TEAM=YOUR_TEAM_ID \
IOS_BUNDLE_IDENTIFIER=com.yourname.yataidon \
./build_ios.sh device -UNETWORK_URL -UNETWORK_AUTH_KEY
```

Without both values, the offline implementation is built. In the installed app's
Documents `config.toml`, set `[network] online_play = true` to enable requests;
set `sync_scores = true` if you also want startup score downloads, then restart.
Leave `access_code` empty for first-time registration, or use your own existing
code. Existing configurations are preserved on upgrade, so rebuilding alone does
not turn these switches on. Local gameplay/saves remain available offline.

The [network integration checks](../tests/network/README.md) exercise the actual
client against an isolated fixture and test HTTPS trust on the Simulator. Real
server credentials and physical-device sync still need end-to-end validation.
Optional Fumen support still requires the same seeds as other platforms.

Platform references: [SDL's iOS integration](https://wiki.libsdl.org/SDL3/README-ios)
and [CMake Apple cross-compilation](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling-for-ios-tvos-visionos-or-watchos).

## Validation

The ARM64 Release build was compiled with Xcode and exercised on an iPhone 17 Pro
Simulator running iOS 26.5. Checks covered first-run asset setup, SDL/CoreAudio
initialization, touch navigation through player entry and song selection, the
bundled TRIPLE HELIX chart, 3D rendering, the Pause control, and returning from the
background. The clock's suspend/resume behavior and the local SQLite database's
integrity were also checked. Physical-device performance, signing, and all alternate
skins have not been validated.
