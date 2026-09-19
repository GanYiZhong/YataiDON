#pragma once

#include "../../libs/animation.h"

struct TextureObject;

class GogoTime {
private:
    TextureResizeAnimation* fire_resize;
    TextureChangeAnimation* fire_change;
    float fire_fade;
    TextureObject* t_fire = nullptr;

public:
    GogoTime();

    void update(double current_ms);
    void draw(float judge_x, float judge_y);
};
