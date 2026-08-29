#pragma once

#include "file_navigator/navigator.h"

class SongSelectScript;

class DiffSortSelect {
private:
    int selected_box;
    int selected_level;
    bool in_level_select;
    bool confirmation;
    int confirm_index;
    int num_boxes;
    int prev_diff;
    int prev_level;

    std::vector<int> limits;

    Statistics statistics;
    std::map<int, std::array<int, 3>> diff_sort_sum_stat;

    TextureResizeAnimation* bg_resize;
    FadeAnimation* diff_fade_in;
    FadeAnimation* box_flicker;
    MoveAnimation* bounce_up_1;
    MoveAnimation* bounce_down_1;
    MoveAnimation* bounce_up_2;
    MoveAnimation* bounce_down_2;
    FadeAnimation* blue_arrow_fade;
    MoveAnimation* blue_arrow_move;

    void draw_statistics();
    void draw_diff_select();
    void draw_level_select();

    // ---- ROUND 15: the arcade むずかしさからえらぶ window ------------------
    // 39.06 `common_song_select/song_select_select_sort_window.lua` +
    // `common_songselect/common_song_select_sort_window.nulm`.  The cabinet does
    // NOT show a row of course boxes and then a star page; it shows ONE window
    // with three rows - むずかしさ / ★の数 / 表示順 - that are decided in turn:
    // kat moves the value of the current row, don commits it and drops the
    // cursor to the next row, and after the third the window waits 2 s and
    // closes.  This mode is only taken when the skin ships
    // `SongSelect:draw_sort_window`; every other skin keeps the old UI above,
    // untouched.
    bool arcade = false;
    SongSelectScript* script = nullptr;
    int session = 0;                     // 0 = diff, 1 = star, 2 = order
    int sort_param[3] = {1, 1, 1};       // 1-based, exactly the arcade's sortParam
    int diff_count = 5;                  // easy..oni-ura (ReSettingOpenOniSelectLabel)
    int star_min = 1, star_max = 10;     // paramDiffStarMin / Max
    int order_count = 4;
    int song_num = 0;                    // PlayDataManager.GetDiffcultySongNum
    double t_open = 0.0;                 // StartSortWindow  (also starts kStartWait)
    double t_end  = 0.0;                 // last row decided (0 = not yet) -> kEndWait
    double t_out  = 0.0;                 // CloseSortWindow  -> label "out"
    double now_ms = 0.0;
    double arrow_t0[3] = {0.0, 0.0, 0.0};
    int    arrow_dir[3] = {0, 0, 0};     // +1 = right arrow bounced, -1 = left
    bool   finished = false;
    std::optional<std::array<int, 3>> arcade_result;

    void arcade_refresh_song_num();
    bool arcade_input_locked() const;
    void arcade_change_param(int move);

public:
    DiffSortSelect(Statistics statistics, int prev_diff, int prev_level,
                   SongSelectScript* script = nullptr, int prev_order = 1);

    void update(double current_ms);
    std::optional<std::pair<int, int>> input_select();
    void input_left();
    void input_right();
    void draw();

    // ---- arcade window, read by the scene and by the skin -----------------
    bool is_arcade() const { return arcade; }
    // Set once the third row has been decided AND the close fade has run out.
    std::optional<std::array<int, 3>> take_result();

    int  lua_session() const     { return session; }         // 0..2 (3 = all decided)
    int  lua_diff() const        { return sort_param[0]; }   // 1..5 (5 = oni-ura)
    int  lua_star() const        { return sort_param[1]; }   // 1..10
    int  lua_order() const       { return sort_param[2]; }   // 1..4
    int  lua_diff_count() const  { return diff_count; }
    int  lua_order_count() const { return order_count; }
    int  lua_song_num() const    { return song_num; }
    // 0 = fading in, 1 = open, 2 = the 2 s hold after the last row, 3 = fading out
    int   lua_phase() const;
    float lua_alpha() const;                  // the root MC's own cmul alpha
    // Per-row `select` bounce: local x offset of that row's arrows, 0 when idle.
    float lua_arrow_offset(int row) const;
};
