#include "drum_hit_effect.h"
#include "../../libs/texture.h"

DrumHitEffect::DrumHitEffect(DrumType type, Side side)
            : type(type), side(side) {
    fade = (FadeAnimation*)tex.get_animation(1, true);
    fade->start();
}

void DrumHitEffect::update(double current_ms) {
    fade->update(current_ms);
}

void DrumHitEffect::draw(float y) {
}

bool DrumHitEffect::is_finished() const {
    return fade->is_finished;
}
