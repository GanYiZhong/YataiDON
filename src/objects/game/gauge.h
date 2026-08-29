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
    // ROUND 52 (r52-lua-divergence-fixes): raw-soul read for the automation
    // live data-out (global_data.h).
    double get_soul() const { return soul; }
    float get_progress() const { return gauge_length / gauge_max; }
    // The gauge-up flash's current fade value (1 -> 0, same animation the
    // engine's own draw() gates its BAR_CLEAR_*_FADE / <pn>p_bar_fade block
    // on), or 0 when no flash is active. A skin needs this to draw its OWN
    // flash on its own segment grid: the engine's copy is pinned to gauge.cpp's
    // 12px pitch (87 units over the plate), which does not line up with the
    // arcade's 21px / 50-segment grid a Lua overlay redraws the fill on, so the
    // stock flash can land visibly ahead of or behind the redrawn fill edge.
    float get_flash_attribute() const {
        if (!gauge_update_anim) return 0.0f;
        if ((int)gauge_length <= (int)previous_length) return 0.0f;
        return gauge_update_anim->attribute;
    }

    // ROUND 18 (r18-misc): the clear (norma) line as a fraction of the bar, i.e.
    // the divisor the arcade's background-dancer rule uses (Background::handle_gauge
    // -> Scripts/background/background.lua handle_dancer_count). Returns 1.0 for
    // modes that have no clear_start table (DAN, result-only gauges).
    float get_clear_progress() const {
        // ROUND 48 (r48-soulgauge-chn-port): under the CHN05 model the clear
        // (norma) line is the raw-soul norma (6000/7000/7000/8000 of 10000),
        // not the 87-unit clear_start display cell.
        if (chn_model && norma > 0) return (float)norma / 10000.0f;
        if (clear_start.empty() || gauge_max <= 0.0f) return 1.0f;
        // No std::min here on purpose: gauge.h must not depend on <algorithm>
        // being pulled in transitively by whoever includes it.
        int i = difficulty;
        if (i < 0) i = 0;
        if (i >= (int)clear_start.size()) i = (int)clear_start.size() - 1;
        return clear_start[i] / gauge_max;
    }

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

    // ROUND 48 (r48-soulgauge-chn-port): CHN05 soul-gauge state. The
    // authoritative gauge is the raw soul, a double in [0, 10000]
    // (TamashiiMax, gauge_rank.md §1c/§2), accumulated in integer
    // per-chart TamashiiPoint words exactly like UpdateTamashii
    // (0x140134140) + UpdateScore (0x14055FA80). gauge_length stays as
    // the derived 87-unit DISPLAY value so every draw path is unchanged.
    // TJA charts carry no TamashiiPoint words, so tp_* are produced by
    // the corpus-fitted CHN05 authoring generator (see gauge.cpp and
    // ENGINE_BINDINGS.md ROUND 48). chn_model is the
    // YATAIDON_R48_DISABLE one-binary A/B gate (default ON = CHN05).
    bool chn_model = false;
    double soul = 0.0;
    int tp_great = 0;   // TamashiiPoint[0] (良 gain, raw-soul units)
    int tp_good = 0;    // TamashiiPoint[1] (可 gain)
    int tp_loss = 0;    // TamashiiPoint[2] (不可/miss, stored NEGATIVE)
    int norma = 0;      // TamashiiNorm (clear line, raw-soul units)
    int art_index = 0;  // clear-zone art tier: 0=easy 1=normal(+hard) 2=oni
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
