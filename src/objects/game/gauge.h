#pragma once

#include "../../libs/global_data.h"
#include "../../libs/animation.h"
#include "../enums.h"

// The one gauge object: handles live gameplay (NORMAL and DAN mode) and the
// result-screen presentation (a fixed final length with its own fade/state),
// which previously lived in a separate ResultGauge copy. Lengths are stored
// in gauge units only; pixel widths are derived from the skin's bar texture
// at draw time, so any skin resolution renders correctly.
class Gauge {
public:
    float gauge_length;
    float gauge_max;

    // Live gameplay gauge.
    Gauge(GaugeMode mode, PlayerNum player_num, int total_notes, int difficulty = 0, int level = 1);

    // Result-screen gauge: shows a fixed final length.
    static Gauge make_result(GaugeMode mode, PlayerNum player_num, float gauge_length, bool is_2p = false);

    void add_good();
    void add_ok();
    void add_bad();
    void update(double current_ms);
    void draw(float y = 0.0f);
    // Result-screen presentation (NORMAL uses its own fade-in; DAN follows
    // the caller-supplied fade).
    void draw_result(double external_fade = 1.0);

    bool get_is_clear() const { return is_clear; }
    bool get_is_rainbow() const { return is_rainbow; }
    float get_progress() const { return gauge_length / gauge_max; }

    ResultState get_state() const { return state; }
    bool result_is_clear() const { return state == ResultState::CLEAR || state == ResultState::RAINBOW; }
    bool result_is_finished() const { return gauge_fade_in && gauge_fade_in->is_finished; }

private:
    Gauge();  // bare init for make_result

    GaugeMode mode;
    PlayerNum player_num;
    int total_notes;
    int difficulty;

    // NORMAL mode
    std::string string_diff;
    std::vector<int> clear_start;
    int level;
    struct GaugeTable {
        float clear_rate;
        float ok_multiplier;
        float bad_multiplier;
    };
    std::vector<std::vector<GaugeTable>> table;
    double rainbow_start_ms = -1.0;
    float rainbow_frac = 0.0f;

    bool anims_loaded = false;

    // Result presentation
    bool is_result = false;
    bool is_2p = false;
    float result_scale = 1.0f;
    ResultState state = ResultState::FAIL;
    FadeAnimation* gauge_fade_in = nullptr;

    // Shared
    float previous_length;
    bool is_clear;
    bool is_rainbow;
    TextureChangeAnimation* tamashii_fire_change;
    FadeAnimation* gauge_update_anim;
    std::optional<FadeAnimation*> rainbow_fade_in;
};
