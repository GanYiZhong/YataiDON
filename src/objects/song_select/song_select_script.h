#pragma once
#include "../../libs/script.h"

class BaseBox;
class SongSelectPlayer;
class Navigator;

class SongSelectScript : public LuaScript {
private:
    sol::protected_function fn_update;
    sol::protected_function fn_restart_text_fade;
    sol::protected_function fn_draw_footer;
    sol::protected_function fn_draw_overlays;
    sol::protected_function fn_draw_box;
    sol::protected_function fn_draw_box_bg;
    sol::protected_function fn_draw_background;
    sol::protected_function fn_draw_selector;

    sol::object box_to_lua(BaseBox* box);

public:
    SongSelectScript();
    void update(double current_ms);
    void restart_text_fade();
    void draw_footer();
    void draw_overlays(int state);

    bool draw_box(BaseBox* box);
    bool draw_box_bg(BaseBox* box);
    bool draw_background(Navigator* nav);
    bool draw_selector(SongSelectPlayer* player, bool is_half, float fade_in);
};
