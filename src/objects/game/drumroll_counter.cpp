#include "drumroll_counter.h"
#include "../../libs/texture.h"

static constexpr int DRUMROLL_COUNTER_FADE_ANIM_ID = 8;
static constexpr int DRUMROLL_COUNTER_STRETCH_ANIM_ID = 9;

DrumrollCounter::DrumrollCounter() {
     drumroll_count = 0;
     fade = dynamic_cast<FadeAnimation*>(tex.get_animation(DRUMROLL_COUNTER_FADE_ANIM_ID));
     stretch = dynamic_cast<TextStretchAnimation*>(tex.get_animation(DRUMROLL_COUNTER_STRETCH_ANIM_ID));
}

void DrumrollCounter::update_count(int count) {
    if (drumroll_count != count) {
        drumroll_count = count;
        fade->start();
        stretch->start();
    }
}

void DrumrollCounter::update(double current_ms, int count) {
    fade->update(current_ms);
    stretch->update(current_ms);

    update_count(count);
}

void DrumrollCounter::update_animations(double current_ms) {
    fade->update(current_ms);
    stretch->update(current_ms);
}

void DrumrollCounter::draw(float y) {
    tex.draw_texture(DRUMROLL_COUNTER::BUBBLE, {.y=y, .fade=fade->attribute});
    std::string counter = std::to_string(drumroll_count);
    const float margin = tex.skin_config[SC::DRUMROLL_COUNTER_MARGIN].x;
    float total_width = static_cast<float>(counter.length()) * margin;
    for (size_t i = 0; i < counter.size(); i++) {
        char digit = counter[i];
        if (digit < '0' || digit > '9') continue;
        tex.draw_texture(DRUMROLL_COUNTER::COUNTER, {.frame=digit - '0', .x=-(total_width/2.0f)+(i*margin), .y=y -(float)stretch->attribute, .y2=(float)stretch->attribute, .fade=fade->attribute});
    }
}

bool DrumrollCounter::is_finished() const {
    return fade->is_finished;
}
