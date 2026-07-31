#include "gauge.h"
#include "../../libs/texture.h"

ResultGauge::ResultGauge(GaugeMode mode, PlayerNum player_num, float gauge_length, bool is_2p)
    : mode(mode), player_num(player_num), gauge_length(gauge_length), is_2p(is_2p) {

    if (mode == GaugeMode::NORMAL) {
        gauge_max    = 87.0f;
        scale        = 10.0f / 11.0f;
        difficulty   = std::min(Difficulty::HARD, Difficulty(global_data.session_data[(int)player_num].selected_difficulty));
        clear_start  = {52, 60, 69};

        if (difficulty >= Difficulty::HARD)        string_diff = "_hard";
        else if (difficulty >= Difficulty::NORMAL)  string_diff = "_normal";
        else                                        string_diff = "_easy";

        gauge_fade_in        = (FadeAnimation*)tex.get_animation(17);
        tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(20);
        gauge_fade_in->start();
        anims_loaded = true;

        if (gauge_length == gauge_max)
            state = ResultState::RAINBOW;
        else if (gauge_length >= clear_start[(int)difficulty] - 1)
            state = ResultState::CLEAR;
        else
            state = ResultState::FAIL;
    } else {
        // DAN mode: animations loaded lazily (same as DanResultGauge pattern)
        gauge_max     = 89.0f;
        scale         = 1.0f;
        clear_start   = {};
        string_diff   = "";
        visual_length = gauge_length * 8.0f;

        gauge_fade_in        = nullptr;
        tamashii_fire_change = nullptr;

        state = (gauge_length == gauge_max) ? ResultState::RAINBOW : ResultState::FAIL;
    }
}

void ResultGauge::update(double current_ms) {
    if (mode == GaugeMode::DAN && !anims_loaded) {
        tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
        gauge_fade_in        = (FadeAnimation*)tex.get_animation(63);
        gauge_fade_in->start();
        anims_loaded = true;
    }

    if (state == ResultState::RAINBOW) {
        if (rainbow_start_ms < 0) rainbow_start_ms = current_ms;
        rainbow_frac = (float)fmod((current_ms - rainbow_start_ms) / 75.0, 8.0);
    }
    if (tamashii_fire_change) tamashii_fire_change->update(current_ms);
    if (gauge_fade_in) gauge_fade_in->update(current_ms);
}

void ResultGauge::draw(double external_fade) {}
