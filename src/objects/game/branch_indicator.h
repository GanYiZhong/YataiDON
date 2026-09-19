#pragma once

#include "../../libs/animation.h"
#include "../enums.h"

struct TextureObject;

class BranchIndicator {
private:
    BranchDifficulty diff_2;
    MoveAnimation* diff_down;
    MoveAnimation* diff_up;
    FadeAnimation* diff_fade;
    FadeAnimation* level_fade;
    TextureResizeAnimation* level_scale;
    int direction;
    TextureObject* t_expert_bg = nullptr;
    TextureObject* t_master_bg = nullptr;
    TextureObject* t_level_up = nullptr;
    TextureObject* t_level_down = nullptr;
    TextureObject* t_diff[3] = {};  // indexed by BranchDifficulty

public:
    BranchDifficulty difficulty;
    BranchIndicator();

    void update(double current_ms);
    void level_up(BranchDifficulty difficulty);
    void level_down(BranchDifficulty difficulty);
    void draw(float y);
};
