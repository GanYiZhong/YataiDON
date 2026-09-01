#pragma once

#include "../libs/screen.h"
#include "../objects/song_select/file_navigator/box_dan.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"
#include "../objects/global/indicator.h"
#include "../objects/song_select/modifier.h"
#include "../objects/global/timer.h"
#include "../objects/global/chara_3d.h"
#include "../objects/global/nameplate.h"
#include <sol/sol.hpp>
#include <atomic>
#include <thread>
#include <mutex>

// ── ROUND 95 (r95-danselect-concurrent-intro) ────────────────────────────────
// Everything `load_dan_box()` reads off disk for one course, with NOTHING that
// touches `tex`, `script_manager` or any Navigator member in it. This is the
// hand-off type between the course-scan worker and the main thread: the worker
// fills a vector of these, the main thread turns each one into a `DanBox` (which
// does reach `tex`) at publish time. See dan_select.cpp.
struct DanBoxData {
    fs::path                   json_path;
    std::string                title;
    int                        color = 0;
    int                        rank = -1;
    int                        dan_index = -1;
    bool                       gaiden = false;
    std::vector<DanSongEntry>  songs;
    std::vector<Exam>          exams;
    int                        total_notes = 0;
    // ROUND 95 -- the localised (title, subtitle) of each song, parallel to
    // `songs`. `DanBox::load_text()` used to get these by running a fresh
    // `SongParser` over every chart of the course, ON THE MAIN THREAD, the first
    // frame the box came on screen -- i.e. a THIRD full parse of every chart,
    // landing right on the intro's reveal (measured: ~2 s of frozen render loop,
    // present in the pre-r95 build too). The scan worker already builds a
    // SongParser for each of these songs, so it just keeps the two strings.
    std::vector<std::pair<std::string, std::string>> song_titles;
};

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

    // ── ROUND 95 — the same walk, off the render thread ──────────────────────
    //
    // `init()` above blocks the render loop for as long as the walk takes (a JSON
    // read plus roughly two SongParser runs per chart of every course found), and
    // `DanSelectScreen::on_screen_start` used to call it BEFORE arming the intro
    // movie, which is the reported defect: the dojo loaded first and only then
    // animated. `begin_init()` starts the same walk on a worker; `poll_init()` is
    // called once a frame from the main thread and does the part that must not
    // leave it -- building the `DanBox` objects (they reach `tex`), sorting,
    // laying the ribbon out. `boxes` is not touched at all until that publish, so
    // draw()/update() can never see a half-built strip.
    //
    // `init()` is kept and is now begin+wait+publish, so every other caller (and
    // YATAIDON_R95_LEGACY) behaves exactly as before.
    void begin_init(const std::vector<fs::path>& song_paths);
    // true once the ribbon is live. Publishes on the frame the worker's result
    // arrives; safe (and cheap) to call every frame, before and after.
    bool poll_init();
    bool init_running() const { return scan_thread.joinable() && !scan_done.load(); }
    // Stop and join the worker. Called from DanSelectScreen::on_screen_end and
    // from ~DanNavigator, so leaving the screen (or the process) can never leave
    // it running.
    void abort_init();
    ~DanNavigator();
    // ROUND 66 (r66-danselect-empty-after-course): one root's worth of the dan
    // walk. Refuses (and logs) an empty or non-directory root instead of letting
    // it throw, and is re-used by init()'s song-library fallback. Returns the
    // number of courses appended to `boxes`.
    int  scan_root(const fs::path& root_path);
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

    // ROUND 95 -- `titles_out`, when non-null, receives the localised
    // (title, subtitle) read off the SongParser this function already builds, so
    // DanBox::load_text() does not have to parse the chart all over again on the
    // main thread. Passing nullptr keeps the pre-r95 signature's behaviour.
    std::optional<DanSongEntry> load_song_entry(const rapidjson::Value& chart,
                                                std::pair<std::string, std::string>* titles_out = nullptr);

    // ROUND 95 — `load_dan_box` split in two. `load_dan_box_data` is the whole of
    // the old body except the final `std::make_unique<DanBox>`; it is
    // WORKER-SAFE (filesystem + rapidjson + SongParser + read-only globals only)
    // and is what the scan thread calls. `load_dan_box` is now that plus the
    // construction, so the synchronous path is unchanged.
    std::optional<DanBoxData> load_dan_box_data(const fs::path& json_path);
    std::unique_ptr<DanBox>   load_dan_box(const fs::path& json_path);
    static std::unique_ptr<DanBox> make_box(const DanBoxData& d);
    // The worker body: `scan_root` + ROUND 66's library fallback, producing data
    // rather than boxes. Static-shaped (no member writes) apart from `scan_abort`.
    std::vector<DanBoxData> scan_all_data(const std::vector<fs::path>& song_paths);
    int scan_root_data(const fs::path& root_path, std::vector<DanBoxData>& out);
    // Turn a finished data vector into the live ribbon. Main thread only.
    void publish(std::vector<DanBoxData>&& data);

    std::thread              scan_thread;
    std::atomic<bool>        scan_done{false};
    std::atomic<bool>        scan_abort{false};
    bool                     scan_published = false;
    std::mutex               scan_mutex;
    std::vector<DanBoxData>  scan_result;   // guarded by scan_mutex
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

    // ROUND 73 (QA defect 4) -- the 1P Don and nameplate. Both objects already
    // existed; this screen simply never drew them (the same "built, never drawn
    // on THIS screen" gate ROUND 71 found on GAME_DAN's skip popup).
    // `dani_select.nulm` sprite 315 (`main`) carries them itself, at root depth
    // 7 `don_1p` and depth 8 `plate_1p_instance` -- above the board and the two
    // task-name plates, below `window_instance` (the confirmation dialog).
    std::unique_ptr<Chara3D> chara;
    Nameplate nameplate;

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

    // ── ROUND 64 (r64-danselect-fidelity) — the credit clock ────────────────
    //
    // The report: 「timer要出現」. DAN_SELECT drew no countdown at all; the
    // cabinet's is the same 100 s select clock SONG_SELECT already has.
    // `dani_select_all.lua` is explicit about every parameter:
    //   SetUp          : timer_:Setup(9); SoundSetting(true,true);
    //                    SetVisible(true); SetShadow(false); SetCount(100)
    //   DaniSelectMain : timer_:StartCount()  -- only once the introduction
    //                    movie has finished (IntroductionMain -> DaniSelectMain)
    //   decide         : if is_daniTimeUp or GetCount() < 30 then
    //                        SetCount(30); StartCount()
    //   IsZero on the wheel   -> TimeUpBegin + se_common_v12a/don_big, i.e. the
    //                            current course is decided for the player
    //   IsZero on the dialog  -> ConfirmationMain's `Timeup` state walks
    //                            selectIndex to 2 (挑戦する) and decides it
    //   confirmation end      -> timer_:StopCount()
    //
    // The clock is CONSTRUCTED at the moment counting starts rather than at
    // screen start, because Timer.lua anchors `last_time` in its constructor:
    // building it early and not ticking it through the entry animation would
    // make the first tick after the gap eat a second. `timer_started` is the
    // engine's DaniSelectMain entry.
    std::unique_ptr<Timer> select_timer;
    bool   timer_started   = false;
    bool   timer_fired     = false;   // one time-up per state, as IsZero is level-triggered
    double screen_start_ms = 0;
    // ROUND 86 — how long IntroductionMain actually runs on THIS entry, in ms. The screen
    // is now reachable two ways and the two play different legs of the same clip, so the
    // DaniSelectMain hand-off can no longer be one constant; see dan_select.cpp.
    double intro_ms        = 0;

    // ── ROUND 95 (r95-danselect-concurrent-intro) ───────────────────────────
    //
    // 「你要邊放動畫邊讓他同時載入dan」. The scan now runs on a worker started at
    // the TOP of on_screen_start and the movie is armed immediately, so the two
    // overlap. The rendezvous, which both the credit clock here and the skin's
    // clip clock derive from the same three numbers:
    //
    //   cover_ms     = intro_ms - DAN_INTRO_MS   (2950.0 from ENTRY, 0.0 from
    //                  SONG_SELECT) -- the part of the clip that shows the player
    //                  nothing, f5..f182 / nothing.
    //   reveal_start = max(screen_start_ms + cover_ms, scan_ready_ms)
    //   intro_end    = reveal_start + DAN_INTRO_MS
    //
    // i.e. scan-finishes-first plays the movie out in full at 1:1 (it is NOT cut
    // short -- the cabinet's Preparing() does not shorten IntroductionMain when
    // its data table happens to be quick), and animation-finishes-first holds the
    // clip clamped on f182 (doors shut, scrim up) until the ribbon exists, then
    // re-anchors on f182 and plays the 1333.3 ms reveal. See ROUND 85's identical
    // clamp-and-re-anchor on the genre board (f113 / f114).
    //
    // FAIL-SOFT: a scan that has not landed within SCAN_TIMEOUT_MS stops holding
    // the reveal. The doors open on an empty ribbon -- which is escapable, the
    // back key works and handle_input_browsing() already returns early on
    // boxes.empty() -- rather than soft-locking the player behind the scrim. The
    // worker's later publish just fills the ribbon in.
    static constexpr double SCAN_TIMEOUT_MS = 20000.0;
    bool   scan_ready      = false;   // the ribbon exists (or the timeout fired)
    double scan_ready_ms   = 0;       // when that happened
    double scan_begin_ms   = 0;       // for the trace, and for the timeout
    bool   legacy_blocking = false;   // YATAIDON_R95_LEGACY=1 -> pre-r95 order
    bool   trace_timeline  = false;   // YATAIDON_R95_TRACE=1
    bool   traced_end      = false;
    // The engine half of the clamp: written EVERY frame, read by
    // Scripts/song_select/dan_doors.lua M.draw_open().
    void   publish_scan_state(double current_ms);

    // Returns GAME_DAN when the confirmation dialog's own time-up decided
    // 挑戦する; std::nullopt otherwise.
    std::optional<Screens> tick_timer(double current_ms);
    void open_confirm(double current_ms);

    void handle_input_browsing(double current_ms);
    // Returns a screen when the dialog decided to leave (挑戦する -> GAME_DAN).
    std::optional<Screens> handle_input_selected();

    void draw_confirm_overlay();
};
