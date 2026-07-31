#include "score_history.h"
#include "../../../libs/texture.h"

ScoreHistory::ScoreHistory(const std::array<std::optional<Score>, 5>& scores, double current_ms)
    : last_ms(current_ms)
{
    for (int i = 0; i < 5; i++) {
        if (scores[i].has_value())
            available.push_back({i, scores[i].value()});
    }
    if (!available.empty())
        curr_index = 1 % (int)available.size();
}

void ScoreHistory::update(double current_ms) {
    if (available.empty()) return;
    if (current_ms >= last_ms + 1000.0) {
        last_ms = current_ms;
        curr_index = (curr_index + 1) % (int)available.size();
    }
}

void ScoreHistory::draw() {}

void ScoreHistory::draw_long() {}

void ScoreHistory::draw_short() {}
