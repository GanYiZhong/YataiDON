#pragma once

#include <array>
#include "game.h"
#include "../objects/game/dan_between.h"
#include "../objects/game/exam_caption.h"

struct DanExamInfo {
    float   progress     = 0;
    float   bar_width    = 0;
    int     counter_value = 0;
    int     red_value    = 0;
    std::string bar_texture;
    // ROUND 67 -- the cabinet's own colour state (dan_bar_state()); the legacy
    // three-bucket `bar_texture` stays as the Lua `handle_dan` API's `bar`.
    std::string bar_state = "empty";
    std::string song_state[3] = {"empty", "empty", "empty"};
    std::string exam_type;
    std::string exam_range;
    // ROUND 19 -- the cabinet's two row shapes (Exam::gothrough).
    bool    gothrough    = true;
    int     song_count   = 0;               // songs finished BEFORE the current one
    int     song_value[3]    = {0, 0, 0};   // that song's own count
    float   song_progress[3] = {0, 0, 0};
};

struct DanInfoCache {
    int remaining_notes = 0;
    std::vector<DanExamInfo> exam_data;
};

class DanGameScreen : public GameScreen {
public:
    DanGameScreen() : GameScreen("game") {}

    void on_screen_start() override;
    Screens on_screen_end(Screens next_screen) override;
    std::optional<Screens> update() override;
    void draw() override;

private:
    int song_index = 0;
    int total_notes = 0;
    int dan_color = 0;

    Gauge dan_gauge{GaugeMode::DAN, PlayerNum::P1, 1};  // initialized properly in init_dan()

    std::vector<bool> exam_failed;
    // ROUND 87 CORRECTS ROUND 47. R47's claim here -- "the cabinet interrupts
    // the whole course the moment an exam becomes unpassable" -- is WRONG, and
    // was the user's 「條件達成失敗他就直接退出了」 bug. It inferred a mid-song
    // early-out from DaniResult.lua's g_unreach_ rows; those rows are a
    // CONSEQUENCE of a SONG-BOUNDARY termination, not evidence of a mid-song
    // one. App::DojoEnsoGameManager::CheckEnsoEnd
    // (D:\tlb_test_harness\decompiled\src\DojoEnsoGameManager.obj.c:1473)
    // writes the course end-state `ed+112` in exactly four places, and the
    // block that consults the norma results (:1580-1598) is entirely inside
    // `if (*(_BYTE*)(ed + 4332))` -- a flag raised ONLY at :1622-1627 after
    // TaikoCorePlayer::IsAllOnpuEnd() has held for 10 consecutive frames, and
    // cleared on any frame it is false. The verdict is therefore read at the
    // song's NATURAL END and nowhere else: final song -> state 1 (ordinary
    // completion, norma not even consulted, :1583-1585); non-final + failed
    // norma -> state 6, course ends WITHOUT advancing (:1594); non-final + ok
    // -> state 5, advance (:1596). The one genuinely mid-song cut is state 4,
    // the 演奏スキップ (:1660-1663). A failed exam therefore lets the song play
    // out, exactly as the user reported it must.
    // The `g_maxReachNum_ ~= 3 -> every exam fails` rule still holds, because
    // state 6 does leave the remaining songs unreached.
    // failed_out is now only entered at a song boundary (state 6) or by a skip
    // (state 4): it freezes the run -- the Player stops updating, the music
    // stops, and the result ribbon starts after FAILOUT_RESULT_DELAY.
    bool   failed_out    = false;
    double failed_out_at = 0.0;
    // ROUND 32 (r32-audit-gamedan): which song(s) of a NON-gothrough exam have
    // already been individually judged to have missed their own quota -- see
    // check_exam_failures(). One bool per exam per song; reset in init_dan().
    std::vector<std::array<bool, 3>> exam_song_failed;
    std::optional<DanInfoCache> dan_info_cache;

    //DanTransition dan_transition;

    // ROUND 69 (r69-dan-song-transition) -- the cabinet's between-song fusuma +
    // next-song-name reveal (App::DojoEnsoGraphicFusuma / dani_enso_between.nulm).
    // Armed by change_song(); its `open` label is what releases start_song().
    DanBetween between;

    std::unique_ptr<OutlinedText> hori_name;

    // ROUND 70 -- the condition threshold captions ("<N> 以上" / "<N> ％"), one
    // proportional text run each, cached because the strings only change when
    // an exam does. Cleared in on_screen_end() BEFORE the font manager goes.
    ExamCaptionCache exam_captions;

    // Cumulative stat tracking across songs
    int prev_good = 0, prev_ok = 0, prev_bad = 0, prev_drumroll = 0;
    int prev_score = 0;
    // ROUND 19 -- per-song totals, needed by the cabinet's per-song ("01") row.
    // Filled as each song ends; the current song is always computed live.
    struct SongStats { int good = 0, ok = 0, bad = 0, drumroll = 0, score = 0, max_combo = 0; };
    std::vector<SongStats> song_stats;
    // ROUND 50 (r50-dani-visual-completion): the cabinet's g_maxComboNum_[j] --
    // the highest combo value observed DURING the current song (a combo carried
    // in across the song boundary counts, exactly as a never-broken combo does
    // on the cabinet's per-song counters). Player::max_combo is run-wide and
    // cannot be decomposed after the fact (ROUND 19/47 stated caveat, now
    // closed); this is polled every frame in update() instead.
    int song_max_combo = 0;
    std::string current_song_title;

    void init_dan();
    void change_song();

    // ROUND 20 (r19-danskip): 演奏スキップ for 段位道場. The counting rule
    // (rim-only, alternating, no timeout, unused-drum lane, 2P lockout) is
    // shared verbatim with GameScreen -- init_skip()/push_skip_state()/
    // draw_skip() are reused unmodified via inheritance (game.h, all public,
    // no dan-specific state needed). Only the "10th hit" action needs a
    // dan-aware body: ROUND 68 corrected what that action IS -- a dan skip
    // ends the WHOLE COURSE at once and lands on DAN_RESULT (the cabinet's
    // enso state 4 is not in DojoEnsoGameManager's advance-to-next-song
    // branch; see the ROUND 68 block in game_dan.cpp) -- and it must still
    // recount THIS song's bad notes against a multi-song cumulative Player
    // (see Player::cut_to_end's baseline params). Because
    // GameScreen::poll_skip()'s own tenth-hit line calls
    // `do_skip()` unqualified and non-virtually, it can never reach a
    // same-named override here, so the rim-counting body is duplicated
    // (poll_skip_dan) purely to redirect that one call -- everything above
    // the tenth-hit branch is byte-identical to GameScreen::poll_skip().
    void update_skip_dan();
    void poll_skip_dan();
    void do_skip_dan();

    DanInfoCache calculate_dan_info();
    int get_exam_progress(const Exam& exam);
    // ROUND 19: the same quantity restricted to one song of the course.
    int get_exam_progress_song(const Exam& exam, int song_idx);
    void draw_exam_row(const DanExamInfo& info, const Exam& exam, int index, float y);
    void fill_unplayed_songs();
    // ROUND 32 (r32-audit-gamedan): `song_finished` marks the boundary of the
    // CURRENT song (`song_index`, before it is advanced) -- distinct from
    // `course_finished`, which marks the boundary of the whole run. Needed
    // because a non-gothrough ("01") exam's "more" (以上) range must be judged
    // against that one song's own count at THAT song's own end, not only at
    // the end of the whole course.
    void check_exam_failures(bool course_finished = false, bool song_finished = false);

    // ROUND 47 -- arcade tier arithmetic (DaniResult.lua CheckResultDetail):
    // 0 fail / 1 red / 2 gold for one measured value against an exam's two
    // borders ("more": >= red, gold at >= gold; "less": < red, gold at < gold;
    // a course with no gold border authored -- gold == 0 or gold == red -- can
    // only ever be red, never spuriously gold).
    static int exam_tier(const Exam& exam, int value);
    // The whole-course verdict 0..6 (SetOdaiResult) + per-exam tiers, written
    // into sd.dan_result_data. Factored out of update() so the natural course
    // end and the ROUND 47 fail-out path fill the result identically.
    void save_result_data(bool all_failed);
    void trigger_fail_out(double current_ms);

    // Per-screen override of the shared skin_config dan_exam_info (row pitch +
    // bar length); falls back to the shared key when the skin declares nothing.
    static const SkinInfo& dan_exam_info();
    void push_dan_state();
    void draw_dan_info();
    void draw_digit_counter(const std::string& digits, float margin_x, TexID tex_id, int index, float y, float x_offset = 0);
};
