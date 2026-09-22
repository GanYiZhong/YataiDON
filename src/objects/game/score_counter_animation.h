#pragma once

#include "../../libs/global_data.h"
#include "../../libs/animation.h"

#include "../../libs/texture.h"

class ScoreCounterAnimation {
private:
    TextureObject* t_score_number = nullptr;
    int counter;
    int direction;
    FadeAnimation* fade_animation_1;
    MoveAnimation* move_animation_x;
    FadeAnimation* fade_animation_2;
    MoveAnimation* move_animation_y_pre;
    MoveAnimation* move_animation_y_fan;
    ray::Color base_color;
    ray::Color color;
    std::string counter_str;
    float total_width;
    float margin;
    std::vector<float> y_pos_list;

public:
    ScoreCounterAnimation(PlayerNum player_num, int counter, bool is_2p);

    void update(double current_ms);
    void draw(float y);

    bool is_finished() const;
};
