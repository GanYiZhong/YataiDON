#pragma once

#include "../../libs/script.h"

class BottomCharacters {
public:
    BottomCharacters();
    void start();
    void update(double current_ms, int state);
    bool is_finished();
    void draw();
};
