#pragma once

#include "../enums.h"
#include "../../libs/animation.h"

#include "../../libs/texture.h"

class Judgment {
private:
    Judgments type;
    bool big;

    FadeAnimation* fade_animation_1;
    FadeAnimation* fade_animation_2;
    MoveAnimation* move_animation;
    TextureChangeAnimation* texture_animation;
    TextureObject* t_effect = nullptr;
    TextureObject* t_outer_effect = nullptr;
    TextureObject* t_text = nullptr;

public:
    Judgment(Judgments type, bool big);

    void update(double current_ms);

    void draw_effect(float judge_x, float judge_y);

    void draw_outer_effect(float judge_x, float judge_y);

    void draw_text(float judge_x, float judge_y);

    bool is_finished() const;
};
