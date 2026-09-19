#pragma once

#include "../../libs/animation.h"

struct TextureObject;

class DanTransition {
private:
    MoveAnimation* slide_in;
    bool started;
    double start_ms = 0;
    double last_ms  = 0;
    TextureObject* t_background = nullptr;
public:
    DanTransition();
    void start();
    void update(double current_ms);
    void draw();
    bool is_started();
    bool is_finished();

    double progress() const;
    double duration() const;
};
