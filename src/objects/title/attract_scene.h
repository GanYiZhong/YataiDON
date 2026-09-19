#pragma once

#include "../../libs/script.h"

// Everything drawn during TitleState::ATTRACT_CAMERA (the camera background,
// the two bana adverts, the cloud) -- one lua object owning the others.
class AttractScene : public LuaScript {
    sol::protected_function fn_update;
    sol::protected_function fn_draw;
    sol::protected_function fn_is_finished;
    sol::protected_function fn_close;
public:
    AttractScene();
    ~AttractScene();
    void update(double current_ms);
    void draw();
    bool is_finished();
};
