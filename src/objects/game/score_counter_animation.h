#pragma once

#include "../../libs/global_data.h"
#include "../../libs/animation.h"

class ScoreCounterAnimation {
private:
    int counter;
    int direction;
    FadeAnimation* fade_animation_1;
    MoveAnimation* move_animation_1;
    FadeAnimation* fade_animation_2;
    MoveAnimation* move_animation_2;
    MoveAnimation* move_animation_3;
    MoveAnimation* move_animation_4;
    ray::Color base_color;
    ray::Color color;
    std::string counter_str;
    float total_width;
    float margin;
    std::vector<float> y_pos_list;

public:
    // player_num picks the popup color (player identity); is_2p picks the
    // animation direction (lane layout). They differ in practice mode, where
    // a P2 player still plays on the top (1P-positioned) lane.
    ScoreCounterAnimation(PlayerNum player_num, int counter, bool is_2p);

    void update(double current_ms);
    void draw(float y);

    bool is_finished() const;
};
