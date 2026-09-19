#pragma once

#include "../../libs/animation.h"

struct TextureObject;

class Fireworks {
private:
    TextureChangeAnimation* explosion_anim;
    TextureObject* t_explosion = nullptr;

public:
    Fireworks();

    void update(double current_ms);
    void draw();

    bool is_finished();
};
