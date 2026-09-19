#include "game_over.h"

GameOverSequence::GameOverSequence() {
    if (!load("GameOver", "game_over")) return;
    fn_update      = lua_object["update"];
    fn_draw        = lua_object["draw"];
    fn_is_finished = lua_object["is_finished"];
}

void GameOverSequence::update(double current_ms) {
    call(fn_update, "GameOver:update", current_ms);
}

void GameOverSequence::draw() {
    call(fn_draw, "GameOver:draw");
}

bool GameOverSequence::is_finished() {
    return call_r<bool>(fn_is_finished, "GameOver:is_finished").value_or(true);
}
