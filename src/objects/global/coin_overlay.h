#pragma once

#include "../../libs/script.h"

class CoinOverlay {
public:
    CoinOverlay() = default;
    void update(double current_ms);
    void draw(float x = 0, float y = 0);
};
