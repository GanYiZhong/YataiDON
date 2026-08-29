#pragma once

#include <array>
#include "game.h"

struct DanExamInfo {
    float   progress     = 0;
    float   bar_width    = 0;
    int     counter_value = 0;
    int     red_value    = 0;
    std::string bar_texture;
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
    // ROUND 47 (r47-dani-full-replication) -- the cabinet interrupts the whole
    // course the moment an exam becomes unpassable (a 未満/less cutoff busted
    // mid-song, or a per-song 以上/more quota missed at its own song's end):
    // the remaining songs are never reached (DaniResult.lua's g_unreach_ rows,
    // whose mere existence proves an early-out path feeds this screen) and
    // every exam is judged failed (CheckResultDetail returns 0 outright when
    // g_maxReachNum_ ~= 3). failed_out freezes the run: the Player stops
    // updating (judgement counts freeze at the bust value, as on the cabinet),
    // the music stops, and the result ribbon starts after FAILOUT_RESULT_DELAY.
    bool   failed_out    = false;
    double failed_out_at = 0.0;
    // ROUND 32 (r32-audit-gamedan): which song(s) of a NON-gothrough exam have
    // already been individually judged to have missed their own quota -- see
    // check_exam_failures(). One bool per exam per song; reset in init_dan().
    std::vector<std::array<bool, 3>> exam_song_failed;
    std::optional<DanInfoCache> dan_info_cache;

    //DanTransition dan_transition;

    std::unique_ptr<OutlinedText> hori_name;

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
    // dan-aware body (a skip ends the CURRENT song of the course, not the
    // whole exam, and must recount THIS song's bad notes against a
    // multi-song cumulative Player -- see Player::cut_to_end's baseline
    // params). Because GameScreen::poll_skip()'s own tenth-hit line calls
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
