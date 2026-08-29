#pragma once

#include "../../libs/script.h"

class Indicator : public LuaScript {
public:
    enum class State {
        SKIP = 0,
        SIDE = 1,
        SELECT = 2,
        WAIT = 3
    };

private:
    sol::protected_function fn_update;
    sol::protected_function fn_draw;
    sol::protected_function fn_draw_top;

public:
    Indicator(State state);
    void update(double current_ms);
    void draw(float x, float y, float fade = 1.0f);

    // ROUND 17 -- the skin's TOP-MOST slot on DAN_SELECT.
    //
    // SONG_SELECT got such a slot in round 14 (`SongSelect:draw_top`), which is
    // what lets the 段位道場 shutter rig live in Lua and still land over the whole
    // HUD.  DAN_SELECT had none: its last skin-owned draw is `Indicator::draw`,
    // and the coin overlay, the GLOBAL::DAN_SELECT plate and the allnet chip all
    // draw after it.  The shutter has to CONTINUE across the SONG_SELECT ->
    // DAN_SELECT hand-off (the cabinet's doors open ON the dan-select screen,
    // `loading_dani_intro` f187..200), so it needs a slot above that chrome.
    //
    // `Indicator` is used as the carrier because it is the one Lua-backed global
    // object DAN_SELECT already builds; a screen script of its own would be a new
    // file and a new registration for one call.  Only DanSelectScreen::draw()
    // calls this, and `LuaScript::call` no-ops on an invalid function, so a skin
    // without `Indicator:draw_top` is unaffected.
    void draw_top();
};
