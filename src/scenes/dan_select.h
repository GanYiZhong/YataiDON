#pragma once

#include "../libs/screen.h"
#include "../objects/song_select/file_navigator/box_dan.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"
#include "../objects/global/indicator.h"
#include "../objects/song_select/modifier.h"
#include <sol/sol.hpp>

class DanNavigator {
public:
    std::vector<std::unique_ptr<DanBox>> boxes;
    int selected_index = 0;

    // ── ROUND 57 (r57-dani-leftovers) — the DAN_SELECT Lua paint surface ────
    // (ROUND 53/54 referral: "moving [the cursor/arrow tables] to Scripts/anim
    // needs a DAN_SELECT Lua paint hook (none exists)"). Mirrors Background's
    // class-instance shape: Scripts/dan_select/dan_select.lua defines
    // `DanSelect` with new() / draw_cursor(x, moved_ms); the C++ keeps owning
    // WHEN (the selected chip position and the ms since the last ribbon step)
    // and the Lua owns WHAT is drawn, sampling the real dani_select.nulm
    // exports (anim/dan_cursor #267, anim/dan_arrow #263) through
    // anim/sampler. FAIL-SOFT: any load/runtime problem drops back to the
    // ROUND 17 inline C++ tables for the rest of the screen's life.
    bool paint_tried = false;
    bool paint_ok    = false;
    sol::table lua_paint;
    sol::protected_function fn_draw_cursor;
    void load_paint_surface();

    // ROUND 17: engine-ms of the last ribbon step. The arcade's two arrow chips
    // are a NON-LOOPING 66-frame flourish that plays on a move and parks
    // invisible, so the draw needs to know when the last one was. Separate from
    // DanSelectScreen::last_moved, which is the double-tap skip window.
    double last_moved = 0;

    void init(const std::vector<fs::path>& song_paths);
    void move_left();
    void move_right();
    void skip(int delta);
    DanBox* get_current();
    void update(double current_ms);
    void draw();

private:
    // The DAN_SELECT ribbon layout. These four numbers used to be compile-time
    // constants, which is why only the selected course and one neighbour each
    // side were ever on screen (LUA_CAPABILITIES item 50). They are still the
    // defaults -- a skin that declares neither key gets exactly the old layout,
    // so PyTaikoGreen is unchanged -- but a skin may now override them through
    // two skin_config entries read by NAME (tex.skin_entry), which means no new
    // SC enum member and no parent-skin edit:
    //
    //   "dan_ribbon"      : { "x": centre,     "width": spacing }
    //   "dan_ribbon_side" : { "x": gap_left,   "y": gap_right   }
    //
    // gap_left / gap_right are the extra space opened up on either side of the
    // selected course so the detail board has room; the arcade's continuous
    // 20-chip ribbon is gap 0 / 0. All values are in PARENT (720p) space and are
    // multiplied by tex.screen_scale, exactly as the constants were.
    static constexpr float BOX_CENTER = 594.0f;
    static constexpr float BASE_SPACING = 150.0f;
    static constexpr float SIDE_OFFSET_L = 200.0f;
    static constexpr float SIDE_OFFSET_R = 500.0f;

    struct RibbonLayout {
        bool  legacy  = true;   // no "dan_ribbon" key -> pre-round-14 formula
        float center  = BOX_CENTER;
        float spacing = BASE_SPACING;
        float side_l  = SIDE_OFFSET_L;
        float side_r  = SIDE_OFFSET_R;
    };
    RibbonLayout ribbon_layout() const;

    void set_positions(bool init, float duration);

    int total_notes_for(const std::vector<DanSongEntry>& songs);

    Exam parse_exam(const rapidjson::Value& e);

    std::optional<DanSongEntry> load_song_entry(const rapidjson::Value& chart);

    std::unique_ptr<DanBox> load_dan_box(const fs::path& json_path);
};

class DanSelectScreen : public Screen {
public:
    DanSelectScreen() : Screen("dan_select") {}

    void on_screen_start() override;
    Screens on_screen_end(Screens next_screen) override;
    std::optional<Screens> update() override;
    void draw() override;

private:
    DanNavigator dan_navigator;
    CoinOverlay coin_overlay;
    AllNetIcon allnet_indicator;
    std::unique_ptr<Indicator> indicator;
    SongSelectState state = SongSelectState::BROWSING;

    // ROUND 17 -- the cabinet's confirmation dialog has THREE entries, not two.
    //
    // `script_lua/dani_select/dani_select_confirmation_main.lua` names them
    // `kselectOption = 1`, `kselectYes = 2`, `kselectNot = 3`, and `MoveCursor`
    // clamps `selectIndex` to [kselectOption, kselectNot] with LEFT = -1 and
    // RIGHT = +1 -- so the on-screen order, left to right, is
    //     [演奏オプション chip]  [挑戦する]  [挑戦しない]
    // which `dani_select_window.nulm` confirms geometrically: at the resting
    // frame (`wait_challenge`, sprite 46 f50) `window/button_option` is at world
    // tx -346, the left button plate `#17@2` at tx -105 and the right plate
    // `#15@1` at tx +233, all at ty +40. The two labels are bound
    //     button_select/text_cursor_left  -> "dani_select_confirmation_decide" (挑戦する)
    //     button_select/text_cursor_right -> "dani_select_confirmation_cancel" (挑戦しない)
    // (`SetConfirmationText`, lines 350-351). Ours had the two swapped.
    //
    // The DEFAULT is `selectIndex = 3` (the table initialiser, line 39), i.e.
    // 挑戦しない -- the RIGHT one, which is also what the cabinet capture shows
    // highlighted. Ours defaulted to the left.
    enum ConfirmEntry { CONFIRM_OPTION = 0, CONFIRM_YES = 1, CONFIRM_NO = 2 };
    int confirm_index = CONFIRM_NO;
    FadeAnimation* confirm_fade = nullptr;

    // ROUND 21 -- `dani_select_confirmation_main.lua`'s state machine is
    // Loading -> StartWait -> Main, and `PlayerInput()` is only ever called
    // from Main. `StartWait` holds for `wait_input_cnt = 0.5 * Common.FPS`
    // (30 frames @ 60 fps = 500 ms) while `mc_main` plays its `in_challenge`
    // entrance clip, so the cabinet's confirmation dialog is fully input-dead
    // for the first 500 ms after it opens -- a don thrown right as the player
    // decides a course cannot also land on the dialog. Ours had no such gate;
    // `handle_input_selected()` processed input from the very first frame.
    double confirm_opened_at = 0;

    // The 演奏オプション the option chip opens. The cabinet's chip is not
    // decoration: `PlayerInput` (line 257) routes a decide on kselectOption into
    // `mc_main:GotoAndPlay("in_option")` + `daniselectoption_:Tween_SlideIn(1)` +
    // the `voice_daniodai_v12e/select_option_c` callout, and the panel itself is
    // `dani_select/dani_select_window_option.nulm` driven by
    // `dani_select_option_main.lua` -- the same option rows the song-select panel
    // has. We reuse the engine's own ModifierSelector rather than build a second
    // one; it mutates the PlayerData it is given, and GameScreen reads the
    // modifiers back out of the DB (`GameScreen::get_player_modifiers`), so the
    // panel is saved on close exactly as song select does it.
    PlayerData dan_player_data;
    std::optional<ModifierSelector> modifier_selector;

    double last_moved = 0;

    // ROUND 32 (r32-audit-songselect) -- `dani_select_dani_main.lua` gates its
    // WHOLE `CheckInput()` (decide AND left/right alike) on `is_inputEnable`
    // (lines 682-728): `MoveCursor()` clears it the instant a move starts
    // (line 280), and it is only ever set back on by a free-running periodic
    // pulse in the `Main` state -- `waitInputCnt()`, `wait_input_cnt = 0.1 *
    // Common.FPS` (line 101) -- that ticks every frame regardless of whether a
    // move happened, so the wall-clock re-enable delay after a move is 0-100 ms
    // (mean 50 ms), NOT a fixed post-move cooldown. wheel_tick_epoch anchors
    // that periodic clock to when the wheel becomes interactive
    // (on_screen_start, the closest equivalent this engine has to the cabinet's
    // DaniSelectMain state entry); wheel_tick_seen is the last 100 ms boundary
    // that cleared wheel_locked.
    bool wheel_locked = false;
    double wheel_tick_epoch = 0;
    long long wheel_tick_seen = -1;

    void handle_input_browsing(double current_ms);
    // Returns a screen when the dialog decided to leave (挑戦する -> GAME_DAN).
    std::optional<Screens> handle_input_selected();

    void draw_confirm_overlay();
};
