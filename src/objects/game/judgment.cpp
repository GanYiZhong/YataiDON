#include "judgment.h"
#include "../../libs/texture.h"

Judgment::Judgment(Judgments type, bool big)
    : type(type), big(big) {

    fade_animation_1 = dynamic_cast<FadeAnimation*>(tex.get_animation(27, true));
    fade_animation_2 = dynamic_cast<FadeAnimation*>(tex.get_animation(28, true));
    move_animation = dynamic_cast<MoveAnimation*>(tex.get_animation(29, true));
    texture_animation = dynamic_cast<TextureChangeAnimation*>(tex.get_animation(30, true));

    move_animation->start();
    fade_animation_2->start();
    fade_animation_1->start();
    texture_animation->start();

    if (type == Judgments::GOOD) {
        t_effect = tex.get_texture(big ? "hit_effect/hit_effect_good_big" : "hit_effect/hit_effect_good");
        t_outer_effect = tex.get_texture(big ? "hit_effect/outer_good_big" : "hit_effect/outer_good");
        t_text = tex.get_texture("hit_effect/judge_good");
    } else if (type == Judgments::OK) {
        t_effect = tex.get_texture(big ? "hit_effect/hit_effect_ok_big" : "hit_effect/hit_effect_ok");
        t_outer_effect = tex.get_texture(big ? "hit_effect/outer_ok_big" : "hit_effect/outer_ok");
        t_text = tex.get_texture("hit_effect/judge_ok");
    } else if (type == Judgments::BAD) {
        t_text = tex.get_texture("hit_effect/judge_bad");
    }
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

void Judgment::draw_effect(float judge_x, float judge_y) {
    float fade = fade_animation_2->attribute;
    tex.draw_texture(t_effect, {.x=judge_x, .y=judge_y, .fade=fade});
}

void Judgment::draw_outer_effect(float judge_x, float judge_y) {
    int index = static_cast<int>(texture_animation->attribute);
    float hit_fade = fade_animation_1->attribute;
    tex.draw_texture(t_outer_effect, {.frame=index, .x=judge_x, .y=judge_y, .fade=hit_fade, .blend=ray::BLEND_ADDITIVE});
}

void Judgment::draw_text(float judge_x, float judge_y) {
    float y = move_animation->attribute;
    int index = static_cast<int>(texture_animation->attribute);
    float fade = fade_animation_2->attribute;

    if (type == Judgments::GOOD) {
        tex.draw_texture(t_text, {.frame=index, .x=judge_x, .y=y + judge_y, .fade=fade});
    } else {
        tex.draw_texture(t_text, {.x=judge_x, .y=y + judge_y, .fade=fade});
    }
}

bool Judgment::is_finished() const {
    return fade_animation_2->is_finished;
}
