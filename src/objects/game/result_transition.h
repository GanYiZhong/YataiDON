#pragma once

#include "../../libs/global_data.h"
#include "../../libs/animation.h"
#include "../../libs/script.h"

// The GAME -> RESULT transition.
//
// The built-in rig is a pair of shutters that slide in from the top and the bottom
// (global animation 5, a `move`), which is what every skin got before round 13.
//
// ROUND 13: optionally scripted, exactly like `Transition` / `SongTransition` above it.
// A skin that ships `Scripts/.../result_transition.lua` defining `ResultTransition` owns
// the transition completely -- `draw` replaces `draw_default()`, and `is_finished`
// replaces the shutter animation's own end, because the cabinet's transition
// (`loading/loading_kuro_result.nulm`: a full-stage black plate whose ALPHA ramps
// linearly over 109 frames) is both a different curve and nearly twice as long as the
// 983 ms shutter, and a `move` animation cannot express an alpha ramp at all.
//
// A skin without the script -- PyTaikoGreen included -- behaves exactly as before:
// `LuaScript::load` returns false when the skin ships no such script, every `fn_*`
// stays invalid, and `draw()` falls through to `draw_default()`.
class ResultTransition : public LuaScript {
private:
    PlayerNum player_num;
    MoveAnimation* move;

    sol::protected_function fn_start, fn_update, fn_draw, fn_is_finished;

    void draw_default();

public:
    bool is_finished;
    bool is_started;

    ResultTransition() = default;

    ResultTransition(PlayerNum player_num);

    void start();
    void update(double current_ms);
    void draw();
};
