#pragma once

#include "../../libs/global_data.h"
#include "../../libs/animation.h"
#include "../../libs/script.h"

class ResultTransition : public LuaScript {
private:
    PlayerNum player_num;
    MoveAnimation* move;

    sol::protected_function fn_start, fn_update, fn_draw, fn_is_finished;

    void draw_default();

    // Resolved once in the constructor (player_num is fixed for this object's
    // lifetime) instead of doing string-keyed texture lookups every frame.
    bool has_footer = false;
    float tex_height = 0.0f;
    float shutter_width = 0.0f;
    TextureObject* t_shutter_1p = nullptr;
    TextureObject* t_shutter_2p = nullptr;
    TextureObject* t_footer_1p = nullptr;
    TextureObject* t_footer_2p = nullptr;
    TextureObject* t_shutter_player = nullptr;
    TextureObject* t_footer_player = nullptr;
    void init_textures();

public:
    bool is_finished;
    bool is_started;

    ResultTransition() = default;

    ResultTransition(PlayerNum player_num);

    void start();
    void update(double current_ms);
    void draw();
};
