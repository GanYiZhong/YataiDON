#include "balloon_counter.h"
#include "../../libs/texture.h"

BalloonCounter::BalloonCounter(int count, bool is_2p)
 : balloon_count(0), balloon_total(count), is_popped(false), is_2p(is_2p) {
     fade = (FadeAnimation*)tex.get_animation(7);
     stretch = (TextStretchAnimation*)tex.get_animation(6);
     fade->reset();
     stretch->reset();
}

void BalloonCounter::update_count(int count) {
    if (balloon_count != count) {
        balloon_count = count;
        fade->start();
        stretch->start();
        if (balloon_count == balloon_total) {
            is_popped = true;
        }
    }
}

void BalloonCounter::update(double current_ms, int count) {
    stretch->update(current_ms);
    if (is_popped) fade->update(current_ms);

    if (count != 0) update_count(count);
}

void BalloonCounter::draw(float y) {
}

bool BalloonCounter::is_finished() const {
    return fade->is_finished;
}
