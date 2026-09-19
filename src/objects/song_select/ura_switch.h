#pragma once

#include "../../libs/animation.h"

#include "../../libs/texture.h"

class UraSwitchAnimation {
private:
    TextureChangeAnimation* texture_change;
    FadeAnimation* fade_out;
    TextureObject* t_ura_switch = nullptr;
public:
    UraSwitchAnimation();

    void start(bool is_backwards);

    void update(double current_ms);

    void draw();
};
