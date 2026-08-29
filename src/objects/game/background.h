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

public:
    Background(PlayerNum player_num, float bpm, const std::string& scene_preset);
    ~Background();
    void update(double current_ms, float bpm);
    void handle_good(PlayerNum player_num);
    void handle_ok(PlayerNum player_num);
    void handle_bad(PlayerNum player_num);
    void handle_drumroll(PlayerNum player_num);
    void handle_balloon(PlayerNum player_num);
    // `flash` (ROUND 19, r19-gauge): the gauge-up flash's current fade value
    // (1 -> 0), or 0 when no flash is active this frame -- see Gauge::get_flash_attribute.
    // Optional/additive: a skin that ignores the 6th arg is unaffected.
    void handle_gauge(PlayerNum player_num, float progress, bool is_clear, bool is_rainbow,
                      float clear_progress = 1.0f, float flash = 0.0f);
    // Fired once, when the chart is over and the ending animation is spawned.
    // total_notes is good + ok + bad, i.e. every judged note. Optional on the
    // Lua side: a script without handle_song_end is simply not called.
    void handle_song_end(PlayerNum player_num, int good, int ok, int bad, int total_notes);

    // The 段位道場 exam state, pushed every frame from DanGameScreen::update.
    // Background:draw_fore() is the one skin draw that runs AFTER the whole
    // GAME_DAN HUD, so a skin could always repaint the exam panel -- it just
    // had nothing to repaint it with (LUA_CAPABILITIES item 56 / MAPPING_dan_info
    // C-4). This is that data, and nothing else changes: the engine still draws
    // its own panel in draw_dan_info().
    //
    // The Lua side receives ONE table:
    //   { dan_color=, song_index=, song_count=, remaining_notes=, total_notes=,
    //     gauge=, gauge_max=, exams = { { type=, range=, red=, gold=, value=,
    //                                    progress=, bar=, failed= }, ... } }
    // `value` is the live count already normalised the way the engine's own
    // panel prints it (for range="less" it is the REMAINING allowance, not the
    // used one), `progress` is 0..1, `bar` is "exam_red"/"exam_gold"/"exam_max".
    // Optional on the Lua side: a Background without handle_dan is never called.
    void handle_dan(PlayerNum player_num, const sol::table& state);
    bool wants_dan() const { return fn_handle_dan.valid(); }

    // 演奏スキップ, pushed every frame from GameScreen::update_skip once the
    // feature is armed (skin flag `option_skip_row` + the option on). Same
    // shape as handle_dan: the engine keeps owning the RULE, the skin owns the
    // PICTURE, and a Background without handle_skip is never called -- the
    // engine then falls back to its own one-line skip_counter text.
    //
    // The Lua side receives ONE table:
    //   { count = 0..10, total = 10, remaining = total - count, skipped = bool }
    // `count` is the arcade's alternating-rim counter; the arcade panel shows
    // the REMAINING hits (count 1 -> "9" ... count 10 -> "0"), so a skin that
    // wants the cabinet look draws `remaining`. A drop back to 0 is the arcade
    // reset (a hit on the playing drum) and is the cue to play the panel out.
    void handle_skip(PlayerNum player_num, const sol::table& state);
    bool wants_skip() const { return fn_handle_skip.valid(); }
    void draw_back();
    void draw_fore();
};
