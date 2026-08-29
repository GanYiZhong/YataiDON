#pragma once

#include "../../libs/animation.h"
#include "../../libs/ray.h"
#include "../../libs/sample_table.h"

class Combo {
private:
    int combo;
    TextStretchAnimation* stretch;
    std::vector<ray::Color> color;
    std::unordered_map<int, int> glimmer_map;
    int total_time;
    int cycle_time;
    std::vector<double> start_times;

    // ROUND 54 (r54-anim-engine-referrals): the glimmer was the one GAME-HUD
    // ramp with no skin lever (r53 flag) - 250/500 ms phases and the 164 ms
    // fade were hardcoded here.  When the skin ships anim/combo_glimmer (the
    // digit MC #25's three gleam placements) + anim/combo_gleam (the 15-frame
    // gleam pulse itself), the real curves drive it: 110-frame parent loop,
    // pulses every 32 frames starting at each instance's own frame, scale
    // 1->1.4->1, alpha flat then out, ty sweeping up 17 px per pulse.
    // FAIL-SOFT: either table missing => the historical hardcode above runs.
    const SampleTable* glim_tbl = nullptr;   // anim/combo_glimmer (#25)
    const SampleTable* gleam_tbl = nullptr;  // anim/combo_gleam (#5)
    const SampleTrack* glim_inst[3] = {nullptr, nullptr, nullptr};
    const SampleTrack* gleam_trk = nullptr;
    double glim_start_ms = 0.0;
    std::vector<float> glim_scale;           // per-instance gleam scale (1 = off)
    bool clip_glimmer = false;

    void update_count(int combo);
    void update_glimmer_clip(double current_ms);
public:
    Combo() = default;
    Combo(int combo, double current_ms);

    void update(double current_ms, int curr_combo);

    void draw(float y);
};
