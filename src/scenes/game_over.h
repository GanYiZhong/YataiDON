#pragma once

#include "../libs/screen.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"

class GameOverScreen : public Screen {
private:
    AllNetIcon allnet_indicator;
    CoinOverlay coin_overlay;

    MoveAnimation* curtain_pull_out;
    MoveAnimation* curtain_pull_in;
    TextureChangeAnimation* kitsune_texture_change;
    MoveAnimation* text_bounce_down;
    MoveAnimation* text_bounce_up;
    MoveAnimation* text_bounce_down_2;
    FadeAnimation* fade_out;
    // Optional copyright phase (arcade end_curtain_mc f229..f266). Both are
    // nullptr unless the skin defines animations 8/9 AND a global/copyright
    // texture, in which case the screen holds the blackout while the plate
    // fades in and rests before handing back to TITLE.
    FadeAnimation* copyright_fade = nullptr;
    FadeAnimation* copyright_hold = nullptr;
    bool has_copyright = false;
    // Optional blackout cue. The arcade starts the copyright blackout on
    // end_curtain_mc f210 = 3433 ms after the curtain starts; the engine's own
    // cue is "the jingle stopped", which ties an arcade timing to whatever
    // length the skin's jingle happens to be. nullptr => the jingle rule.
    FadeAnimation* blackout_cue = nullptr;
    bool ad_played;
    bool voice_played;

public:
    GameOverScreen() : Screen("game_over") {
    }

    void on_screen_start() override;

    Screens on_screen_end(Screens next_screen) override;

    std::optional<Screens> update() override;

    void draw() override;
};
