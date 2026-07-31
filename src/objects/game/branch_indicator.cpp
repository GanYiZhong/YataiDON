#include "branch_indicator.h"
#include "../../libs/texture.h"

BranchIndicator::BranchIndicator()
    : difficulty(BranchDifficulty::NORMAL), diff_2(BranchDifficulty::NORMAL), direction(1) {

    diff_down = (MoveAnimation*)tex.get_animation(41);
    diff_up = (MoveAnimation*)tex.get_animation(42);
    diff_fade = (FadeAnimation*)tex.get_animation(43);
    level_fade = (FadeAnimation*)tex.get_animation(44);
    level_scale = (TextureResizeAnimation*)tex.get_animation(45);
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
}
