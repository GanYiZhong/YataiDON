#pragma once

#include "../../libs/script.h"
#include <functional>

class Timer : public LuaScript {
    sol::protected_function fn_update;
    sol::protected_function fn_draw;
public:
    Timer(int time, double current_time_ms, std::function<void()> confirm_func);
    void update(double current_ms);
    void draw(float x = 0, float y = 0);

    // ROUND 64 (r64-danselect-fidelity) -- the cabinet's `Timer:GetCount()`
    // (common/Timer.lua). DAN_SELECT needs it to reproduce
    // dani_select_all.lua:159-163: when a course is decided, the remaining
    // count is clamped UP to 30 s only `if is_daniTimeUp or GetCount() < 30`.
    // Returns -1 when the Lua object never loaded, so every caller can treat
    // "unknown" as "leave the clock alone".
    int time() const;
};
