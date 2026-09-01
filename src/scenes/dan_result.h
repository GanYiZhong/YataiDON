#pragma once

#include "../libs/screen.h"
#include "../libs/global_data.h"
#include "../libs/text.h"
#include "../objects/result/background.h"
#include "../objects/game/gauge.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"
#include "../objects/global/nameplate.h"
#include "../objects/global/chara_3d.h"
#include "../objects/game/exam_caption.h"

class DanResultScreen : public Screen {
public:
    DanResultScreen() : Screen("dan_result") {}

    void on_screen_start() override;
    Screens on_screen_end(Screens next_screen) override;
    std::optional<Screens> update() override;
    void draw() override;

private:
    AllNetIcon allnet_indicator;
    CoinOverlay coin_overlay;
    FadeAnimation* fade_out   = nullptr;
    FadeAnimation* page2_fade = nullptr;

    std::optional<ResultBackground> background;
    std::unique_ptr<Gauge>        gauge;
    std::unique_ptr<OutlinedText>    hori_name;
    std::vector<std::unique_ptr<OutlinedText>> song_names;

    // ROUND 70 -- see game_dan.h. One cache for the whole-run `tx_border`
    // (size 36), the per-song one (size 20) and the tamashii norma marker
    // (size 25); the key includes the size so all three share it safely.
    ExamCaptionCache exam_captions;

    // ROUND 16 (r16-dan): the player Don and the nameplate. `dani_result.nulm`
    // MC 437 carries `don_1p` (char 305, depth 17) and `plate_1p_instance`
    // (char 306, depth 18) at alpha 1 from frame 0 to frame 434 -- i.e. on BOTH
    // pages, never hidden. We drew neither.
    Nameplate nameplate;
    std::unique_ptr<Chara3D> chara;

    bool is_page2 = false;
    // ms since the CURRENT page opened. The cabinet gates input and advances on
    // this clock, so it is the whole reason DAN_RESULT is no longer static:
    // DaniResultSongMain.lua kWaitTime_ 0.5s / kWaitEndTime_ 30s, and the same
    // pair in DaniResultTotalMain.lua.
    double page_start_ms = 0.0;
    // ── ROUND 72 (r72-danresult-icon-and-doubleplay) ──────────────────────────
    // Page 1's OWN clock. `page_start_ms` is the *current page's* clock and is
    // deliberately rewound to `now` when page 2 opens (handle_input) because the
    // cabinet re-arms kWaitTime_/kWaitEndTime_ per page. draw_page1() was reading
    // that same clock, so the rewind re-armed every song board's `land` gate and
    // the whole 3-row slide-in from the right replayed underneath page 2's
    // 500 ms cross-fade. On the cabinet MC 437 is ONE playhead that never
    // rewinds (song_01 45 / song_02 70 / song_03 95, then `total` at 120), so
    // page 1 animates exactly once. This clock is set once, in on_screen_start.
    double page1_start_ms = 0.0;
    std::vector<bool> page1_armed;   // r72 trace latch, see draw_page1()

    // ── ROUND 50 (r50-dani-visual-completion) ────────────────────────────────
    // Page-2 count-up timeline (DaniResultTotalMain's state machine flattened
    // into a precomputed schedule -- every duration is knowable up front since
    // the values are final when the screen opens). All times are ms on the
    // page-2 clock. Common.FPS = 120 for the Lua timers; the lumen clip frames
    // stay 60 fps -- NEVER conflate the two.
    double totals_start = 0;     // end_total: totals/score/gauge count-up begins
    double totals_end   = 0;     // every totals counter + the gauge has landed
    struct RowSchedule {
        double land  = 0;        // row slides in (detail_in_0N)
        double fill0 = 0;        // bar fill begins
        double filld = 0;        // bar fill duration
        double numin = 0;        // live digits appear (num_in), SE gauge_judgement
    };
    std::vector<RowSchedule> rows;
    double stamp_at = 0;         // stamp_anm starts (StampSE fires)
    double voice_at = 0;         // end_stamp (StampVoice + atmos)
    bool page2_skipped = false;  // a don press mid-count-up -> everything lands
    // one-shot SE latches
    bool se_total_intro = false, se_countup = false, se_gauge_max = false,
         se_stamp = false, se_voice = false;
    std::vector<bool> se_row_fill, se_row_judge;
    int  page1_plates_played = 0;   // partial_plate per landed song board

    // The built-in tamashii gauge row (DaniResultTotalBase). Which exam row is
    // the course's "gauge" exam; -1 = course has none (row column unchanged).
    int    gauge_exam   = -1;
    int    gauge_value  = 0;     // g_tamashiiGaugeValue_ (0..100)
    int    gauge_border = 0;     // g_gaugeBorderValue_  (0..100)

    // Rank persistence + reward flow (DaniResult.lua CheckReward +
    // DaniResultReward). Computed once in on_screen_start.
    bool shodan       = false;   // this run set a new best rank (昇段だドン due)
    int  prev_best    = 0;       // stored rank before this run (0 = never)
    bool celebrating  = false;   // the 昇段だドン overlay is up
    double celebrate_start_ms = 0;
    bool se_advance = false, se_shogo = false;

    // ── ROUND 57 (r57-dani-leftovers) ────────────────────────────────────────
    // Congrats-at-top-course (dani_result_congrats; DaniResult.lua:318-329 +
    // DaniResultReward Congrat state): first pass of the library's highest
    // course pops the full-width congratulation after the 昇段 celebration.
    bool   congrats_due     = false;
    bool   congrats_showing = false;
    double congrats_start_ms = 0;
    bool   se_congrats = false;
    // Best-score bar (DaniResultTotalBase best_score_mc; value =
    // score - stored best, shown only when positive and g_isBestScore_).
    int  prev_best_score = 0;
    bool best_score_show = false;
    int  prev_arrival    = 0;    // stored arrival BEFORE this run (mask key)
    void draw_best_score(double fade, double on_page);
    void draw_congrats(double now);

    void handle_input(double current_ms);
    void build_page2_timeline();
    void update_sounds(double now);
    void apply_reward();
    void draw_page1(double now);
    void draw_page2(double fade, double now);
    // ROUND 65: the exam column draws at NATIVE size — the cabinet's card is
    // 1400 px wide and the row pitch 154 (dani_result.nulm MC 437's own
    // total_detail_0N_mc placements). The 0.8 this defaulted to was never in
    // the 39.06 data.
    void draw_exam_info(double fade, double now, float scale = 1.0f);
    void draw_gauge_row(const Exam& exam, float y, double fade, double now, float scale);
    void draw_celebration(double now);
    // Count-up-aware digit column: rolls ones-first, 0.5 s per digit
    // (DaniResultSongScore.NumCounter), landing on the final value; digits not
    // yet reached stay hidden ("none"). roll_t < 0 = land instantly.
    void draw_digit_counter(const std::string& digits, float margin_x, TexID id,
                             int index, float y, double fade, float scale,
                             float x_off = 0.0f, double roll_t = -1.0);
    void draw_chara_and_plate();
};
