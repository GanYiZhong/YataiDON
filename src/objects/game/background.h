#include "../../libs/global_data.h"
#include <sol/sol.hpp>

class Background {
private:
    sol::table lua_object;
    sol::protected_function fn_update;
    sol::protected_function fn_handle_good;
    sol::protected_function fn_handle_ok;
    sol::protected_function fn_handle_bad;
    sol::protected_function fn_handle_drumroll;
    sol::protected_function fn_handle_balloon;
    sol::protected_function fn_handle_gauge;
    sol::protected_function fn_handle_song_end;
    sol::protected_function fn_handle_dan;
    sol::protected_function fn_handle_skip;
    sol::protected_function fn_handle_ending;
    sol::protected_function fn_draw_ending;
    sol::protected_function fn_draw_back;
    sol::protected_function fn_draw_fore;
    sol::protected_function fn_draw_gauge;

public:
    Background(PlayerNum player_num, float bpm, const std::string& scene_preset);
    ~Background();
    void update(double current_ms, float bpm);
    void handle_good(PlayerNum player_num);
    void handle_ok(PlayerNum player_num);
    void handle_bad(PlayerNum player_num);
    void handle_drumroll(PlayerNum player_num);
    void handle_balloon(PlayerNum player_num);
    void handle_gauge(PlayerNum player_num, float progress, bool is_clear, bool is_rainbow,
                      float clear_progress = 1.0f, float flash = 0.0f);
    void handle_song_end(PlayerNum player_num, int good, int ok, int bad, int total_notes);

    void handle_dan(PlayerNum player_num, const sol::table& state);
    bool wants_dan() const { return fn_handle_dan.valid(); }
    void handle_skip(PlayerNum player_num, const sol::table& state);
    bool wants_skip() const { return fn_handle_skip.valid(); }
    // Ending banner (clear / full combo / donderful / fail): a skin that defines both
    // handle_ending(player_num, kind) and draw_ending(player_num) draws it itself; the
    // engine keeps running its own animation for the sounds but skips its drawing.
    void handle_ending(PlayerNum player_num, const std::string& kind);
    void draw_ending(PlayerNum player_num);
    bool wants_ending() const { return fn_handle_ending.valid() && fn_draw_ending.valid(); }
    void draw_back();
    void draw_fore();

    void draw_gauge(PlayerNum player_num);
    bool wants_gauge_draw() const { return fn_draw_gauge.valid(); }
};
