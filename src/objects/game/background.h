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
    void draw_back();
    void draw_fore();

    // ROUND 80 (r80-gauge-layering-recheck): per-lane soul-gauge overlay,
    // called from Player::draw() IMMEDIATELY AFTER that lane's Gauge::draw().
    //
    // The cabinet's soul gauge is ONE graphic sitting at a fixed place in the
    // enso graphic stack -- `EnsoGraphicTamashiiGage`, created from
    // `datatable/enso_post.bin` target 0's twelfth row of thirty-three (the
    // factory's `case 9`, EnsoGraphicFactory::CreateEnsoGraphics 0x1400EAF60,
    // which walks the table and appends each graphic to one vector in row
    // order) -- so it is drawn UNDER don_enso, lane_left, taiko, onp_jump,
    // hit_effect, course, option, score, combo_number, action_result,
    // branch_effect, don_fukidashi, renda_number, name_plate, action_fusen,
    // action_kusudama, song_info, skip and papamama. A skin that repaints the
    // gauge from draw_fore() -- the last Lua paint of the whole GAME HUD --
    // puts its half of the gauge ON TOP of all of those instead, which is
    // exactly the user report this round investigated ("the gauge is not the
    // topmost layer").
    //
    // `player_num` is passed so a two-player skin repaints only the lane whose
    // engine gauge just drew; both lanes then land in their own draw slots.
    // Optional on the Lua side: a Background without `draw_gauge` is never
    // called and keeps whatever it does in draw_fore().
    void draw_gauge(PlayerNum player_num);
    bool wants_gauge_draw() const { return fn_draw_gauge.valid(); }
};
