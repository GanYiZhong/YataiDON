#include "result_transition.h"
#include "../../libs/texture.h"

ResultTransition::ResultTransition(PlayerNum player_num)
    : player_num(player_num), is_finished(false), is_started(false) {

    move = (MoveAnimation*)global_tex.get_animation(5);
    move->reset();
}

void ResultTransition::start() {
    move->start();
}

void ResultTransition::update(double current_ms) {
    move->update(current_ms);
    is_started = move->is_started;
    is_finished = move->is_finished;
}

void ResultTransition::draw() {
}
