#include "drumroll_counter.h"
#include "../../libs/texture.h"

DrumrollCounter::DrumrollCounter() {
     drumroll_count = 0;
     fade = (FadeAnimation*)tex.get_animation(8);
     stretch = (TextStretchAnimation*)tex.get_animation(9);
}

void DrumrollCounter::update_count(int count) {
    if (drumroll_count != count) {
        drumroll_count = count;
        fade->start();
        stretch->start();
    }
}

void DrumrollCounter::update(double current_ms, int count) {
    fade->update(current_ms);
    stretch->update(current_ms);

    if (count != 0) update_count(count);
}

void DrumrollCounter::draw(float y) {
}

bool DrumrollCounter::is_finished() const {
    return fade->is_finished;
}
