#pragma once

#include "../global/nameplate.h"
#include "../global/chara_3d.h"
#include "neiro.h"
#include "modifier.h"
#include "ura_switch.h"
#include "diff_sort.h"

class SongSelectScript;

class SongSelectPlayer {
public:
    PlayerNum player_num;
    PlayerData player_data;
    Difficulty selected_difficulty;
    Difficulty prev_diff;
    std::vector<Difficulty> curr_diffs;
    double last_moved;
    bool selected_song;
    bool is_ready;
    bool is_ura;
    int ura_toggle;
    bool diff_select_move_right;
    std::string search_string;
    // Set when a don key opens Song Search: typed characters are dropped until that key
    // (and any other don key) is released, so the opening keystroke is not typed.
    bool search_guard = false;
    double search_guard_until_ms = 0.0;

    std::optional<NeiroSelector> neiro_selector;
    std::optional<ModifierSelector> modifier_selector;
    std::optional<UraSwitchAnimation> ura_switch;

    MoveAnimation* diff_selector_move_1;
    MoveAnimation* diff_selector_move_2;
    FadeAnimation* text_fade_in;
    MoveAnimation* selected_diff_bounce;
    FadeAnimation* selected_diff_fadein;
    FadeAnimation* selected_diff_highlight_fade;
    TextureResizeAnimation* selected_diff_text_resize;
    FadeAnimation* selected_diff_text_fadein;

    std::unique_ptr<Chara3D> chara;
    Nameplate nameplate;

    SongSelectScript* script = nullptr;
    bool selector_handled_by_lua = false;
    // set each frame from draw_option_panel: the skin drew the option panel itself, so
    // the character stays put instead of riding the panel (the cabinet's Don does not move)
    bool option_panel_by_lua = false;

    SongSelectPlayer(PlayerNum player_num);

    void update(double current_time);
    bool is_voice_playing();
    // true from the moment a course is confirmed (start voice playing or done) until
    // reset_selection(); the skin swaps the course mark to its decided look on it
    bool difficulty_decided() const { return selected_difficulty >= Difficulty::EASY && (voice_played || is_ready); }

    SongSelectState select_song();
    void sync_ura(bool ura);
    void init_diff_cursor();
    void reset_selection();
    SongSelectState handle_input_browsing(double current_ms);
    SongSelectState handle_input_selecting();
    std::optional<std::pair<int,int>> handle_input_diff_sort(DiffSortSelect* diff_sort_selector);
    std::optional<std::string> handle_input_search();

    void draw_selector(bool is_half, float fade_in);
    void try_lua_selector(bool is_half, float fade_in, int pass);
    void draw_background_diffs(SongSelectState state);
    void draw(SongSelectState state, bool is_half, float diff_fade_in);

private:
    bool voice_played;
    void navigate_difficulty_left();
    void navigate_difficulty_right();
    void toggle_ura_mode();
    void start_background_diffs();
};
