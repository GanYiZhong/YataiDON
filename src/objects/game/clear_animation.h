#pragma once

#include "../../libs/animation.h"

struct TextureObject;

class ClearAnimation {
private:
    bool is_2p;
    FadeAnimation* bachio_fade_in;
    TextureChangeAnimation* bachio_texture_change;
    TextureChangeAnimation* bachio_out;
    MoveAnimation* bachio_move_out;
    std::vector<FadeAnimation*> clear_separate_fade_in;
    std::vector<TextStretchAnimation*> clear_separate_stretch;
    FadeAnimation* clear_highlight_fade_in;
    bool draw_clear_full;
    std::string name;
    int frame;
    TextureObject* t_clear = nullptr;
    TextureObject* t_clear_separated = nullptr;
    TextureObject* t_clear_highlight = nullptr;
    TextureObject* t_bachio_l_in = nullptr;
    TextureObject* t_bachio_l_out = nullptr;
    TextureObject* t_bachio_r_in = nullptr;
    TextureObject* t_bachio_r_out = nullptr;

public:
    ClearAnimation(bool is_2p);

    void update(double current_ms);
    void draw();
};
