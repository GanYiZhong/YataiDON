#include "combo.h"
#include "../../libs/global_data.h"
#include <cmath>
#include <stdexcept>

namespace {
    constexpr int COMBO_STRETCH_ANIM_ID = 5;
}

Combo::Combo(int combo, double current_ms)
    : combo(combo) {
    stretch = dynamic_cast<TextStretchAnimation*>(tex.get_animation(COMBO_STRETCH_ANIM_ID, true));
    if (stretch == nullptr) {
        throw std::runtime_error("combo stretch animation missing or of unexpected type");
    }
    color = {ray::Fade(ray::WHITE, 1), ray::Fade(ray::WHITE, 1), ray::Fade(ray::WHITE, 1)};
    glimmer_map[0] = 0;
    glimmer_map[1] = 0;
    glimmer_map[2] = 0;
    total_time = 250;
    cycle_time = total_time * 2;
    start_times = {
                current_ms,
                current_ms - (2.0f / 3.0f) * cycle_time,
                current_ms - (4.0f / 3.0f) * cycle_time
    };

    t_counter = tex.get_texture("combo/counter");
    t_counter_gold = tex.has_texture("combo/counter_gold") ? tex.get_texture("combo/counter_gold") : nullptr;
    t_counter_100 = tex.has_texture("combo/counter_100") ? tex.get_texture("combo/counter_100") : nullptr;
    t_gleam = tex.get_texture("combo/gleam");
    t_combo = tex.get_texture("combo/combo_" + global_data.config->general.language);
    t_combo_100 = tex.get_texture("combo/combo_100_" + global_data.config->general.language);
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

    for (size_t i = 0; i < 3; i++) {
        double elapsed_time = std::fmod(current_ms - start_times[i], cycle_time);
        if (elapsed_time < 0) elapsed_time += cycle_time;
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
    if (combo < 3) return;

    std::string counter = std::to_string(combo);

    const bool tiers  = tex.options[SCO::COMBO_COLOR_TIERS];
    const bool gold   = combo >= 100;
    const bool silver = tiers && combo >= 50 && combo < 100;

    TextureObject* digit_tex = t_counter;
    if (gold) {
        digit_tex = t_counter_gold ? t_counter_gold
                  : t_counter_100  ? t_counter_100
                  : t_counter;
    } else if (silver) {
        digit_tex = t_counter_100 ? t_counter_100 : t_counter;
    }

    float margin;
    float total_width;
    if (!gold) {
        margin = tex.skin_config[SC::COMBO_MARGIN].x;
        total_width = counter.length() * margin;
        tex.draw_texture(t_combo, {.y=y});
        for (int i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(digit_tex, {.frame=digit - '0', .x=-(total_width / 2) + (i * margin), .y=y + (float)-stretch->attribute, .y2=(float)stretch->attribute});
        }

    } else {
        margin = tex.skin_config[SC::COMBO_MARGIN].y;
        total_width = counter.length() * margin;
        tex.draw_texture(t_combo_100, {.y=y});
        for (int i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(digit_tex, {.frame=digit - '0', .x=-(total_width / 2) + (i * margin), .y=y + (float)-stretch->attribute, .y2=(float)stretch->attribute});
        }
        std::vector<std::pair<float, float>> glimmer_positions = {
            {tex.skin_config[SC::COMBO_GLIMMER_1].x, tex.skin_config[SC::COMBO_GLIMMER_1].y},
            {tex.skin_config[SC::COMBO_GLIMMER_2].x, tex.skin_config[SC::COMBO_GLIMMER_2].y},
            {tex.skin_config[SC::COMBO_GLIMMER_3].x, tex.skin_config[SC::COMBO_GLIMMER_3].y}
        };
        for (size_t j = 0; j < glimmer_positions.size(); j++) {
            auto [x, y_pos] = glimmer_positions[j];
            for (int i = 0; i < 3; i++) {
                tex.draw_texture(t_gleam, {.color=color[j], .x=x+(i*margin), .y=y+y_pos+glimmer_map[j]});
            }
        }
    }
}
