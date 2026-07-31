#pragma once

#include "../../libs/script.h"

class Indicator {
public:
    enum class State {
        SKIP = 0,
        SIDE = 1,
        SELECT = 2,
        WAIT = 3
    };

    Indicator(State state);
    void update(double current_ms);
    void draw(float x, float y, float fade = 1.0f);
};
