#include "score_counter.h"
#include "../../libs/texture.h"

ScoreCounter::ScoreCounter(int score, bool is_2p) : score(score), is_2p(is_2p) {
    stretch = (TextStretchAnimation*)tex.get_animation(4, true);
}

void ScoreCounter::update_count(int score) {
    if (score != this->score) {
        this->score = score;
        stretch->start();
    }
}

void ScoreCounter::update(double current_ms) {
    if (score > 0) {
        stretch->update(current_ms);
    }
}

void ScoreCounter::draw(float y) {
}
