#include "attract_scene.h"

AttractScene::AttractScene() {
    if (!load("AttractScene", "attract_scene")) return;
    fn_update      = lua_object["update"];
    fn_draw        = lua_object["draw"];
    fn_is_finished = lua_object["is_finished"];
    fn_close       = lua_object["close"];
}

AttractScene::~AttractScene() {
    call(fn_close, "AttractScene:close");
}

void AttractScene::update(double current_ms) {
    call(fn_update, "AttractScene:update", current_ms);
}

void AttractScene::draw() {
    call(fn_draw, "AttractScene:draw");
}

bool AttractScene::is_finished() {
    return call_r<bool>(fn_is_finished, "AttractScene:is_finished").value_or(true);
}
