#pragma once

#include "../../libs/animation.h"

class DanTransition {
private:
    MoveAnimation* slide_in;
    bool started;
    double start_ms = 0;
    double last_ms  = 0;
public:
    DanTransition();
    void start();
    void update(double current_ms);
    void draw();
    bool is_started();
    bool is_finished();

    // 0..1 over the transition's own animation duration, clamped, so a skin can
    // draw its own rig on the engine's clock instead of latching a private one.
    //
    // This exists because `ScriptManager` owns a TextureWrapper of its own
    // (src/libs/script.h) that is a different object from the engine-global
    // `tex`: `tex.get_animation(38, ...)` from Lua therefore returns a COPY
    // whose is_started/attribute this class never touches, and the run state of
    // the animation the engine is actually driving was invisible from Lua
    // (LUA_CAPABILITIES C-dan-2). Handing the number out is the narrow fix.
    double progress() const;
    double duration() const;
};
