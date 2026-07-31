#include "combo.h"
#include "../../libs/texture.h"
#include "../../libs/global_data.h"
#include <math.h>

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
}
