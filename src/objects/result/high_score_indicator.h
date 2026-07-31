#pragma once

#include "../../libs/script.h"

class HighScoreIndicator {
public:
    HighScoreIndicator() = default;
    HighScoreIndicator(int old_score, int new_score, bool is_2p);
    void update(double current_ms);
    void draw();
};
