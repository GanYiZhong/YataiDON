#include "warning_screen.h"

WarningScreen::WarningScreen(double current_ms) {
    if (!load("WarningScreen", "warning_screen", current_ms)) return;
    fn_update      = lua_object["update"];
    fn_draw        = lua_object["draw"];
    fn_is_finished = lua_object["is_finished"];
}

void WarningScreen::update(double current_ms) {
    call(fn_update, "WarningScreen:update", current_ms);
}

void WarningScreen::draw() {
    call(fn_draw, "WarningScreen:draw");
}

bool WarningScreen::is_finished() {
    return call_r<bool>(fn_is_finished, "WarningScreen:is_finished").value_or(true);
}
