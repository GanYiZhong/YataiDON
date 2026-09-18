#include "branch_indicator.h"
#include "../../libs/texture.h"
#include <algorithm>
#include <stdexcept>

namespace {
    constexpr int BRANCH_ANIM_DIFF_DOWN = 41;
    constexpr int BRANCH_ANIM_DIFF_UP = 42;
    constexpr int BRANCH_ANIM_DIFF_FADE = 43;
    constexpr int BRANCH_ANIM_LEVEL_FADE = 44;
    constexpr int BRANCH_ANIM_LEVEL_SCALE = 45;

    template <class T>
    T* require_anim(int id) {
        auto* a = dynamic_cast<T*>(tex.get_animation(id));
        if (!a) throw std::runtime_error("animation " + std::to_string(id) + " has unexpected type");
        return a;
    }
}

BranchIndicator::BranchIndicator()
    : difficulty(BranchDifficulty::NORMAL), diff_2(BranchDifficulty::NORMAL), direction(1) {

    diff_down = require_anim<MoveAnimation>(BRANCH_ANIM_DIFF_DOWN);
    diff_up = require_anim<MoveAnimation>(BRANCH_ANIM_DIFF_UP);
    diff_fade = require_anim<FadeAnimation>(BRANCH_ANIM_DIFF_FADE);
    level_fade = require_anim<FadeAnimation>(BRANCH_ANIM_LEVEL_FADE);
    level_scale = require_anim<TextureResizeAnimation>(BRANCH_ANIM_LEVEL_SCALE);
}

void BranchIndicator::update(double current_ms) {
    diff_down->update(current_ms);
    diff_up->update(current_ms);
    diff_fade->update(current_ms);
    level_fade->update(current_ms);
    level_scale->update(current_ms);
}

void BranchIndicator::level_up(BranchDifficulty difficulty) {
    diff_2 = this->difficulty;
    this->difficulty = difficulty;
    diff_down->start();
    diff_up->start();
    diff_fade->start();
    level_fade->start();
    level_scale->start();
    direction = 1;
}

void BranchIndicator::level_down(BranchDifficulty difficulty) {
    diff_2 = this->difficulty;
    this->difficulty = difficulty;
    diff_down->start();
    diff_up->start();
    diff_fade->start();
    level_fade->start();
    level_scale->start();
    direction = -1;
}

void BranchIndicator::draw(float y) {
    if (difficulty == BranchDifficulty::EXPERT) {
        tex.draw_texture(BRANCH::EXPERT_BG, {.y=y, .fade = std::clamp(1.0f - (float)diff_fade->attribute, 0.0f, 0.5f)});
    } else if (difficulty == BranchDifficulty::MASTER) {
        tex.draw_texture(BRANCH::MASTER_BG, {.y=y, .fade = std::clamp(1.0f - (float)diff_fade->attribute, 0.0f, 0.5f)});
    }

    std::string level_texture = direction == -1 ? "level_down" : "level_up";
    tex.draw_texture(tex.get_enum("branch/" + (level_texture)), {.scale = (float)level_scale->attribute, .center = true, .y=y, .fade = level_fade->attribute});

    tex.draw_texture(tex.get_enum("branch/" + (branch_diff_to_string(diff_2))), {.y = y + (float)(diff_down->attribute - diff_up->attribute) * direction, .fade = diff_fade->attribute});

    tex.draw_texture(tex.get_enum("branch/" + (branch_diff_to_string(difficulty))), {.y = y + (float)(diff_up->attribute * (direction * -1)) - (tex.skin_config[SC::BRANCH_INDICATOR_Y_OFFSET].y * direction * -1), .fade = 1 - diff_fade->attribute});
}
