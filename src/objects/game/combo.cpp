#include "combo.h"
#include "../../libs/texture.h"
#include "../../libs/global_data.h"
#include <cmath>

Combo::Combo(int combo, double current_ms)
    : combo(combo) {
    stretch = (TextStretchAnimation*)tex.get_animation(5, true);
    color = {ray::Fade(ray::WHITE, 1), ray::Fade(ray::WHITE, 1), ray::Fade(ray::WHITE, 1)};
    glimmer_map[0] = 0;
    glimmer_map[1] = 0;
    glimmer_map[2] = 0;
    total_time = 250;
    cycle_time = total_time * 2;
    start_times = {
                current_ms,
                current_ms + (2.0f / 3.0f) * cycle_time,
                current_ms + (4.0f / 3.0f) * cycle_time
    };

    // ROUND 54: real clip-sampled glimmer (see combo.h). All-or-nothing: any
    // missing piece keeps the historical hardcoded ramp untouched.
    glim_scale = {1.0f, 1.0f, 1.0f};
    glim_start_ms = current_ms;
    glim_tbl = get_sample_table("combo_glimmer");
    gleam_tbl = get_sample_table("combo_gleam");
    if (glim_tbl && gleam_tbl) {
        glim_inst[0] = glim_tbl->track("#6@1/#5@0");
        glim_inst[1] = glim_tbl->track("#6@1/#5@1");
        glim_inst[2] = glim_tbl->track("#6@1/#5@2");
        gleam_trk = gleam_tbl->track("#3@0");
        clip_glimmer = glim_inst[0] && glim_inst[1] && glim_inst[2] && gleam_trk &&
                       glim_tbl->field("ty") >= 0 && gleam_tbl->field("sx") >= 0 &&
                       gleam_tbl->field("a") >= 0;
    }
}

// Dump-verified pulse geometry that the exporter's presence-flattening cannot
// express (ANIM_COVERAGE.md ROUND 53 limitation note): inside the 110-frame
// digit-MC loop each gleam instance re-enters every 32 frames and lives for
// the gleam clip's own 15 frames (frame-by-frame verified against the dense
// dump of combo_number.nulm #25: instance 0 present f12-26/44-58/76-90/108-,
// instance 1 f18-32/..., instance 2 f27-41/...).
static constexpr double GLIM_LOOP_FRAMES = 110.0;
static constexpr double GLIM_PULSE_STRIDE = 32.0;

void Combo::update_glimmer_clip(double current_ms) {
    const double pf = std::fmod((current_ms - glim_start_ms) / glim_tbl->ms_per_frame(),
                                GLIM_LOOP_FRAMES);
    const int col_ty = glim_tbl->field("ty");
    const int col_sx = gleam_tbl->field("sx");
    const int col_a  = gleam_tbl->field("a");
    const double window = gleam_trk->last_frame() - gleam_trk->first_frame();

    for (int i = 0; i < 3; i++) {
        const SampleTrack* inst = glim_inst[i];
        const double s0 = inst->first_frame();
        double fade = 0.0, dy = 0.0, scale = 1.0;
        if (pf >= s0) {
            const double cf = std::fmod(pf - s0, GLIM_PULSE_STRIDE);
            if (cf <= window) {
                fade  = gleam_tbl->sample(*gleam_trk, col_a,
                                          gleam_trk->first_frame() + cf);
                scale = gleam_tbl->sample(*gleam_trk, col_sx,
                                          gleam_trk->first_frame() + cf);
                dy = glim_tbl->sample(*inst, col_ty, pf) - inst->rows.front()[col_ty + 1];
            }
        }
        glimmer_map[i] = static_cast<int>(std::lround(dy));
        glim_scale[i] = static_cast<float>(scale);
        color[i] = ray::Fade(ray::WHITE, static_cast<float>(fade));
    }
}

void Combo::update_count(int curr_combo) {
    if (curr_combo != combo) {
        combo = curr_combo;
        stretch->start();
    }
}

void Combo::update(double current_ms, int curr_combo) {
    update_count(curr_combo);
    stretch->update(current_ms);

    if (clip_glimmer) {
        update_glimmer_clip(current_ms);
        return;
    }

    for (size_t i = 0; i < 3; i++) {
        double elapsed_time = current_ms - start_times[i];
        if (elapsed_time > cycle_time) {
            double cycles_completed = std::floor(elapsed_time / cycle_time);
            start_times[i] += cycles_completed * cycle_time;
            elapsed_time = current_ms - start_times[i];
        }
        float fade;
        if (elapsed_time <= total_time) {
            glimmer_map[i] = -int(elapsed_time / 16.67);
            float fade_start_time = total_time - 164;
            if (elapsed_time >= fade_start_time) {
                fade = 1 - (elapsed_time - fade_start_time) / 164;
            } else {
                fade = 1;
            }
        } else {
            glimmer_map[i] = 0;
            fade = 0;
        }
        color[i] = ray::Fade(ray::WHITE, fade);
    }
}

void Combo::draw(float y) {
    // Optional skin key `combo_min {x}`: the combo at which the lane counter
    // starts being drawn. The arcade shows it from 10; this engine has always
    // shown it from 3. A skin that never declares the key keeps the 3.
    int min_combo = 3;
    if (const SkinInfo* m = tex.skin_entry("combo_min"); m && m->x > 0)
        min_combo = static_cast<int>(m->x);
    if (combo < min_combo) return;

    std::string counter = std::to_string(combo);
    float margin;
    float total_width;

    // Digit tiers. `combo_tier.x` is the combo at which the counter switches from
    // combo/counter to combo/counter_100; `combo_tier.y` (0 = disabled) is the combo
    // at which it switches again to the optional combo/counter_gold. A skin that ships
    // neither key nor the extra texture keeps the original two-tier behaviour
    // (switch at 100, no third set).
    int tier_mid = static_cast<int>(tex.skin_config[SC::COMBO_TIER].x);
    int tier_top = static_cast<int>(tex.skin_config[SC::COMBO_TIER].y);
    if (tier_mid <= 0) tier_mid = 100;

    TexID counter_tex = COMBO::COUNTER_100;
    if (tier_top > 0 && combo >= tier_top && tex.has_texture("combo/counter_gold")) {
        counter_tex = tex.get_enum("combo/counter_gold");
    }

    if (combo < tier_mid) {
        margin = tex.skin_config[SC::COMBO_MARGIN].x;
        total_width = counter.length() * margin;
        tex.draw_texture(tex.get_enum("combo/combo_" + global_data.config->general.language), {.y=y});
        for (int i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(COMBO::COUNTER, {.frame=digit - '0', .x=-(total_width / 2) + (i * margin), .y=y + (float)-stretch->attribute, .y2=(float)stretch->attribute});
        }

    } else {
        margin = tex.skin_config[SC::COMBO_MARGIN].y;
        total_width = counter.length() * margin;
        tex.draw_texture(tex.get_enum("combo/combo_100_" + global_data.config->general.language), {.y=y});
        for (int i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(counter_tex, {.frame=digit - '0', .x=-(total_width / 2) + (i * margin), .y=y + (float)-stretch->attribute, .y2=(float)stretch->attribute});
        }
        std::vector<std::pair<float, float>> glimmer_positions = {
            {tex.skin_config[SC::COMBO_GLIMMER_1].x, tex.skin_config[SC::COMBO_GLIMMER_1].y},
            {tex.skin_config[SC::COMBO_GLIMMER_2].x, tex.skin_config[SC::COMBO_GLIMMER_2].y},
            {tex.skin_config[SC::COMBO_GLIMMER_3].x, tex.skin_config[SC::COMBO_GLIMMER_3].y}
        };
        for (size_t j = 0; j < glimmer_positions.size(); j++) {
            auto [x, y_pos] = glimmer_positions[j];
            // clip mode adds the gleam's own 1->1.4->1 scale pulse (centered,
            // so scale 1.0 draws exactly where the legacy path always did)
            const float s = clip_glimmer ? glim_scale[j] : 1.0f;
            for (int i = 0; i < 3; i++) {
                tex.draw_texture(COMBO::GLEAM, {.color=color[j], .scale=s, .center=true, .x=x+(i*tex.skin_config[SC::COMBO_MARGIN].x), .y=y+y_pos+glimmer_map[j]});
            }
        }
    }
}
