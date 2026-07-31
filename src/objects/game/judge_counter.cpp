#include "../../libs/texture.h"
#include "judge_counter.h"
#include <math.h>


JudgeCounter::JudgeCounter()
    : good(0), ok(0), bad(0), drumrolls(0) {
    orange = ray::Color{253, 161, 0, 255};
    white = ray::WHITE;
}

void JudgeCounter::update(int good, int ok, int bad, int drumrolls) {
    this->good = good;
    this->ok = ok;
    this->bad = bad;
    this->drumrolls = drumrolls;
}

void JudgeCounter::draw_counter(float counter, float x, float y, float margin, ray::Color color) {
}

void JudgeCounter::draw() {
}
