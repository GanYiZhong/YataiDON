#pragma once

#include "../../libs/script.h"
#include <functional>

class Timer {
public:
    Timer(int time, double current_time_ms, std::function<void()> confirm_func);
    void update(double current_ms);
    void draw(float x = 0, float y = 0);
};
