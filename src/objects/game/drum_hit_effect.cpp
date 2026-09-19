#include "drum_hit_effect.h"

DrumHitEffect::DrumHitEffect(DrumType type, Side side)
            : type(type), side(side) {
    fade = (FadeAnimation*)tex.get_animation(1, true);
    fade->start();

    if (type == DrumType::DON) {
        t_effect = (side == Side::LEFT) ? tex.get_texture("lane/drum_don_l") : tex.get_texture("lane/drum_don_r");
    } else if (type == DrumType::KAT) {
        t_effect = (side == Side::LEFT) ? tex.get_texture("lane/drum_kat_l") : tex.get_texture("lane/drum_kat_r");
    }
}

void DrumHitEffect::update(double current_ms) {
    fade->update(current_ms);
}

void DrumHitEffect::draw(float y) {
    tex.draw_texture(t_effect, {.y=y, .fade=fade->attribute});
}

bool DrumHitEffect::is_finished() const {
    return fade->is_finished;
}
