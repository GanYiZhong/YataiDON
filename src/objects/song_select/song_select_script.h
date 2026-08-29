#pragma once
#include "../../libs/script.h"

class BaseBox;
class SongSelectPlayer;
class Navigator;
class DiffSortSelect;

class SongSelectScript : public LuaScript {
private:
    sol::protected_function fn_update;
    sol::protected_function fn_restart_text_fade;
    sol::protected_function fn_draw_footer;
    sol::protected_function fn_draw_overlays;
    sol::protected_function fn_draw_top;
    sol::protected_function fn_draw_box;
    sol::protected_function fn_draw_box_bg;
    sol::protected_function fn_draw_background;
    sol::protected_function fn_draw_selector;
    sol::protected_function fn_draw_option_panel;
    sol::protected_function fn_draw_sort_window;

    sol::object box_to_lua(BaseBox* box);

public:
    SongSelectScript();
    void update(double current_ms);
    void restart_text_fade();
    void draw_footer();
    void draw_overlays(int state);
    // The LAST skin-owned draw on SONG_SELECT: called from
    // SongSelectScreen::draw() immediately before dan_transition->draw(), i.e.
    // over the song counter, the select timer, the allnet chip, the coin
    // overlay and the control-guide indicator. Before this existed a skin that
    // needed a full-screen overlay had to smuggle it into the tail of
    // Indicator:draw (LUA_CAPABILITIES C-dan-3).
    //
    // dan_progress is 0..1 while the 段位道場 shutter transition is running and
    // -1 when it is not, so the skin can drive its own rig off the engine's
    // clock (C-dan-2). A skin without SongSelect:draw_top is never called.
    void draw_top(float dan_progress);

    bool draw_box(BaseBox* box);
    bool draw_box_bg(BaseBox* box);
    // True when the skin scripts the difficulty-select backdrop. Lets the 2P
    // screen run the same draw_diff_select_bg() the 1P screen always ran
    // without adding a draw for skins that never scripted it.
    bool has_box_bg() const { return fn_draw_box_bg.valid(); }
    bool draw_background(Navigator* nav);
    // ROUND 15: `pass` 0 = before Navigator::draw() (the parts the cabinet keeps UNDER
    // the difficulty cards: course_frame_N at depth 7/10), 1 = after it (player_cursor_N
    // at depth 21/22, above everything on the panel). Both passes report the same
    // handled flag, so the C++ fallback selector is skipped exactly when the skin owns it.
    bool draw_selector(SongSelectPlayer* player, bool is_half, float fade_in, int pass);
    // The 演奏オプション / 音色 board. Same "the skin owning the method turns the
    // C++ drawing off" contract as draw_background / draw_box: when a skin
    // defines SongSelect:draw_option_panel(player, kind) the engine calls it
    // INSTEAD of ModifierSelector::draw() / NeiroSelector::draw(), in the same
    // draw-order slot, so the skin can repaint the whole board (the changed-value
    // box, the pulsing row cursor, greyed rows) rather than decorate it from
    // draw_overlays, which runs above the C++ value text. kind: 1 = modifier,
    // 2 = neiro. A skin without the method is completely unaffected.
    bool draw_option_panel(SongSelectPlayer* player, int kind);

    // ROUND 15 - the arcade むずかしさからえらぶ window
    // (`common_songselect/common_song_select_sort_window`).  Declaring
    // SongSelect:draw_sort_window(window) switches DiffSortSelect from the
    // PyTaikoGreen course-boxes UI to the cabinet's three-row window AND makes
    // the skin the only thing that draws it - the C++ drawing is not run at
    // all, exactly like draw_option_panel.  A skin without the method keeps the
    // old selector, logic included.
    bool has_sort_window() const { return fn_draw_sort_window.valid(); }
    bool draw_sort_window(DiffSortSelect* window);
};
