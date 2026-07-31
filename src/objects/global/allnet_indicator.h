#pragma once

#include "../../libs/script.h"

class AllNetIcon {
public:
    AllNetIcon() = default;
    bool online = false;
    void update(double current_ms);
    void draw(float x = 0, float y = 0);
};
