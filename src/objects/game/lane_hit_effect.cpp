#include "lane_hit_effect.h"
#include "../../libs/texture.h"

LaneHitEffect::LaneHitEffect(DrumType type, Judgments judgment)
            : type(type), judgment(judgment) {
    fade = (FadeAnimation*)tex.get_animation(0, true);
    fade->start();
}

void LaneHitEffect::update(double current_ms) {
    fade->update(current_ms);
}

void LaneHitEffect::draw(float y) {
}

bool LaneHitEffect::is_finished() const {
    return fade->is_finished;
}
