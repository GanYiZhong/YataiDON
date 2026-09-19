#pragma once

#include "../../libs/animation.h"

#include "../../libs/texture.h"

class ScoreCounter {
private:
    int score;
    bool is_2p;
    TextStretchAnimation* stretch;

    TextureObject* t_lane_score_cover = nullptr;
    TextureObject* t_score_number = nullptr;

public:
    ScoreCounter(int score, bool is_2p);
    void update_count(int score);
    void update(double current_ms);
    void draw(float y);

};
