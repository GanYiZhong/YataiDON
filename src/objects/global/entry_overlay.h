#pragma once

#include "../../libs/script.h"

class EntryOverlay {
public:
    EntryOverlay() = default;
    bool online = false;
    void update(double current_ms);
    void draw(float x = 0, float y = 0);
};
