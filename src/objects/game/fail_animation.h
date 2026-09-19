#pragma once

#include "../../libs/animation.h"

#include "../../libs/texture.h"

class FailAnimation {
private:
    bool is_2p;
    FadeAnimation* bachio_fade_in;
    TextureChangeAnimation* bachio_texture_change;
    MoveAnimation* bachio_fall;
    MoveAnimation* bachio_move_out;
    FadeAnimation* bachio_boom_fade_in;
    TextureResizeAnimation* bachio_boom_scale;
    MoveAnimation* bachio_up;
    MoveAnimation* bachio_down;
    FadeAnimation* text_fade_in;
    std::string name;
    int frame;
    TextureObject* t_fail = nullptr;
    TextureObject* t_bachio_boom = nullptr;
    TextureObject* t_bachio_l_in = nullptr;
    TextureObject* t_bachio_l_fall = nullptr;
    TextureObject* t_bachio_r_in = nullptr;
    TextureObject* t_bachio_r_fall = nullptr;

public:
    FailAnimation(bool is_2p);

    void update(double current_ms);
    void draw();
};
