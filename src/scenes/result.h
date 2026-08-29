#pragma once

#include "../libs/screen.h"
#include "../libs/text.h"
#include "../objects/result/background.h"
#include "../objects/result/player.h"
#include "../objects/result/fade_in.h"
#include "../objects/game/song_info.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"

class ResultScreen : public Screen {
protected:
    std::unique_ptr<OutlinedText> song_info;
    FadeAnimation* fade_out;
    AllNetIcon allnet_indicator;
    CoinOverlay coin_overlay;

    std::optional<ResultBackground> background;
    std::optional<ray::Texture2D> loading_graphic;
    double start_ms = 0;
    double skipped_time = 0;
    // ROUND 19 (result press semantics -- Graphics/result/MAPPING.md ROUND 19).
    // Every constant below is a frame count out of 39.06
    // `script_lua/result/ResultMain.lua`. ROUND 24 correction (user-supplied, this
    // machine's script_lua does not itself define `Common.FPS` -- it is a native
    // engine binding with no in-Lua fallback to grep for -- so this cannot be
    // independently re-derived from source; taken as ground truth per the user):
    // the cabinet's script-logic tick is 120 fps, not 60. 1 frame = 1000/120 ms.
    // This HALVES every wait below versus the round-19 (60fps) build, and is the
    // direct explanation for the "結算過太久" (RESULT takes too long) report --
    // round 19's re-verification concluded "no bug" only because it inherited the
    // same wrong 60fps assumption baked in here since round 19 first wrote it.
    static constexpr double kFrameMs        = 1000.0 / 120.0;
    // WaitUpdateTamashiiGage: `if this.timer_ > 100 then ... this.isEnableSkip_ = true`.
    // Before that the cabinet ignores the drum entirely.
    static constexpr double kEnableSkipMs   =  100 * kFrameMs;   //  833.3
    // Medal -> WaitEffectEnd: `elseif this.kWaitEffectEndTime_ < this.timer_`,
    // kWaitEffectEndTime_ = 500.  isEffectSkip_ does NOT collapse this state.
    static constexpr double kWaitEffectEndMs =  500 * kFrameMs;  //  4166.7
    // WaitNextScene (timer_ reset to 0 on entry):
    //   `if this.timer_ > 500 and (AVAILABLE_OK or AVAILABLE_CANCEL) then isNextScene = true`
    //   `elseif this.timer_ > 3600 then isNextScene = true`
    static constexpr double kWaitNextSceneMs =  500 * kFrameMs;  //  4166.7  press accepted
    static constexpr double kAutoNextSceneMs = 3600 * kFrameMs;  // 30000.0  advances by itself
    double skip_enabled_ms = 0;   // engine-clock ms at which a collapse press becomes legal
    std::optional<ResultPlayer> player_1;
    std::optional<FadeIn> fade_in;
    std::unique_ptr<SongNum> song_num;

    void handle_input(double current_ms);

    // ROUND 19: engine-clock ms at which the reveal sequence is over, or 0 while it
    // is still running.  RESULT_2P overrides it -- the cabinet runs ONE state machine
    // for both boards, so the screen is "done" only once both boards are.
    virtual double reveal_end_ms();
    // Shared by RESULT and RESULT_2P: the whole press/timeout rule, so the two
    // screens cannot drift apart again.
    void update_input_and_timeout(double current_ms);

    void draw_overlay();

    void draw_song_info();
public:
    ResultScreen() : Screen("result") {
    }

    void on_screen_start() override;

    std::optional<Screens> update() override;

    Screens on_screen_end(Screens next_screen) override;

    void draw() override;
};
