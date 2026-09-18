#include "result_crown.h"

ResultCrown::ResultCrown(int crown_type, bool is_2p) {
    if (!load("ResultCrown", "result_crown", crown_type, is_2p)) return;
    fn_update    = lua_object["update"];
    fn_draw      = lua_object["draw"];
    fn_is_settled = lua_object["is_settled"];
}

void ResultCrown::update(double current_ms) {
    call(fn_update, "ResultCrown:update", current_ms);
}

void ResultCrown::draw() {
    call(fn_draw, "ResultCrown:draw");
}

bool ResultCrown::is_settled() {
    auto result = call_r<bool>(fn_is_settled, "ResultCrown:is_settled");
    if (!result) {
        // No script / Lua error: never report "still animating", otherwise the
        // result screen would wait on a signal that can never arrive.
        spdlog::warn("ResultCrown:is_settled unavailable, treating crown as settled");
        return true;
    }
    return *result;
}
