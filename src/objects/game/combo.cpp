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
    if (combo < 3) return;

    std::string counter = std::to_string(combo);

    // Combo colour tiers: white (default) -> silver at 50+ -> gold at 100+.
    // The skin ships counter (white), counter_100 (silver) and counter_gold
    // (gold); the gold sheet is optional, so fall back to the silver sheet when
    // a skin does not provide it. Only the 100+ tier keeps the wider three-digit
    // spacing and the gleam animation.
    const bool gold   = combo >= 100;
    const bool silver = combo >= 50 && combo < 100;

    auto have = [&](TexID id) {
        return tex.textures.find((uint32_t)id) != tex.textures.end();
    };
    TexID digit_tex = COMBO::COUNTER;
    if (gold)        digit_tex = have(COMBO::COUNTER_GOLD) ? COMBO::COUNTER_GOLD : COMBO::COUNTER_100;
    else if (silver) digit_tex = COMBO::COUNTER_100;

    float margin;
    float total_width;
    if (!gold) {
        margin = tex.skin_config[SC::COMBO_MARGIN].x;
        total_width = counter.length() * margin;
        tex.draw_texture(tex.get_enum("combo/combo_" + global_data.config->general.language), {.y=y});
        for (int i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(digit_tex, {.frame=digit - '0', .x=-(total_width / 2) + (i * margin), .y=y + (float)-stretch->attribute, .y2=(float)stretch->attribute});
        }

    } else {
        margin = tex.skin_config[SC::COMBO_MARGIN].y;
        total_width = counter.length() * margin;
        tex.draw_texture(tex.get_enum("combo/combo_100_" + global_data.config->general.language), {.y=y});
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
                tex.draw_texture(COMBO::GLEAM, {.color=color[j], .x=x+(i*tex.skin_config[SC::COMBO_MARGIN].x), .y=y+y_pos+glimmer_map[j]});
            }
        }
    }
}
