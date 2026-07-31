#include "judgment.h"
#include "../../libs/texture.h"

Judgment::Judgment(Judgments type, bool big)
    : type(type), big(big) {

    fade_animation_1 = (FadeAnimation*)tex.get_animation(27, true);
    fade_animation_2 = (FadeAnimation*)tex.get_animation(28, true);
    move_animation = (MoveAnimation*)tex.get_animation(29, true);
    texture_animation = (TextureChangeAnimation*)tex.get_animation(30, true);

    move_animation->start();
    fade_animation_2->start();
    fade_animation_1->start();
    texture_animation->start();
}

void Judgment::update(double current_ms) {
    BaseAnimation* animations[] = {
        fade_animation_1,
        fade_animation_2,
        move_animation,
        texture_animation
    };

    for (int i = 0; i < 4; i++) {
        animations[i]->update(current_ms);
    }
}

void Judgment::draw(float judge_x, float judge_y) {
}

bool Judgment::is_finished() const {
    return fade_animation_2->is_finished;
}
