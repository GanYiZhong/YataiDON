#pragma once

#include "../../libs/animation.h"

struct TextureObject;

class BalloonCounter {
private:
    int balloon_count;
    int balloon_total;
    bool is_popped;
    bool is_2p;
    TextStretchAnimation* stretch;
    FadeAnimation* fade;
    TextureObject* t_pop = nullptr;
    TextureObject* t_bubble = nullptr;
    TextureObject* t_counter = nullptr;
public:
    BalloonCounter(int count, bool is_2p);

    void update_count(int count);

    void update(double current_ms, int count);

    void draw(float y);

    bool is_finished() const;

    bool has_popped() const;
};
