#include "timer.h"
#include "../../libs/global_data.h"

Timer::Timer(int time, double current_time_ms, std::function<void()> confirm_func) {
    bool is_frozen = global_data.config->general.timer_frozen;
    // ROUND 52 (r52-lua-divergence-fixes): automation-only unfreeze so the
    // hidden driver can verify the timer voice schedule on a cabinet config
    // that ships timer_frozen = true (config.toml is never edited). Same
    // env-gate pattern as YATAIDON_R33_GLSTATE; no effect unless set.
    if (std::getenv("YATAIDON_R52_TIMER_RUN")) is_frozen = false;
    if (!load("Timer", "timer", time, current_time_ms, confirm_func, is_frozen)) return;
    fn_update = lua_object["update"];
    fn_draw   = lua_object["draw"];
}

void Timer::update(double current_ms) { call(fn_update, "Timer:update", current_ms); }
void Timer::draw(float x, float y)    { call(fn_draw,   "Timer:draw", x, y); }
