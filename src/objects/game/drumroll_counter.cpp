#include "drumroll_counter.h"

#include <algorithm>
#include <stdexcept>

static constexpr int DRUMROLL_COUNTER_FADE_ANIM_ID = 8;
static constexpr int DRUMROLL_COUNTER_STRETCH_ANIM_ID = 9;

DrumrollCounter::DrumrollCounter() {
    drumroll_count = 0;
    fade = dynamic_cast<FadeAnimation*>(tex.get_animation(DRUMROLL_COUNTER_FADE_ANIM_ID));
    stretch = dynamic_cast<TextStretchAnimation*>(tex.get_animation(DRUMROLL_COUNTER_STRETCH_ANIM_ID));
    if (!fade || !stretch) {
        throw std::runtime_error("DrumrollCounter: animation 8/9 has an unexpected type");
    }
    t_bubble = tex.get_texture("drumroll_counter/bubble");
    t_counter = tex.get_texture("drumroll_counter/counter");
}

void DrumrollCounter::update_count(int count) {
    count = std::max(count, 0);
    if (drumroll_count != count) {
        drumroll_count = count;
        fade->start();
        stretch->start();
    }
}

void DrumrollCounter::update(double current_ms, int count) {
    update_count(count);

    fade->update(current_ms);
    stretch->update(current_ms);
}

void DrumrollCounter::update_animations(double current_ms) {
    fade->update(current_ms);
    stretch->update(current_ms);
}

void DrumrollCounter::draw(float y, const std::optional<SkinInfo>& place) {
    // With a place, the bubble and digits are scaled about the bubble's centre and moved there.
    const bool placed = place && place->width > 0;
    const float scale = placed ? place->width / static_cast<float>(t_bubble->x2[0]) : 1.0f;
    const float bubble_cx = static_cast<float>(t_bubble->x[0]) + static_cast<float>(t_bubble->x2[0]) / 2.0f;
    const float bubble_cy = static_cast<float>(t_bubble->y[0]) + static_cast<float>(t_bubble->y2[0]) / 2.0f;
    // the offset that puts a point at `base` (its own centre) where the scaled layout wants it
    auto shift = [&](float base, float centre, float target) { return target + ((base - centre) * scale) - base; };
    if (placed) {
        tex.draw_texture(t_bubble, {.scale = scale,
                                    .center = true,
                                    .x = place->x - bubble_cx,
                                    .y = place->y - bubble_cy,
                                    .fade = fade->attribute});
    } else {
        tex.draw_texture(t_bubble, {.y = y, .fade = fade->attribute});
    }
    std::string counter = std::to_string(drumroll_count);
    const float margin = tex.get_skin_config(SC::GAME_DRUMROLL_COUNTER_MARGIN)->x;
    float total_width = static_cast<float>(counter.length()) * margin;
    const float digit_cy = static_cast<float>(t_counter->y[0]) + static_cast<float>(t_counter->y2[0]) / 2.0f;
    const auto stretch_px = static_cast<float>(stretch->attribute) * scale;
    for (size_t i = 0; i < counter.size(); i++) {
        char digit = counter[i];
        if (digit < '0' || digit > '9') {
            continue;
        }
        float x = -(total_width / 2.0f) + (i * margin);
        float digit_y = y;
        if (placed) {
            const float digit_cx =
                static_cast<float>(t_counter->x[0]) + x + static_cast<float>(t_counter->x2[0]) / 2.0f;
            x += shift(digit_cx, bubble_cx, place->x);
            digit_y = shift(digit_cy, bubble_cy, place->y);
        }
        tex.draw_texture(t_counter, {.frame = digit - '0',
                                     .scale = scale,
                                     .center = placed,
                                     .x = x,
                                     .y = digit_y - stretch_px,
                                     .y2 = stretch_px,
                                     .fade = fade->attribute});
    }
}

bool DrumrollCounter::is_finished() const {
    return fade->is_finished;
}
