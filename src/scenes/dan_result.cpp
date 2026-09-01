#include "dan_result.h"
#include <cmath>
#include <cstdlib>   // ROUND 72: std::getenv for the YATAIDON_R72_DANRES trace
#include "../libs/input.h"
#include "../libs/scores.h"

// ROUND 19's `song_info.cpp` precedent: an optional `outline` key on a skin
// entry, in OutlinedText's outline-radius units, falling back to the historic
// hardcoded 5 when the skin never declared one.
static float skin_outline(const SkinInfo& s) { return s.outline >= 0 ? s.outline : 5.0f; }

// ─── DanResultScreen ─────────────────────────────────────────────────────────
//
// ROUND 16 (r16-dan) — the cabinet's SEQUENCE, which this screen never had.
//
// Until this round DAN_RESULT drew page 1 and then sat there: page 2 (the pass/
// fail stamp, the rank plate, the condition rows, the totals) was gated behind a
// don press that an unattended cabinet never gets, and nothing on page 1 moved.
// The 39.06 cabinet advances by itself, and every constant below is read out of
// the cabinet, not chosen:
//
//   script_lua/dani_result/DaniResultSongMain.lua   kWaitTime_    = 0.5 * FPS
//                                                   kWaitEndTime_ = 30  * FPS
//        -> input is dead for 0.5 s, then `InputDon() or kWaitEndTime_ <= t`
//   script_lua/dani_result/DaniResultTotalMain.lua  same pair + kWaitDetail_ 0.2 s
//   Common.FPS = 120 (the Lua timers); the lumen clips stay 60 fps.
//
// and the reveal cadence is the frame labels of `dani_result.nulm` sprite 437
// (`lumen_anim_dump --list`):
//
//   init 0 | dani 5 | song_01 45 | song_02 70 | song_03 95 | end_song 119
//   total 120 | end_total 214 | detail_in_01 215 | end_detail_01 244
//   detail_in_02 245 | ... | detail_in_03 275 | end_detail_03 304
//   stamp 305 | end_stamp 394
//
// ROUND 50 (r50-dani-visual-completion) — the fixed 30-frame row stagger above
// was ROUND 47's placeholder; the cabinet actually CHAINS the page-2 sequence
// on count-up completion (DaniResultTotalMain: TotalSlideIn -> TotalCountUp ->
// TotalEndWait(0.2s) -> [DetailSlideIn -> DetailCountUp]* -> DetailEndWait
// (0.5s) -> stamp). Every phase duration is computable up front (the values
// are final when the screen opens), so build_page2_timeline() flattens the
// whole state machine into a schedule; a don press mid-sequence skips it all
// (TotalSkip/DetailSkip), landing the stamp 0.5 s later.

namespace {
constexpr double FRAME_MS     = 1000.0 / 60.0;    // lumen clip frames
constexpr double LUA_FRAME_MS = 1000.0 / 120.0;   // Common.FPS = 120

// DaniResultSongMain.lua:12-13, DaniResultTotalMain.lua:18-20.
constexpr double WAIT_TIME_MS     = 0.5  * 1000.0;   // input dead zone
constexpr double WAIT_END_TIME_MS = 30.0 * 1000.0;   // auto-advance
constexpr double WAIT_DETAIL_MS   = 0.2  * 1000.0;   // kWaitDetail_

// Page 1: `dani` is frame 5, so song_0N lands (45|70|95) - 5 frames in.
constexpr double SONG_LAND_MS[3] = { 40 * FRAME_MS, 65 * FRAME_MS, 90 * FRAME_MS };
// Page 2 lumen segments.
constexpr double TOTAL_SLIDE_MS  = 94 * FRAME_MS;    // total -> end_total
constexpr double ROW_SLIDE_MS    = 29 * FRAME_MS;    // detail_in_0N -> end_detail_0N
constexpr double STAMP_ANM_MS    = 89 * FRAME_MS;    // stamp -> end_stamp

// DaniResultSongScore.NumCounter: each digit rolls for kRollEndTime_ =
// 0.5 * FPS Lua frames = 500 ms, ones digit first.
constexpr double DIGIT_ROLL_MS   = 500.0;
// DaniResultTotalBase: gauge +1 unit per (kGaugeSpeed_=2)+1 = 3 Lua frames.
constexpr double GAUGE_UNIT_MS   = 3 * LUA_FRAME_MS;         // 25 ms
// gauge_result_num_mc num_in(5)->wait(15) = 10 lumen frames, then
// kGaugeNumWait_ = 0.5*FPS.
constexpr double GAUGE_NUMIN_MS  = 10 * FRAME_MS + 500.0;
// DaniResultDetailBase: +1 unit per (kGaugeSpeed_=1)+1 = 2 Lua frames.
constexpr double ROW_UNIT_MS     = 2 * LUA_FRAME_MS;         // 16.67 ms
// score_all_mc num_in reveal (~15 lumen frames) + kNumInWait_ = 60 Lua frames.
constexpr double ROW_NUMIN_MS    = 15 * FRAME_MS;
constexpr double ROW_NUMWAIT_MS  = 60 * LUA_FRAME_MS;        // 500 ms

// The slide-in itself, measured off the cabinet rather than guessed:
// `lumen_anim_dump dani_result.nulm --sprite 437 --range 40,125 --all` gives
// song_board_01_mc `tx` 1740 -> 140 over frames 45..67 -- 22 frames, 1600 px.
// The engine's MoveAnimation easings were fitted against those 23 samples; the
// best is `exponential` ease_out at MAE 43.2 px, and the piecewise "waypoint"
// path beats it at **MAE 26.5 px / max 138 px** with the break 5 frames in at an
// offset of 35 px. 26.5 px of 1600 is 1.7 %, and it happens mid-flight where the
// plate is travelling ~90 px per frame. That fit is what the two constants below
// encode; nothing here is hand-drawn.
constexpr double SLIDE_MS       = 22 * FRAME_MS;
constexpr float  SLIDE_DIST     = 1600.0f;
constexpr double SLIDE_BREAK_AT = 5.0 / 22.0;
constexpr float  SLIDE_BREAK_X  = 35.0f;

// Offset still to travel at `t` ms after the row started sliding (0 = at rest).
float slide_offset(double t) {
    if (t <= 0)        return SLIDE_DIST;
    if (t >= SLIDE_MS) return 0.0f;
    double p = t / SLIDE_MS;
    if (p <= SLIDE_BREAK_AT)
        return (float)(SLIDE_DIST + (SLIDE_BREAK_X - SLIDE_DIST) * (p / SLIDE_BREAK_AT));
    return (float)(SLIDE_BREAK_X * (1.0 - (p - SLIDE_BREAK_AT) / (1.0 - SLIDE_BREAK_AT)));
}

int digit_count(int v) {
    int n = 1;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

// ROUND 50 — the 昇段だドン glyph pairs (dani_shoudan.nulm text_White/Gold_mc,
// 20 frames; DaniResultReward.SetShodanText's kDan* -> w_NN mapping re-keyed to
// this skin's dan_index 0..24, "0 Shokyuu" .. "24 Tatsujin"):
//   frames 0..9 = 一..十, 10 = 初, 11 = 級, 12 = 段, 13..16 = 玄名超達,
//   17 = 人, 18 = 合 (RL), 19 = 格 (RR) — live-verified: the composite reads
//   e.g. 「十段合格」 (scratchpad/r50/shots/r50gold/21_celebration_plate.png).
bool shoudan_glyphs(int di, int& ll, int& lr) {
    if (di == 0)               { ll = 10;      lr = 11; return true; }  // 初級
    if (di >= 1  && di <= 10)  { ll = 10 - di; lr = 11; return true; }  // 十級..一級
    if (di == 11)              { ll = 10;      lr = 12; return true; }  // 初段
    if (di >= 12 && di <= 20)  { ll = di - 11; lr = 12; return true; }  // 二段..十段
    if (di >= 21 && di <= 24)  { ll = di - 8;  lr = 17; return true; }  // 玄人..達人
    return false;
}
}  // namespace

void DanResultScreen::on_screen_start() {
    Screen::on_screen_start();
    audio.play_sound("bgm", VolumePreset::MUSIC);
    // ROUND 50 — DaniResultMain.StartWait plays the announce voice WITH the
    // bgm (the same take game/dan_transition reuses on the way in), and page 1
    // opens on se_daniresult partial_intro (DaniResultSongMain Loading).
    audio.play_sound("announce", VolumePreset::VOICE);
    audio.play_sound("partial_intro", VolumePreset::SOUND);

    fade_out   = (FadeAnimation*)tex.get_animation(0);
    page2_fade = (FadeAnimation*)tex.get_animation(1);
    is_page2   = false;
    page_start_ms = get_current_ms();
    // ROUND 72 — page 1 gets its own, never-rewound clock (see dan_result.h).
    page1_start_ms = page_start_ms;
    page1_armed.assign(
        global_data.session_data[(int)global_data.player_num].dan_result_data.songs.size(),
        false);

    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    background.emplace(PlayerNum::DAN, tex.screen_width);

    // The player Don + nameplate: the cabinet has them on this screen from the
    // first frame (see the header comment). Built exactly the way ResultPlayer
    // builds its pair, minus the RESULT score animator this screen has no use for.
    {
        auto pd = scores_manager.get_player_data(get_player_id(global_data.player_num));
        chara = make_chara_from_player_data(pd ? &*pd : nullptr);
        if (pd) {
            chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
            chara->apply_face(pd->chara_face_index);
        } else {
            chara->set_don_colors(chara_default_color_1(get_player_id(global_data.player_num)),
                                  chara_default_color_2(get_player_id(global_data.player_num)),
                                  {249, 240, 225, 255});
        }
        chara->set_anim(AnimIndex::DON_NORMAL);
        nameplate = Nameplate(pd ? pd->username : "", pd ? pd->title : "",
                              global_data.player_num,
                              pd ? pd->dan : -1, pd ? pd->gold : false,
                              pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
    }
    //gauge = std::make_unique<Gauge>(Gauge::make_result(GaugeMode::DAN, global_data.player_num, sd.dan_result_data.gauge_length));

    int font_size = tex.skin_config[SC::DAN_TITLE].font_size;
    hori_name = std::make_unique<OutlinedText>(sd.dan_result_data.dan_title, font_size, ray::WHITE, ray::BLACK, false,
                                                skin_outline(tex.skin_config[SC::DAN_TITLE]));

    // ROUND 22 (r22-danresultlist): the row title is its own text field on the
    // cabinet (`dani_result.nulm` id=109, `dani_result_song_name`), with its own
    // measured 40px size -- NOT the page-2 course title's 36px (SC::DAN_TITLE),
    // which this used to borrow. See draw_page1() for the alignment fix this
    // pairs with.
    const SkinInfo& sn_cfg = tex.skin_config[SC::DAN_RESULT_SONG_NAME];
    int song_font_size = sn_cfg.font_size > 0 ? sn_cfg.font_size : font_size;
    // (song_names are built AFTER apply_reward below — the ROUND 57 hidden
    // mask needs prev_arrival, which apply_reward fetches.)

    // ── ROUND 50 ─────────────────────────────────────────────────────────────
    // The built-in gauge row's data (DaniResultTotalBase.SetUp: g_tamashiiGauge-
    // Value_ + g_gaugeBorderValue_): our gauge exam's own live counter value IS
    // the arcade percentage (game_dan.cpp get_exam_progress "gauge"), and its
    // red border is the norma.
    gauge_exam = -1;
    const DanResultData& rd = sd.dan_result_data;
    for (int i = 0; i < (int)rd.exams.size() && i < (int)rd.exam_data.size(); i++) {
        if (rd.exams[i].type == "gauge") {
            gauge_exam   = i;
            gauge_value  = std::max(0, std::min(100, rd.exam_data[i].counter_value));
            gauge_border = std::max(0, std::min(100, rd.exams[i].red));
            break;
        }
    }

    page2_skipped = false;
    se_total_intro = se_countup = se_gauge_max = se_stamp = se_voice = false;
    se_advance = se_shogo = false;
    page1_plates_played = 0;
    celebrating = false;
    shodan = false;
    prev_best = 0;
    // ROUND 57
    congrats_due = congrats_showing = false;
    congrats_start_ms = 0;
    se_congrats = false;
    prev_best_score = 0;
    best_score_show = false;

    apply_reward();
    build_page2_timeline();

    // ROUND 57 — page-1 row titles, hidden-mask aware (DaniResultSongBase
    // SetUp: `unreach == true and g_arrivalSongCnt_ < songNum+1 and isHidden`
    // -> the row is titled the wordlist's "hidden" ？？？？？？; a song the
    // player ever reached — this run or any stored one — shows its name).
    song_names.clear();
    for (int i = 0; i < (int)sd.dan_result_data.songs.size(); i++) {
        const DanResultSong& song = sd.dan_result_data.songs[i];
        std::string title = song.song_title;
        if (song.hidden && song.unreached && prev_arrival < i + 1)
            title = "？？？？？？";
        song_names.push_back(std::make_unique<OutlinedText>(title, song_font_size, ray::WHITE, ray::BLACK, false,
                                                              skin_outline(sn_cfg)));
    }
}

// ROUND 50 — rank persistence + nameplate update (DaniResult.lua CheckReward +
// DaniResultReward.ChangeNamePlateDani). The cabinet keeps
// DaniRecordInfo.normal_rank[dani] = best g_odaiResult_ + 1 and rewrites the
// nameplate the first time a course is passed at a better verdict
// (g_daniRank_[g_odaiDani_] < g_odaiResult_ + 1 -> g_isShodan_). This engine's
// equivalents: the `dan_results` table in scores.db (one row per player+course
// title) and PlayerData.dan/gold/rainbow (the nameplate chip the settings menu
// already exposes — the chip index doubles as the arcade's g_dispDani_ display
// choice: a player who sets dan = -1 in settings hides the band, which is the
// same gate `is_disp_dani` provides on the cabinet; documented as the
// deliberate model, see LUA_CHN_AUDIT.md nameplate row (a)).
// Chip tiers: gold plate at a gold verdict (odai_result >= 4, rank 5..7),
// rainbow at 全良金 (odai_result == 6, rank 7).
void DanResultScreen::apply_reward() {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;
    if (rd.odai_result < 0) return;               // legacy record — no verdict

    const int pid = get_player_id(global_data.player_num);
    auto prev = scores_manager.get_dan_record(pid, rd.dan_title);
    prev_best = prev ? prev->rank : 0;
    prev_best_score = prev ? prev->score : 0;     // ROUND 57
    prev_arrival    = prev ? prev->arrival : 0;   // ROUND 57 (hidden-song mask)

    // ROUND 57 — a FAIL is recorded too: the cabinet's CheckReward runs
    // unconditionally, so a failed course stores normal_rank = 0+1 = 1
    // ("played, never passed") — which is exactly the state DaniResult.lua's
    // congrats test (`g_daniRank_ <= 1`) and the hidden-song reveal
    // (clear_level > kNoPlay) both key on. r50 skipped fails entirely.
    const int new_rank = rd.odai_result + 1;      // 1 = played/failed, 2..7 = passes
    // arrival_song_cnt: songs actually REACHED this run (the unreach rows are
    // the ones the fail-out cut off).
    int arrival = 0;
    for (const auto& s : rd.songs)
        if (!s.unreached) arrival++;

    // 昇段 only on a PASS that beats the stored rank, and never for a gaiden
    // course (DaniResult.lua:318 `g_odaiResult_ > 0 and g_isGaiden_ == false`).
    shodan = rd.odai_result > 0 && new_rank > prev_best && !rd.is_gaiden;

    // ── ROUND 57 ─────────────────────────────────────────────────────────────
    // Best-score bar (DaniResultTotalBase.SetUp counter_bestScore_ = score -
    // bestScore, shown iff > 0 and g_isBestScore_). g_isBestScore_ starts true
    // and is cleared when the stored rank beats this run (`g_daniRank_ >
    // g_odaiResult_+1`, DaniResult.lua:341) or the course was never recorded
    // (`g_daniRank_ == 0`, :345 — a first pass has no best to beat), and the
    // skip/fail paths never reach a positive odai_result here at all.
    best_score_show = rd.odai_result > 0 && prev_best > 0 &&
                      prev_best <= new_rank && rd.score > prev_best_score;
    // Congrats popup (DaniResult.lua:318-329): a 昇段 on the library's own
    // HIGHEST course (g_odaiDani_ == g_maxDaniNum_) whose stored rank was
    // still 0/1 (never passed). Gaiden courses are not modelled (schema-less,
    // ROUND 47 item 6), so g_isGaiden_ == false always holds here.
    congrats_due = shodan && rd.dan_index >= 0 &&
                   rd.dan_index_max >= 0 && rd.dan_index == rd.dan_index_max &&
                   prev_best <= 1;

    DanRecord rec;
    rec.dan_index = rd.dan_index;
    rec.rank      = std::max(prev_best, new_rank);
    rec.score     = std::max(prev_best_score, rd.score);
    rec.arrival   = std::max(prev ? prev->arrival : 0, arrival);   // ROUND 57
    scores_manager.save_dan_record(pid, rd.dan_title, rec);

    // Nameplate: only on a rank-up, only when the course maps to a chip, and
    // only forward on the ladder (g_playerDani_ <= g_odaiDani_ gate).
    if (shodan && rd.dan_index >= 0) {
        if (auto pd = scores_manager.get_player_data(pid)) {
            if (pd->dan <= rd.dan_index) {
                pd->dan     = rd.dan_index;
                pd->gold    = rd.odai_result >= 4;
                pd->rainbow = rd.odai_result == 6;
                scores_manager.save_player_data(*pd);
                spdlog::info("Dan rank-up: '{}' -> nameplate dan={} gold={} rainbow={} (rank {} > prev {})",
                             rd.dan_title, pd->dan, pd->gold, pd->rainbow, new_rank, prev_best);
            }
        }
    }
}

// ROUND 50 — flatten DaniResultTotalMain's chained state machine into a
// schedule on the page-2 clock. See the header comment for the states.
void DanResultScreen::build_page2_timeline() {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;

    totals_start = TOTAL_SLIDE_MS;

    // Totals phase: the 8 rolled counters (score twice = same digits) roll
    // 500 ms per digit, ones-first; the gauge fills at 25 ms/unit in parallel
    // and holds num_in + 0.5 s after.
    int total_good = 0, total_ok = 0, total_bad = 0, total_dr = 0;
    for (const auto& s : rd.songs) {
        total_good += s.good; total_ok += s.ok; total_bad += s.bad; total_dr += s.drumroll;
    }
    int max_digits = digit_count(rd.score);
    for (int v : { total_good, total_ok, total_bad, total_dr, rd.max_combo,
                   total_good + total_ok + total_bad + total_dr })
        max_digits = std::max(max_digits, digit_count(v));
    double totals_dur = max_digits * DIGIT_ROLL_MS;
    if (gauge_exam >= 0)
        totals_dur = std::max(totals_dur, gauge_value * GAUGE_UNIT_MS + GAUGE_NUMIN_MS);
    totals_end = totals_start + totals_dur;

    // Detail rows, chained (the gauge exam is the built-in totals-phase row).
    rows.assign(rd.exams.size(), RowSchedule{});
    se_row_fill.assign(rd.exams.size(), false);
    se_row_judge.assign(rd.exams.size(), false);
    double t = totals_end + WAIT_DETAIL_MS;
    for (int i = 0; i < (int)rd.exams.size() && i < (int)rd.exam_data.size(); i++) {
        RowSchedule& r = rows[i];
        if (i == gauge_exam) {
            r.land  = 0.0;                        // on the board from page open
            r.fill0 = totals_start;
            r.filld = gauge_value * GAUGE_UNIT_MS;
            r.numin = totals_start + r.filld + GAUGE_NUMIN_MS - 500.0;
            continue;
        }
        const DanResultExam& re = rd.exam_data[i];
        float p = re.progress;
        // Units the bar traverses: an 以上 bar climbs to p, a 未満 bar counts
        // DOWN from 100 % to p (DaniResultDetailBase InitDetailAll down-type).
        float travel = (rd.exams[i].range == "less") ? (1.0f - p) : p;
        if (!rd.exams[i].gothrough) {
            travel = 0.0f;
            for (int j = 0; j < 3; j++) {
                float sp = re.song_progress[j];
                travel = std::max(travel, (rd.exams[i].range == "less") ? (1.0f - sp) : sp);
            }
        }
        r.land  = t;
        r.fill0 = t + ROW_SLIDE_MS;
        r.filld = std::max(0.0, (double)std::lround(travel * 100.0f) * ROW_UNIT_MS);
        r.numin = r.fill0 + r.filld + ROW_NUMIN_MS;
        t = r.numin + ROW_NUMWAIT_MS;
    }
    if (rows.empty()) t = totals_end + WAIT_DETAIL_MS;

    stamp_at = t + WAIT_TIME_MS;                  // DetailEndWait kWaitTime_
    voice_at = stamp_at + STAMP_ANM_MS;           // end_stamp -> StampVoice
}

Screens DanResultScreen::on_screen_end(Screens next_screen) {
    reset_session();
    exam_captions.clear();   // ROUND 70 -- OutlinedText out before the font does
    return Screen::on_screen_end(next_screen);
}

void DanResultScreen::handle_input(double current_ms) {
    const double on_page = current_ms - page_start_ms;

    // `if this.currentTime_ < this.kWaitTime_ then return end` -- the cabinet
    // eats every don for the first half second of each page so the press that
    // ended the previous one cannot skip this one too.
    if (on_page < WAIT_TIME_MS) return;

    const bool don = is_l_don_pressed(global_data.player_num) ||
                     is_r_don_pressed(global_data.player_num);
    // `InputDon() == true or this.kWaitEndTime_ <= this.currentTime_`
    const bool timed_out = on_page >= WAIT_END_TIME_MS;
    if (!don && !timed_out) return;

    // ROUND 57 — the congrats popup takes no input at all: DaniResultReward's
    // Congrat state machine has no InputDon branch anywhere (Start_ -> Wait_
    // until the clip's `end` label at f899 -> kWaitFolder_ = 180 Lua frames)
    // — the screen advances itself from update(). Swallow the don silently.
    if (congrats_showing) return;

    if (don) audio.play_sound("don", VolumePreset::SOUND);

    if (celebrating) {
        const double on_cel = current_ms - celebrate_start_ms;
        // ROUND 57 — the cabinet's Shodan machine only reaches its don-exit
        // Wait_ state after InWait (the playhead reaching dress_change_start
        // = f180 = 3.0 s) plus the nameplate rank-up beat; the r50 0.5 s gate
        // let a don kill the celebration before the glyphs even popped
        // (f35/f63). 3.5 s = the 3.0 s InWait plus the plate beat estimate
        // (the nameplate clip's own length is unexported — documented).
        constexpr double CEL_EXIT_GATE_MS = 3500.0;
        if (on_cel >= CEL_EXIT_GATE_MS || timed_out) {
            if (congrats_due) {
                // DaniResultReward SetNextState: Shodan -> Congrat.
                congrats_showing  = true;
                congrats_start_ms = current_ms;
                celebrating       = false;
            } else if (!fade_out->is_started) {
                fade_out->start();
            }
        }
        return;
    }

    if (is_page2) {
        // ROUND 50 — a don mid-sequence is TotalSkip/DetailSkip, not an exit:
        // everything lands and the stamp comes 0.5 s later (DetailEndWait).
        if (!page2_skipped && on_page < stamp_at && !timed_out) {
            page2_skipped = true;
            totals_start = std::min(totals_start, on_page);
            totals_end   = on_page;
            for (auto& r : rows) {
                r.land  = std::min(r.land,  on_page);
                r.fill0 = std::min(r.fill0, on_page);
                r.filld = 0.0;
                r.numin = on_page;
            }
            stamp_at = on_page + WAIT_TIME_MS;
            voice_at = stamp_at + STAMP_ANM_MS;
            return;
        }
        // The stamp must be up (and its own 0.5 s dead zone past) before the
        // screen can move on (DaniResultTotalMain.state stamp).
        if (on_page < stamp_at + WAIT_TIME_MS && !timed_out) return;

        if (shodan && !celebrating) {
            // 昇段だドン (DaniResultReward Shodan) — the celebration overlay.
            celebrating = true;
            celebrate_start_ms = current_ms;
            return;
        }
        if (!fade_out->is_started) fade_out->start();
    } else {
        page2_fade->start();
        is_page2 = true;
        page_start_ms = current_ms;
    }
}

// ROUND 50 — the scheduled one-shot cues (see build_page2_timeline).
void DanResultScreen::update_sounds(double now) {
    const double on_page = now - page_start_ms;
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;

    // ROUND 57 — Congrat Start_: `glad_c` + `popup_superlative`
    // (DaniResultReward.lua:250-251), fired the moment the popup opens.
    if (congrats_showing) {
        if (!se_congrats) {
            se_congrats = true;
            audio.play_sound("voice_glad", VolumePreset::VOICE);
            audio.play_sound("popup_superlative", VolumePreset::SOUND);
        }
        return;
    }

    if (celebrating) {
        const double on_cel = now - celebrate_start_ms;
        if (!se_advance) {
            se_advance = true;
            audio.play_sound("advance_intro", VolumePreset::SOUND);
            audio.play_sound("voice_advance", VolumePreset::VOICE);
        }
        // ROUND 57 — the beat is the playhead REACHING dress_change_start
        // (f180 = 3.0 s; ShodanState.InWait_ waits on that label before
        // PlayDaniRankUpStart + advance_shogo). r50's 2.0 s was an estimate
        // made before the clip labels were dumped.
        if (!se_shogo && on_cel >= 3000.0) {
            se_shogo = true;        // nameplate_dani_rankup beat
            audio.play_sound("advance_shogo", VolumePreset::SOUND);
        }
        return;
    }

    if (!is_page2) {
        // One partial_plate per song board as its slide starts
        // (DaniResultSongMain.SlideInSE).
        while (page1_plates_played < (int)rd.songs.size() && page1_plates_played < 3 &&
               on_page >= SONG_LAND_MS[page1_plates_played]) {
            audio.play_sound("partial_plate", VolumePreset::SOUND);
            page1_plates_played++;
        }
        return;
    }

    if (!se_total_intro) {
        se_total_intro = true;      // DaniResultTotalMain Loading Finish
        audio.play_sound("total_intro", VolumePreset::SOUND);
    }
    if (!se_countup && on_page >= totals_start && !page2_skipped) {
        se_countup = true;          // StartCountUp: se_common count_up_loop_c
        audio.play_sound("count_up_loop", VolumePreset::SOUND);
    }
    if (!se_gauge_max && gauge_exam >= 0 && gauge_value >= 100 &&
        on_page >= totals_start + gauge_value * GAUGE_UNIT_MS) {
        se_gauge_max = true;        // achieve_tamashii_c at kGaungeMax_
        audio.play_sound("achieve_tamashii", VolumePreset::SOUND);
    }
    for (int i = 0; i < (int)rows.size(); i++) {
        if (i == gauge_exam) continue;
        if (!se_row_fill[i] && on_page >= rows[i].fill0 && rows[i].filld > 0 && !page2_skipped) {
            se_row_fill[i] = true;  // gauge_up_loop / gauge_down_loop
            const bool less = i < (int)rd.exams.size() && rd.exams[i].range == "less";
            audio.play_sound(less ? "gauge_down_loop" : "gauge_up_loop", VolumePreset::SOUND);
        }
        if (!se_row_judge[i] && on_page >= rows[i].numin) {
            se_row_judge[i] = true; // UpdateNumIn: gauge_judgement
            audio.play_sound("gauge_judgement", VolumePreset::SOUND);
        }
    }
    if (!se_stamp && on_page >= stamp_at) {
        se_stamp = true;            // DaniResultTotalMain.StampSE
        const int r = rd.odai_result;
        if (r == 0)      audio.play_sound("stamp_notclear",     VolumePreset::SOUND);
        else if (r < 0) {
            bool any_failed = std::any_of(rd.exam_data.begin(), rd.exam_data.end(),
                                          [](const DanResultExam& e){ return e.failed; });
            audio.play_sound(any_failed ? "stamp_notclear" : "stamp_clear_normal", VolumePreset::SOUND);
        }
        else if (r <= 3) audio.play_sound("stamp_clear_normal", VolumePreset::SOUND);
        else             audio.play_sound("stamp_clear_upper",  VolumePreset::SOUND);
    }
    if (!se_voice && on_page >= voice_at) {
        se_voice = true;            // StampVoice at end_stamp
        const int r = rd.odai_result;
        if (r == 0) {
            audio.play_sound("voice_notclear", VolumePreset::VOICE);
        } else if (r > 0) {
            audio.play_sound("atmos_clear", VolumePreset::SOUND);
            audio.play_sound(r <= 3 ? "voice_clear_normal" : "voice_clear_upper", VolumePreset::VOICE);
        }
    }
}

std::optional<Screens> DanResultScreen::update() {
    Screen::update();
    double current_ms = get_current_ms();
    allnet_indicator.update(current_ms);

    handle_input(current_ms);
    update_sounds(current_ms);
    page2_fade->update(current_ms);
    fade_out->update(current_ms);
    nameplate.update(current_ms);
    if (chara) chara->update(current_ms);

    // ROUND 57 — the congrats popup self-advances: the clip runs start ->
    // `end` (f899 = 15.0 s @60 fps) and the state then dwells kWaitFolder_ =
    // 180 Lua frames (1.5 s @ Common.FPS 120) before finishing. No input path
    // exists in the arcade state machine, so this timer is the only exit.
    if (congrats_showing &&
        current_ms - congrats_start_ms >= 899 * FRAME_MS + 180 * LUA_FRAME_MS) {
        if (!fade_out->is_started) fade_out->start();
    }

    if (fade_out->is_finished)
        return on_screen_end(Screens::DAN_SELECT);

    return std::nullopt;
}

// The Don and the nameplate ride on both pages (cabinet alpha 1 throughout), so
// this is called once from draw() rather than from either page.
//
// Positions: the cabinet's `don_1p` registration point is screen (220, 786) and
// `plate_1p_instance` is (220, 970) (`--sprite 437 --range 394,394`). Our own
// anchors are not those registration points, so the values are carried across
// with the offset the RESULT screen already proves: the arcade `result.nulm`
// puts `don_L` at (238, 787) and `name_plate_L` at (198.6, 970), and this skin
// draws them at result_chara (205, 935) / result_nameplate (2, 922) -- i.e.
// chara = reg + (-33, +148) (Chara3D anchors at the feet) and nameplate =
// reg - (196.6, 48) (top-left of a 393x96 plate). Applying the same two offsets
// to the dan reg points gives (187, 934) and (23, 922), which is what the skin
// declares as `dan_result_chara` / `dan_result_nameplate`. A skin that declares
// neither keeps the RESULT slots.
void DanResultScreen::draw_chara_and_plate() {
    const SkinInfo* c = tex.skin_entry("dan_result_chara");
    const SkinInfo* n = tex.skin_entry("dan_result_nameplate");
    const SkinInfo& cs = c ? *c : tex.skin_config[SC::RESULT_CHARA];
    const SkinInfo& ns = n ? *n : tex.skin_config[SC::RESULT_NAMEPLATE];
    if (chara) chara->draw(cs.x, cs.y);
    nameplate.draw(ns.x, ns.y);
}

// ROUND 50 — count-up-aware digit column. `digits` is most-significant-first;
// digit r from the right rolls during [r*500, (r+1)*500) ms of `roll_t`
// (cycling faces, DaniResultSongScore's digit "loop" clip), lands after, and is
// hidden ("none") before its own roll begins. roll_t < 0 = everything landed.
void DanResultScreen::draw_digit_counter(const std::string& digits, float margin_x, TexID id,
                                          int index, float y, double fade, float scale,
                                          float x_off, double roll_t) {
    const int n = (int)digits.size();
    for (int j = 0; j < n; j++) {
        const int r = n - 1 - j;              // position from the right (0 = ones)
        int frame = digits[j] - '0';
        if (roll_t >= 0.0) {
            if (roll_t < r * DIGIT_ROLL_MS) continue;                  // "none"
            if (roll_t < (r + 1) * DIGIT_ROLL_MS)
                frame = (int)(roll_t / 83.0 + r) % 10;                 // rolling
        }
        float x = x_off - (float)(n - j) * margin_x;
        tex.draw_texture(id, {.frame=frame, .scale=scale, .x=x, .y=y, .fade=fade, .index=index});
    }
}

void DanResultScreen::draw_page1(double now) {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    float height = tex.skin_config[SC::DAN_RESULT_INFO_HEIGHT].y;
    float margin = tex.skin_config[SC::SCORE_INFO_COUNTER_MARGIN].x;
    // ROUND 72 — page 1's OWN clock. This used to read `page_start_ms`, which
    // handle_input REWINDS to `now` when page 2 opens (the cabinet re-arms
    // kWaitTime_/kWaitEndTime_ per page, so that rewind is correct FOR INPUT).
    // Reading it here re-armed every board's `land` gate and replayed the whole
    // 3-row slide-in from the right on the page-2 clock, under (and past the
    // right edge of) page 2's 500 ms cross-fade. `dani_result.nulm` MC 437 is a
    // single playhead — song_01 45 / song_02 70 / song_03 95, then `total` at
    // 120 — that never rewinds, so page 1 animates exactly once.
    const double on_page = now - page1_start_ms;

    // ROUND 72 — env-gated slide trace (`YATAIDON_R72_DANRES=1`), the r40
    // `[r40animclock]` pattern. One line the first time each board crosses its
    // own `land`, so a REPLAY shows up in the log instead of having to be
    // inferred from pixels. `page1_armed` is only ever reset by
    // on_screen_start, so a second set of lines means the clock rewound.
    static const bool r72_trace = std::getenv("YATAIDON_R72_DANRES") != nullptr;

    for (int i = 0; i < (int)sd.dan_result_data.songs.size(); i++) {
        const DanResultSong& song = sd.dan_result_data.songs[i];
        float y = i * height;

        // The cabinet slides each board in from the right on its own clock and
        // does not draw it at all before its label frame. Courses with more than
        // three songs keep the same 25-frame stagger past the third.
        double land = (i < 3) ? SONG_LAND_MS[i]
                              : SONG_LAND_MS[2] + (i - 2) * 25 * FRAME_MS;
        if (on_page < land) continue;
        float sx = slide_offset(on_page - land);
        if (r72_trace && i < (int)page1_armed.size() && !page1_armed[i]) {
            page1_armed[i] = true;
            spdlog::info("[r72danres] page1 board {} SLIDE-IN arm at on_page={:.0f} "
                         "(land={:.0f}) is_page2={} page1_clock={:.0f} page_clock={:.0f}",
                         i, on_page, land, is_page2 ? 1 : 0,
                         now - page1_start_ms, now - page_start_ms);
        }

        // ROUND 47 -- an unreached song (the course failed out before it): the
        // cabinet's g_unreach_ board (DaniResultSongBase StartUnreach) shows
        // the song's own banner but NO stats. This skin ships no dedicated
        // unreach art, so the row is drawn dimmed with its stats suppressed --
        // if `result_info/unreached` art lands later it is drawn on top.
        const float rf = song.unreached ? 0.45f : 1.0f;

        tex.draw_texture(BACKGROUND::GENRE_BANNER, {.frame=song.genre_index, .x=sx, .y=y, .fade=rf});

        if (i < (int)song_names.size() && song_names[i]) {
            // ROUND 22 (r22-danresultlist): the cabinet LEFT-aligns this field
            // (`dani_result.nulm` id=109, align=0, pen (632,192.5) row 1 / row
            // pitch 276 -- verified against `dan_result_info_height.y`, which
            // this screen already had right) -- this had been right-aligning
            // instead (ROUND 11 C-list item 4, never closed). A fixed right
            // anchor makes the LEFT edge walk with every title's width, so a
            // long title starts near mid-row while a short one hugs the right
            // edge -- the exact per-row inconsistency the user reported ("one
            // row looks centered"). Left-aligning at a fixed x makes every row
            // start at the same point regardless of length. The cabinet's own
            // box is 1126 px wide (632..1758); `dan_result_song_name.width`
            // carries that so a title that overflows it shrinks via `x2`
            // (same squeeze `draw_page2`'s `hori_name` already does) instead of
            // spilling into the stat columns or changing anchor.
            const SkinInfo& sn = tex.skin_config[SC::DAN_RESULT_SONG_NAME];
            float tw = song_names[i]->width;
            float draw_w = sn.width > 0 ? std::min(tw, sn.width) : tw;
            song_names[i]->draw({.x = sn.x + sx, .y = y + sn.y, .x2 = draw_w - tw});
        }

        tex.draw_texture(RESULT_INFO::SONG_NUM,   {.frame=i, .x=sx, .y=y, .fade=rf});
        tex.draw_texture(RESULT_INFO::DIFFICULTY, {.frame=song.selected_difficulty, .x=sx, .y=y, .fade=rf});
        tex.draw_texture(RESULT_INFO::DIFF_STAR,  {.x=sx, .y=y, .fade=rf});
        tex.draw_texture(RESULT_INFO::DIFF_X,     {.x=sx, .y=y, .fade=rf});

        std::string lv = std::to_string(song.diff_level);
        float dm = tex.skin_config[SC::DAN_RESULT_DIFF_NUM_MARGIN].x;
        // ROUND 33 (r33-diffnum-level10): the real chip's own multi-digit glyph
        // (song_board_diff_num_mc__shape104, the "10" chip) is LEFT-anchored and
        // grows RIGHTWARD -- see MAPPING.md ROUND 33 for the full derivation.
        //
        // ROUND 74 defect 5 -- the pitch was 7, which is arithmetically too
        // small for THESE glyphs to clear each other: "1" carries ink at cell
        // columns 12..21 and "0" at 9..24, so a pitch p puts "1" ink at
        // [12,21] and "0" ink at [p+9, p+24]; they only stop overlapping at
        // p >= 22-9+1 = 13.  At 7 the ★10 chip overlapped by SIX columns.
        //
        // The cabinet's own numbers (`lumen_shape_dump dani_result.nulm ... -m
        // -r 108 -f {0,40,45}`, char 108 = song_board_diff_num_mc, one
        // pre-composed star+number artwork per level, all at identity):
        //   ★1  star ink 373..398, "1" ink 409..418
        //   ★9  star ink 373..398, "9" ink 406..422
        //   ★10 star ink 373..398, "1" ink 400..409, "0" ink 413..428
        // A single digit therefore sits on cell-left 397 (409-12 and 406-9 both
        // give 397) and the PAIR sits on cell-left 388, i.e. the block shifts
        // LEFT by 9 per extra digit, and the pair's own pitch is
        // (413-9) - (397-9) = 16.  Hence `dan_result_diff_num_margin.x` = 16 and
        // `dan_result_diff_num_lead.x` = 9; levels 1..9 are byte-identical
        // (lead*0 = 0) and ★10 reproduces the cabinet's 3 px inter-digit gap to
        // the pixel.  `lead` is read by name and defaults to 0 so a skin that
        // never declared it keeps ROUND 33's plain left anchor.
        const SkinInfo* dl = tex.skin_entry("dan_result_diff_num_lead");
        const float lead = (dl ? dl->x : 0.0f) * (float)(lv.size() - 1);
        for (int j = 0; j < (int)lv.size(); j++)
            tex.draw_texture(RESULT_INFO::DIFF_NUM,
                             {.frame=lv[j]-'0', .x=sx - lead + (float)(j*dm), .y=y});

        auto draw_stat = [&](TexID icon, int val, int idx) {
            tex.draw_texture(icon, {.x=sx, .y=y, .fade=rf});
            if (song.unreached) return;         // ROUND 47: no digits on an unreach row
            std::string sv = std::to_string(val);
            std::reverse(sv.begin(), sv.end());
            for (int j = 0; j < (int)sv.size(); j++)
                tex.draw_texture(RESULT_INFO::COUNTER, {.frame=sv[j]-'0', .x=sx-(float)(j*margin), .y=y, .index=idx});
        };
        // ROUND 50 note: the cabinet's page 1 lands its numbers INSTANTLY --
        // DaniResultSongMain.SetUpBoard calls CountSkip() at setup and SlideIn
        // goes straight to Wait, so the CountUp state is never entered. The
        // instant landing here is therefore arcade-correct, not a gap.
        draw_stat(RESULT_INFO::GOOD,     song.good,     0);
        draw_stat(RESULT_INFO::OK,       song.ok,       1);
        draw_stat(RESULT_INFO::BAD,      song.bad,      2);
        draw_stat(RESULT_INFO::DRUMROLL, song.drumroll, 3);
        if (song.unreached && tex.has_texture("result_info/unreached"))
            tex.draw_texture(tex.get_enum("result_info/unreached"), {.x=sx, .y=y});
    }
}

// ROUND 50 — the built-in tamashii gauge row (DaniResultTotalBase +
// enso_dani/enso/tamashiigauge/tamashii_gauge.nulm, label `dani_result`).
// Replaces the generic threshold-bar drawing for the course's `gauge` exam:
// the cabinet's gauge row is the soul-gauge widget itself — 50 lamp cells at a
// 21 px pitch, a dark under-norma zone, the norma marker chevron, the 魂 ball,
// and the percentage in the gauge's own 56x64 digit set whose palette follows
// the verdict (noclear/clear/max). All geometry is measured (see
// scratchpad/r50/bake_tamashii.py header); the count-up runs at the cabinet's
// own 25 ms/unit with the digits revealed only after the fill (InitGauge's
// empty -> num_in order).
//
// ROUND 65 — the widget now lives in TRUE arcade coordinates. ROUND 50
// authored every position as `exam_bg + 0.8*(arcade - exam_bg)` because the
// column drew at 0.8; that scale was a mistake (dani_result.nulm MC 437 places
// the cards at native size, 154 apart), so `tamashii/texture.json` was
// re-based by the inverse map and `scale` is 1.0. Cross-check: the re-based
// track x0 is 563 (ROUND 50 measured 564) and the 魂 ball lands at 1616, i.e.
// its 88 px art centred on the widget origin + 514 = 1660. Both fall out of
// the transform, neither was tuned.
void DanResultScreen::draw_gauge_row(const Exam& exam, float y, double fade, double now, float scale) {
    const double on_page = now - page_start_ms;
    const int final_cells  = gauge_value  / 2;
    const int border_cells = gauge_border / 2;

    int cur = gauge_value;
    bool landed = true;
    if (!page2_skipped && on_page < totals_start + gauge_value * GAUGE_UNIT_MS) {
        cur = (int)std::max(0.0, (on_page - totals_start) / GAUGE_UNIT_MS);
        if (cur > gauge_value) cur = gauge_value;
        landed = false;
    }
    const int cells = cur / 2;
    const bool clear = cur >= gauge_border && gauge_border > 0;

    auto T = [&](const char* n) { return tex.get_enum(std::string("tamashii/") + n); };

    tex.draw_texture(T("base"), {.scale=scale, .y=y, .fade=fade});
    if (border_cells > 0)
        tex.draw_texture(T("norma"), {.scale=scale, .y=y, .x2=scale*21.0f*border_cells, .fade=fade});
    if (cells > 0)
        tex.draw_texture(T(clear ? "fill_clear" : "fill_1p"),
                         {.scale=scale, .y=y, .x2=scale*21.0f*cells, .fade=fade});
    tex.draw_texture(T("overlay"), {.scale=scale, .y=y, .fade=fade});

    // Percentage in the gauge's own digit set, group centred, "%" last —
    // hidden until the fill lands (gauge_result_num_mc stays "empty" until
    // num_in). Palette per GaugeNumColorUpdate: max / clear / noclear_1p.
    const bool numin = page2_skipped ||
                       on_page >= totals_start + gauge_value * GAUGE_UNIT_MS + (GAUGE_NUMIN_MS - 500.0);
    if (numin) {
        const char* pal = gauge_value >= 100 ? "max" : clear ? "clear" : "noclear";
        std::string s = std::to_string(gauge_value);
        const int n = (int)s.size() + 1;                        // digits + %
        const float pitch = 34.0f * scale;
        TexID numtex = T((std::string("num_") + pal).c_str());
        TexID pcttex = T((std::string("pct_") + pal).c_str());
        for (int k = 0; k < n; k++) {
            float dx = (k - (n - 1) / 2.0f) * pitch;
            if (k < n - 1)
                tex.draw_texture(numtex, {.frame=s[k]-'0', .scale=scale, .x=dx, .y=y, .fade=fade});
            else
                tex.draw_texture(pcttex, {.scale=scale, .x=dx, .y=y, .fade=fade});
        }
    }

    // ROUND 57 — the fever flame (r50's documented cut): GaugeLampUpdate
    // plays tamashii_mc "fever_start" the moment the fill reaches
    // kGaungeMaxValue_ = 100 (tamashii_gauge.nulm sprite 136: fever_start
    // f20..28 — flame grows in at the ball, alpha 0->1 over f20..24 — then
    // the fever loop from f29: 8 flame cels (shapes 120..134) at 3 f/cel,
    // 24-frame cycle, with the ball flickering dark/lit every 2 frames and
    // its glow copy (shape 115) pulsing 0.6/0 in antiphase). Art baked by
    // scratchpad/r57/bake_r57.py into tamashii/flame (8 frames, pre-scaled
    // to the clip's own 0.8) + tamashii/fever_glow. The grow-in's 0.14->0.8
    // scale ramp rides alpha only here (150 ms; the row draw path is
    // TL-anchored so a faithful centre-anchored scale would need its own
    // anchor math — documented approximation).
    const bool fever = gauge_value >= 100 && landed;
    if (fever && tex.has_texture("tamashii/flame")) {
        // clip clock starts at fever entry: the frame the fill landed on 100.
        const double t0 = page2_skipped ? 0.0
                          : totals_start + gauge_value * GAUGE_UNIT_MS;
        const double f = 20.0 + std::max(0.0, on_page - t0) / FRAME_MS;
        const double flame_a = std::min(1.0, (f - 20.0) / 4.0);
        const int cel = f < 29.0 ? 0 : ((int)((f - 29.0) / 3.0)) % 8;
        tex.draw_texture(T("flame"), {.frame=cel, .scale=scale, .y=y,
                                      .fade=fade * flame_a});
        const bool tick = ((int)((f - 29.0) / 2.0)) % 2 == 0;
        tex.draw_texture(T(f >= 29.0 && tick ? "tamashii_dark" : "tamashii_lit"),
                         {.scale=scale, .y=y, .fade=fade});
        if (f >= 24.0 && (f < 29.0 || tick) && tex.has_texture("tamashii/fever_glow"))
            tex.draw_texture(T("fever_glow"), {.scale=scale, .y=y, .fade=fade * 0.6});
    } else {
        tex.draw_texture(T(clear && landed ? "tamashii_lit" : "tamashii_dark"),
                         {.scale=scale, .y=y, .fade=fade});
    }
    tex.draw_texture(T("marker"), {.scale=scale, .x=scale*21.0f*border_cells, .y=y, .fade=fade});
}

// ROUND 65 (r65-danresult-examrows) — the page-2 condition rows rebuilt on the
// cabinet's own card, measured from 39.06 rather than inferred.
//
// SOURCES. `script_lua/dani_result/DaniResultTotalMain.lua` builds this column
// out of `total_mc` (the soul-gauge row — DaniResultTotalBase) plus up to THREE
// `total_detail_0N_mc`, each of which loads
// `lumen/000_default/enso_dani/result/dani_result_detail.nulm` into its
// `dummy_detail`. So the page-2 block is at most FOUR cards, and every card is
// the same 1400x149 white plate (that movie's shape 1).
//
// GEOMETRY (all 1920x1080 skin units; `lumen_anim_dump dani_result.nulm
// --sprite 437 --range 305,305` for the card origins, `lumen_shape_dump
// dani_result_detail.nulm ... -m -r 234 -f {0,5,10,15} -v` for everything
// inside one card — the -v placement list IS the authored table):
//
//   card centres      (1100, 425) then +154 per row  -> ROW PITCH 154, and the
//                     card spans x 400..1800, y centre +-74.5
//   caption pill      `txt_theme_name`/`tx_detail_label`, text box local
//                     (-683,-74.5) 340x54, centred — drawn on EVERY card,
//                     whole-run and per-song alike
//   whole-run ("all", detail_mc frame 0)
//     badge           the gold tomoe cluster, ring centred local (-648.5, 18.5)
//     bar             local x -302..676 (978 wide), y -41..51; the fill grows
//                     from the LEFT (`score_com_gauge_bar_up_mc` is a dark
//                     cover, right-anchored, that shrinks to nothing at 100)
//     value           `score_l_*_mc` digit group, LEFT-anchored at local
//                     (-257,-10), pitch 52 — the big number sits hard against
//                     the bar's left end and grows rightwards
//     threshold       `tx_border` text, LEFT-aligned at local (-602,-24.5),
//                     size 36, next to the badge
//   per-song ("1st"/"2nd"/"3rd", frames 5/10/15)
//     three FIXED slots at local x -415 / 50 / 515 (pitch 465); only the first
//     `g_maxReachNum_` are placed, each with its own ordinal badge at
//     local -559/-94/371 (i.e. slot - 144), its own 330x54 box, its own
//     LEFT-anchored digit group at slot+(-140,12) pitch 27, and its own
//     `tx_border` at slot+(-76,20.5) size 20 — BELOW the box.
//
// THE 未満 VALUE IS NOT THE ACHIEVED COUNT. DaniResultDetailBase.InitDetailAll /
// InitDetail feed the digit counters `odaiBorder_ - odaiNum_` (clamped at 0) on
// a `less` row and `odaiNum_` on a `more` row: a 「8 未満」 card with zero 可
// shows a big 8 — the REMAINING ALLOWANCE — and its bar starts full and unwinds.
//
// THERE IS NO 達成失敗 ON THIS SCREEN. `isUnreach_` is declared in
// DaniResultDetailBase and never read; `dani_result_detail.nulm` has no fail
// label (labels: all/1st/2nd/3rd, init/num_in/wait, and the gauge palettes).
// The cabinet's 達成失敗 plate belongs to page 1's SONG boards
// (`DaniResultSongBase.mainMc_.unreached`, StartUnreach/end_unreach), which is
// where this engine already draws it. A lost condition on page 2 is carried by
// the bar colour and the value alone — so the overlay this screen used to stamp
// over the threshold caption and the 魂 emblem is gone.
//
// SCALE. The column draws at NATIVE size. ROUND 50 authored its gauge widget
// into a 0.8-scaled frame; that 0.8 was never in the cabinet (the card really is
// 1400 px wide on a 1470 px board), it just made the row block cover ~72 % of
// the panel and squeezed the 154 px pitch down to 112, which is what made the
// captions collide. `tamashii/texture.json` is re-based to true arcade
// coordinates in the same round, so `scale` is 1.0 everywhere here.
void DanResultScreen::draw_exam_info(double fade, double now, float scale) {
    const SessionData& sd  = global_data.session_data[(int)global_data.player_num];
    const double on_page = now - page_start_ms;
    // Per-screen override of the shared dan_exam_info; see game_dan.cpp.
    // DAN_RESULT's own entry carries the CARD pitch (154) and the big-digit
    // pitch (52); GAME_DAN's shared 140/40 belong to the in-play panel.
    const SkinInfo& dei = tex.skin_entry("dan_exam_info_result")
                        ? *tex.skin_entry("dan_exam_info_result")
                        : tex.skin_config[SC::DAN_EXAM_INFO];
    float offset_y = dei.y * scale;
    float margin   = dei.x * scale;

    // Per-song slot pitch (arcade 465) and the sub-row's own digit pitch (27).
    const SkinInfo* sr = tex.skin_entry("dan_result_exam_sub_row");
    const float sub_pitch = (sr ? sr->y : 465.0f) * scale;
    const SkinInfo* sb = tex.skin_entry("dan_result_exam_sub_bar");
    const float sub_full   = sb ? sb->width : 324.0f;
    const float sub_margin = (sb ? sb->x : 27.0f) * scale;

    // The cabinet's threshold caption is a size-36 text field, not the big
    // score digit set; the skin ships its own baked 26x48 sheet for it. Fall
    // soft to the value digits for a skin that never baked one.
    const bool has_border_digits = tex.has_texture("exam_info/exam_border_counter");
    const TexID border_id = has_border_digits ? EXAM_INFO::EXAM_BORDER_COUNTER
                                              : EXAM_INFO::VALUE_COUNTER;
    const float border_pitch = (has_border_digits ? 26.0f : margin) * scale;
    // The per-song `tx_border` is size 20 against the whole-run row's 36.
    const float sub_border_scale = 20.0f / 36.0f;

    // A texture.json entry may be a single object or an array; `index` indexes
    // that array, and TextureObject defaults to ONE entry. The per-song row
    // wants the second pen, so ask first -- a skin (PyTaikoGreen, or any child
    // that never declared the pair) must not be indexed out of range.
    auto pens = [&](TexID id) {
        auto it = tex.textures.find((uint32_t)id);
        return (it == tex.textures.end()) ? 1 : (int)it->second->x.size();
    };
    const int sub_pen    = pens(EXAM_INFO::EXAM_LESS) > 1 ? 1 : 0;
    const int sub_bd_pen = pens(border_id)            > 1 ? 1 : 0;

    // Left-anchored digit column (the cabinet's own anchoring for BOTH the
    // value and the threshold: digit1 = ones sits at (n-1)*pitch and the
    // leftmost glyph never moves, so a 3-digit value starts where a 1-digit
    // value starts). draw_digit_counter() is the RIGHT-anchored page-1 helper
    // and stays for its own callers.
    auto digits_left = [&](const std::string& s, float x0, float y0, float pitch,
                           TexID id, int index, float sc) {
        for (int k = 0; k < (int)s.size(); k++)
            tex.draw_texture(id, {.frame=s[k]-'0', .scale=sc, .x=x0 + k*pitch,
                                  .y=y0, .fade=fade, .index=index});
    };

    // ROUND 67 -- one fill draw for both the whole-run bar and a per-song slot.
    //
    // `state` is a frame label of dani_result_detail.nulm sprite 49 / 156 (see
    // dan_bar_state() in global_data.h). Five of the seven are a FLAT TINT of a
    // white rectangle (shape #47, cmul=[0,0,0,256] + cadd=<RGB>), which this
    // skin ships as 8 px colour strips; `empty` places no fill at all; and
    // `max` replaces the fill entirely with `score_l/m_rainbow_mc` -- one
    // gradient PERIOD as wide as the bar, scrolled a whole period per 80-frame
    // (1.333 s) loop, i.e. 972/80*60 = 729 px/s on the 972-wide bar. The tile
    // ships at two periods so the source rect can slide without wrapping.
    auto have = [&](TexID id) {
        return tex.textures.find((uint32_t)id) != tex.textures.end();
    };
    auto draw_exam_fill = [&](const std::string& state, float w, float rx, float ry,
                              float sc, double fd, double t, bool sub) {
        if (w <= 0 || state == "empty") return;
        const TexID rb   = sub ? EXAM_INFO::EXAM_SUB_RAINBOW : EXAM_INFO::EXAM_RAINBOW;
        const TexID d80  = sub ? EXAM_INFO::EXAM_SUB_DOWN80  : EXAM_INFO::EXAM_DOWN80;
        const TexID up50 = sub ? EXAM_INFO::EXAM_SUB_RED     : EXAM_INFO::EXAM_RED;
        const TexID up80 = sub ? EXAM_INFO::EXAM_SUB_GOLD    : EXAM_INFO::EXAM_GOLD;
        const TexID p100 = sub ? EXAM_INFO::EXAM_SUB_MAX     : EXAM_INFO::EXAM_MAX;
        if (state == "max" && have(rb)) {
            auto it = tex.textures.find((uint32_t)rb);
            const float th   = (float)it->second->height;
            const float perd = (float)it->second->width * 0.5f;   // two periods
            // The clip loops in 80 stage frames at the movie's own 60 fps
            // (`lumen_anim_dump --list` reports "fps": 60 for both
            // dani_result_detail and dani_enso_detail), i.e. one full period
            // every 1.3333 s. NEVER Common.FPS=120 here -- the Lua tick rate and
            // the clip's frame rate are different clocks.
            //
            // ROUND 74 defect 1 -- the caller's `t` is get_current_ms(), a fresh
            // wall-clock read taken inside draw(); using it samples the scroll at
            // draw time and so carries the frame's CPU-time variance into the
            // phase. The scroll takes the frame-latched clock instead
            // (animation.h:18 -- "so render-time variance doesn't cause jitter").
            // `t` is deliberately left alone: every other animation on this
            // screen is keyed to it and to the counters it drives.
            (void)t;
            const float ph   = perd - (float)std::fmod(get_frame_ms() / 1000.0
                                                       * (perd * 60.0 / 80.0),
                                                       (double)perd);
            // ROUND 74 defect 1 -- WRAP SAFETY, same as game_dan.cpp. Every
            // texture is loaded with TEXTURE_WRAP_CLAMP (texture.h:100/119), so
            // a source window that runs past the tile repeats its last texel
            // column instead of wrapping -- a frozen stripe of whatever colour
            // ends the tile (pink, here). DAN_RESULT's own tiles happen to fit
            // exactly (972 window on a 1944 tile, 324 on 648), so this branch
            // never fires today; it is here so a future period/bar mismatch
            // degrades into a seamless tile rather than a stuck edge.
            const float sw = w / sc;                   // source width, tile units
            const float avail = 2.0f * perd - ph;
            if (sw <= avail) {
                tex.draw_texture(rb, {.scale=sc, .x=rx, .y=ry, .x2=w, .fade=fd,
                                      .src=ray::Rectangle{ph, 0.0f, sw, th}});
            } else {
                tex.draw_texture(rb, {.scale=sc, .x=rx, .y=ry, .x2=avail * sc,
                                      .fade=fd,
                                      .src=ray::Rectangle{ph, 0.0f, avail, th}});
                tex.draw_texture(rb, {.scale=sc, .x=rx + avail * sc, .y=ry,
                                      .x2=(sw - avail) * sc, .fade=fd,
                                      .src=ray::Rectangle{0.0f, 0.0f, sw - avail, th}});
            }
            return;
        }
        TexID id = p100;
        if (state == "up_50")        id = up50;
        else if (state == "up_80")   id = up80;
        else if (state == "down_80") id = have(d80) ? d80 : up80;
        // `max` on a skin with no rainbow art falls back to the 100 % tint.
        tex.draw_texture(id, {.scale=sc, .x=rx, .y=ry, .x2=w, .fade=fd});
    };

    for (int i = 0; i < (int)sd.dan_result_data.exams.size(); i++) {
        const Exam& exam       = sd.dan_result_data.exams[i];
        const DanResultExam& rd = sd.dan_result_data.exam_data[i];
        float y = i * offset_y;

        // ROUND 50 — rows land on the chained schedule (DetailSlideIn), not a
        // fixed 30-frame stagger.
        const RowSchedule rs = i < (int)rows.size() ? rows[i] : RowSchedule{};
        if (on_page < rs.land) continue;

        // Row fill animation: the bar climbs (or, for 未満, unwinds from 100 %)
        // at the cabinet's 1 unit / 2 Lua frames until its own landing.
        double frac = 1.0;
        if (!page2_skipped && rs.filld > 0)
            frac = std::max(0.0, std::min(1.0, (on_page - rs.fill0) / rs.filld));
        const bool row_numin = page2_skipped || on_page >= rs.numin;

        tex.draw_texture(EXAM_INFO::EXAM_BG, {.scale=scale, .y=y, .fade=fade});

        // ROUND 50 — the course's gauge exam is the arcade's built-in
        // tamashii-gauge row, not a generic threshold bar.
        const bool tamashii_row = (i == gauge_exam) && tex.has_texture("tamashii/base");

        if (tamashii_row) {
            draw_gauge_row(exam, y, fade, now, scale);
        } else if (exam.gothrough) {
            // 39.06 dani_result_detail.nulm detail_mc "all" frame: ONE tomoe
            // badge near the row's own left edge (measured absolute offset from
            // exam_bg's own origin, scratchpad/r19dn/render_detail_row2.py).
            tex.draw_texture(EXAM_INFO::EXAM_BADGE, {.frame=0, .scale=scale, .y=y, .fade=fade});
            tex.draw_texture(EXAM_INFO::EXAM_OVERLAY_1, {.scale=scale, .y=y, .fade=fade});

            float p = rd.progress;
            if (exam.range == "less") p = 1.0f - (1.0f - p) * (float)frac;
            else                      p = p * (float)frac;
            float bar_w = dei.width * p * scale;
            // ROUND 67 -- the cabinet's own six-state palette. While the bar is
            // still climbing the row is in an intermediate counting-up state
            // (GaugeColorChange is re-run every counter tick with isMax=false,
            // DaniResultDetailBase.lua:519/542); only the LANDED bar takes the
            // settled state, which is the one that can be `max` (= rainbow).
            std::string bs = rd.bar_state;
            if (frac < 1.0) {
                const int g = (int)std::floor(p * 100.0f);
                bs = g <= 0 ? "empty" : g <= 49 ? "up_50" : g <= 99 ? "up_80" : "up_100";
            }
            draw_exam_fill(bs, bar_w, 0.0f, y, scale, fade, now, false);
        } else {
            // Per-song: the cabinet's `1st`/`2nd`/`3rd` frames of detail_mc —
            // THREE FIXED SLOTS at local x -415/50/515 (pitch 465), of which
            // only the first `g_maxReachNum_` are placed. g_maxReachNum_ is the
            // number of songs the run actually REACHED (DaniResult.lua
            // SetUnReach), so a fail-out that never got to song 3 legitimately
            // shows two boxes -- but a completed course must show all three,
            // which the old `rd.song_count + 1` (song_count = songs finished
            // BEFORE the current one, snapshotted mid-run) could not.
            int reach = 0;
            for (const auto& s : sd.dan_result_data.songs)
                if (!s.unreached) reach++;
            if (reach <= 0) reach = std::max(1, rd.song_count + 1);
            reach = std::min(reach, 3);
            for (int j = 0; j < reach; j++) {
                float sx = j * sub_pitch;
                // The ordinal badge is the SAME cluster slot as the whole-run
                // tomoe (shapes 228/230/232 vs 3, all at detail-local
                // -559+465k), so it rides the identical `exam_badge` anchor;
                // frame 0 = tomoe, 1..3 = 1st/2nd/3rd.
                tex.draw_texture(EXAM_INFO::EXAM_BADGE, {.frame=j + 1, .scale=scale, .x=sx, .y=y, .fade=fade});
                tex.draw_texture(EXAM_INFO::EXAM_SUB_BG, {.scale=scale, .x=sx, .y=y, .fade=fade});
                float sp = rd.song_progress[j];
                if (exam.range == "less") sp = 1.0f - (1.0f - sp) * (float)frac;
                else                      sp = sp * (float)frac;
                // ROUND 67 -- same cabinet palette as the whole-run bar
                // (sprite 156 carries the identical label set and tints).
                std::string sbs = rd.song_state[j];
                if (frac < 1.0) {
                    const int g = (int)std::floor(sp * 100.0f);
                    sbs = g <= 0 ? "empty" : g <= 49 ? "up_50"
                        : g <= 99 ? "up_80" : "up_100";
                }
                draw_exam_fill(sbs, sub_full * sp, sx, y, scale, fade, now, true);
                tex.draw_texture(EXAM_INFO::EXAM_SUB_FRONT, {.scale=scale, .x=sx, .y=y, .fade=fade});
                // Per-box threshold (`tx_border`, size 20) UNDER the box. The
                // cabinet carries a per-song odaiBorder_[idx]; this engine's
                // dan.json schema has one border per exam, so every box repeats
                // the course's own value (documented divergence, MAPPING R65).
                // ROUND 70 -- char 226, size 20, align=1=RIGHT, border 3.50,
                // three slots whose translates are +439/-26/-491 (right edges
                // card-local +677/+212/-253 = screen 1777/1312/847 at pitch
                // 465). Same one-text-run rule as the whole-run field above.
                const SkinInfo* sbt = tex.skin_entry("dan_result_exam_border_song");
                OutlinedText* scap = nullptr;
                float sbt_ol = 3.5f / tex.screen_scale;
                if (sbt) {
                    if (sbt->outline >= 0) sbt_ol = sbt->outline;
                    scap = exam_captions.get(
                        exam_threshold_text(tex, exam.type, exam.range, exam.red,
                                            global_data.config->general.language),
                        sbt->font_size > 0 ? sbt->font_size : 20, sbt_ol);
                }
                if (scap) {
                    const float pad = ExamCaptionCache::pad_for(sbt_ol, tex.screen_scale);
                    scap->draw({.x = sbt->x + sx - scap->width + pad,
                                .y = sbt->y - pad + y, .fade = fade});
                } else {
                    const std::string bs = std::to_string(exam.red);
                    const float bx = sx;
                    digits_left(bs, bx, y, border_pitch * sub_border_scale,
                                border_id, sub_bd_pen, scale * sub_border_scale);
                    const float after = bx + bs.size() * border_pitch * sub_border_scale
                                      + 8.0f * scale * sub_border_scale;
                    if (exam.range == "less")
                        tex.draw_texture(EXAM_INFO::EXAM_LESS, {.scale=scale*sub_border_scale, .x=after, .y=y, .fade=fade, .index=sub_pen});
                    else if (exam.range == "more")
                        tex.draw_texture(EXAM_INFO::EXAM_MORE, {.scale=scale*sub_border_scale, .x=after, .y=y, .fade=fade, .index=sub_pen});
                }
                // The live per-song value, LEFT-anchored at the box's own left
                // end (score_m_*_mc at slot+(-140,12), pitch 27) — the cabinet
                // reveals it only at num_in.
                // (`song_value[j]` is ALREADY the cabinet's number: game_dan.cpp
                // stores `max(0, red - val)` on a `less` exam, so no transform
                // here — applying one again zeroed the row.)
                if (row_numin) {
                    // ROUND 78 -- same per-state digit rule as the whole-run row
                    // (score_m_gauge_num_mc char 225 carries the identical label
                    // set); only `max` has its own sheet.
                    const bool smax = frac >= 1.0 && rd.song_state[j] == "max"
                                    && have(EXAM_INFO::EXAM_SUB_COUNTER_MAX);
                    digits_left(std::to_string(rd.song_value[j]), sx, y, sub_margin,
                                smax ? EXAM_INFO::EXAM_SUB_COUNTER_MAX
                                     : EXAM_INFO::EXAM_SUB_COUNTER, 0, scale);
                }
            }
        }

        // ── the caption pill ────────────────────────────────────────────────
        // `txt_theme_name` (DaniResultDetailBase.Init's clear_theme_* keys) is
        // a child of detail_mc itself and is drawn on EVERY frame label, so the
        // per-song rows get their caption too. This whole block used to sit
        // inside `if (exam.gothrough || tamashii_row)`, which is exactly why a
        // per-song row (a 連打数 course condition) came up with an EMPTY pill.
        // It is also drawn at a FIXED anchor — the old digit-count shift
        // (`-len * 20 * screen_scale`) has no counterpart on the cabinet.
        static const std::unordered_map<std::string, TexID> icon_ids = {
            {"gauge",        EXAM_INFO::EXAM_GAUGE},
            {"combo",        EXAM_INFO::EXAM_COMBO},
            {"hit",          EXAM_INFO::EXAM_HIT},
            {"judgebad",     EXAM_INFO::EXAM_JUDGEBAD},
            {"judgegood",    EXAM_INFO::EXAM_JUDGEGOOD},
            {"judgeperfect", EXAM_INFO::EXAM_JUDGEPERFECT},
            {"score",        EXAM_INFO::EXAM_SCORE},
            {"renda",        EXAM_INFO::EXAM_ROLL},    // ROUND 47: 連打数 exam type
        };
        auto icon_it = icon_ids.find(exam.type);
        if (icon_it != icon_ids.end())
            tex.draw_texture(icon_it->second, {.scale=scale, .y=y, .fade=fade});

        if (exam.gothrough || tamashii_row) {
        const std::string red_str = std::to_string(exam.red);

        // ROUND 50 / 65 — on the tamashii row the threshold caption is NOT in
        // the card's own tx_border pen: it belongs to the gauge movie and RIDES
        // THE NORMA MARKER. `tamashii_gauge.nulm` sprite 145 (`gauge_border`)
        // carries the chevron (shape 139) at its own origin and the text at
        // (-113, 10), size 25, align=2 — i.e. CENTRED on the marker in a 228x52
        // box, so its centre lands on the marker's own x and 26 px below the
        // text-box top. gauge_mc frame border_N places that whole sprite at
        // track_x0 − 14 + 21·N (verified: border_15 sits at widget-local −281 =
        // screen 865, and 563 − 14 + 21·15 = 864).
        //
        // Screen for row 0 therefore: centre x = 550 + 21·cells, centre y = 477
        // — 52 px below where the detail rows' own 36 px caption sits, and
        // under the lamp track rather than over it (ROUND 50's "pull 288 px
        // and left-align" rode the marker but landed the text ON the gauge).
        // The caption is also the gauge's own 25 px size, not 36.
        const float csc  = tamashii_row ? (25.0f / 36.0f) : 1.0f;
        const float gap1 = 4.0f * scale * csc, gap2 = 10.0f * scale * csc;
        float cap_w = red_str.size() * border_pitch * csc;
        if (exam.type == "gauge") cap_w += gap1 + 31.0f * scale * csc;
        if (exam.range == "less" || exam.range == "more")
            cap_w += gap2 + 70.0f * scale * csc;
        float cap_dx = 0.0f, cap_dy = 0.0f;
        if (tamashii_row) {
            cap_dx = scale * (21.0f * (gauge_border / 2) + 52.0f) - cap_w * 0.5f;
            cap_dy = 52.0f * scale;
        }

        // ROUND 70 — the cabinet draws this as ONE proportional text run, and
        // right-aligns it (`dani_result_detail.nulm` char 119, size 36,
        // align=1=RIGHT, border 4.00, box right edge card-local −332 = screen
        // 768). See objects/game/exam_caption.h.
        //
        // Measured BEFORE (scratchpad/r70/m_res.py, the 可の数 row of
        // scratchpad/r67/shots/after_pass/15_p2_settled.png against the
        // cabinet's own card rendered at the SAME screen origin):
        //     ours    "8 未満"  digit 498-523, 未満 539-596   (left-anchored)
        //     cabinet "8 未満"  digit 660-689, 未満 698-773   (right-anchored)
        // -- 162 px too far left, and our 未満 is 58 px wide against the
        // cabinet's 76 (baked below its own size-36 record).
        //
        // The tamashii row is a DIFFERENT field: it belongs to the gauge movie
        // and rides the norma marker, size 25 align=2 CENTRE, and its wordlist
        // key is `requirement_soul_gauge` = "%s ％" with NO 以上
        // (DaniResultTotalBase.lua:77 -- the same key dani_select uses at
        // dani_select_theme_disp_data.lua:141). exam_threshold_text() returns
        // that for a gauge exam, which is why the "％" and "以上" sprites and
        // their invented 4/10 px gaps all disappear together.
        const SkinInfo* bt = tex.skin_entry(tamashii_row ? "dan_result_gauge_border_text"
                                                         : "dan_result_exam_border_text");
        OutlinedText* capt = nullptr;
        float bt_ol = 4.0f / tex.screen_scale;
        if (bt) {
            if (bt->outline >= 0) bt_ol = bt->outline;
            capt = exam_captions.get(
                exam_threshold_text(tex, exam.type, exam.range, exam.red,
                                    global_data.config->general.language),
                bt->font_size > 0 ? bt->font_size : (tamashii_row ? 25 : 36), bt_ol);
        }
        if (capt) {
            const float pad = ExamCaptionCache::pad_for(bt_ol, tex.screen_scale);
            // The whole-run field is RIGHT-anchored on `bt->x` (a right EDGE, so
            // the image's own symmetric padding has to come back off); the
            // tamashii one is CENTRED on the norma marker, and the padding is
            // symmetric there so centring the image centres the glyphs.
            const float cx = tamashii_row
                ? bt->x + scale * (21.0f * (gauge_border / 2) + 52.0f) - capt->width * 0.5f
                : bt->x - capt->width + pad;
            capt->draw({.x = cx, .y = bt->y - pad + y, .fade = fade});
        } else {
        // Threshold caption, in the cabinet's own reading order — digits, then
        // ％ on a gauge row, then 以上/未満.
        digits_left(red_str, cap_dx, y + cap_dy, border_pitch * csc, border_id, 0, scale * csc);
        float after = cap_dx + red_str.size() * border_pitch * csc;
        if (exam.type == "gauge") {
            tex.draw_texture(EXAM_INFO::EXAM_PERCENT, {.scale=scale*csc, .x=after + gap1, .y=y + cap_dy, .fade=fade, .index=0});
            after += gap1 + 31.0f * scale * csc;
        }
        after += gap2;
        if (exam.range == "less")
            tex.draw_texture(EXAM_INFO::EXAM_LESS, {.scale=scale*csc, .x=after, .y=y + cap_dy, .fade=fade});
        else if (exam.range == "more")
            tex.draw_texture(EXAM_INFO::EXAM_MORE, {.scale=scale*csc, .x=after, .y=y + cap_dy, .fade=fade});
        }

        if (!tamashii_row) {
            tex.draw_texture(EXAM_INFO::EXAM_OVERLAY_2, {.scale=scale, .y=y, .fade=fade});
            // ROUND 50 — the live count stays hidden until the row's num_in
            // (DaniResultDetailBase: the digits appear AFTER the bar fill).
            // ROUND 65 — and it is LEFT-anchored against the bar's own left
            // end, big (pitch 52), NOT right-anchored off the card's right
            // margin (which is how it ended up floating outside the plate).
            // A 未満 row shows the REMAINING ALLOWANCE (odaiBorder_ − odaiNum_,
            // clamped at 0), which is what InitDetailAll feeds its counters —
            // and which game_dan.cpp:302 ALREADY stores in counter_value, so
            // this draws it straight.
            if (row_numin) {
                // ROUND 78 -- the value digits carry the bar's own state, and
                // exactly ONE of the seven differs: `score_l_gauge_num_mc`
                // (char 118) draws its three layers white in every flat state
                // and, on `max`, colour-MULTIPLIES back+front by the movie's own
                // style[25] #FF5096 and the `add` layer (a white glyph with a
                // top-heavy alpha ramp) by style[26] #FFFF00 -- the cabinet's
                // yellow-to-pink vertical gradient. The skin bakes that as a
                // second sheet; a skin without it keeps the white one.
                const int vpen = pens(EXAM_INFO::VALUE_COUNTER) > 1 ? 1 : 0;
                const int ppen = pens(EXAM_INFO::EXAM_PERCENT)  > 1 ? 1 : 0;
                const std::string cur_str = std::to_string(rd.counter_value);
                // Only the LANDED bar takes `max` (same rule as the fill: the
                // counting-up ticks re-run GaugeColorChange with isMax=false).
                const bool vmax = frac >= 1.0 && rd.bar_state == "max"
                                && have(EXAM_INFO::VALUE_COUNTER_MAX);
                digits_left(cur_str, 0.0f, y, margin,
                            vmax ? EXAM_INFO::VALUE_COUNTER_MAX
                                 : EXAM_INFO::VALUE_COUNTER, vpen, scale);
                if (exam.type == "gauge")
                    tex.draw_texture(EXAM_INFO::EXAM_PERCENT,
                                     {.scale=scale, .x=cur_str.size()*margin + 6.0f*scale,
                                      .y=y, .fade=fade, .index=ppen});
            }
        }
        }

        // ROUND 65 — NO 達成失敗 overlay here. See this function's header: the
        // cabinet's page-2 detail card has no fail state at all (its
        // `isUnreach_` is dead, the movie has no fail label); 達成失敗 is a
        // page-1 SONG-board plate (DaniResultSongBase `unreached`). The old
        // overlay + 50 % dim was stamped across the threshold caption and the
        // 魂 emblem, which is the collision the user reported.
    }
}

void DanResultScreen::draw_page2(double fade, double now) {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;
    const double on_page = now - page_start_ms;

    tex.draw_texture(BACKGROUND::RESULT_2_BG, {.fade=fade});
    for (int i = 0; i < 5; i++)
        tex.draw_texture(BACKGROUND::RESULT_2_DIVIDER, {.x=(float)(i*240), .fade=fade});
    tex.draw_texture(BACKGROUND::RESULT_2_PULLOUT, {.fade=fade});

    // Cabinet display-list order on MC 437 is total_mc(13) -> com_dani_mc(15) ->
    // don_1p(17) -> plate_1p_instance(18) -> stamp_all_mc(20), and 15/17/18 are
    // alpha 1 from frame 0. So the rank plate, the Don and the nameplate go over
    // the totals board and under the stamp, at FULL opacity on both pages -- the
    // page-2 cross-fade must not take them with it. On page 1 `fade` is 0, so
    // everything else in this function is invisible and only these three draw.
    // ROUND 19 -- when the skin ships `rank_plate` art (the brush kanji AND
    // romaji, e.g. 十段/TENTH DAN, baked from com_dani_mc) it replaces the flat
    // colour plate + generated title, mirroring game_dan.cpp's same swap.
    if (rd.dan_rank >= 0 && tex.skin_entry("dan_result_rank_plate")) {
        tex.draw_texture(RESULT_INFO::RANK_PLATE, {.frame=rd.dan_rank});
    } else {
        tex.draw_texture(RESULT_INFO::DAN_EMBLEM, {.frame=rd.dan_color});
        if (hori_name) {
            // The course name belongs to the plate, so it rides the plate's
            // opacity, not the page cross-fade.
            SkinInfo hn = tex.skin_config[SC::DAN_RESULT_HORI_NAME];
            hori_name->draw({
                .x = hn.x - hori_name->width/2.0f,
                .y = hn.y,
                .x2= std::min(hori_name->width, hn.width) - hori_name->width
            });
        }
    }
    draw_chara_and_plate();

    float margin = tex.skin_config[SC::SCORE_INFO_COUNTER_MARGIN].x;

    int total_good     = 0, total_ok = 0, total_bad = 0, total_dr = 0;
    for (const auto& s : rd.songs) {
        total_good += s.good; total_ok += s.ok; total_bad += s.bad; total_dr += s.drumroll;
    }

    // ROUND 50 — the totals roll (DaniResultSongScore digit roll, ones first,
    // 0.5 s per digit) from end_total; a skip lands everything.
    const bool totals_landed = page2_skipped || on_page >= totals_end;
    const double roll_t = totals_landed ? -1.0 : (on_page - totals_start);
    if (totals_landed || on_page >= totals_start) {
        auto draw_total = [&](TexID icon, int idx_icon, int total, int idx_counter) {
            tex.draw_texture(icon, {.fade=fade, .index=idx_icon});
            draw_digit_counter(std::to_string(total), margin, RESULT_INFO::COUNTER,
                               idx_counter, 0.0f, fade, 1.0f, margin, roll_t);
        };
        draw_total(RESULT_INFO::GOOD,     1, total_good, 4);
        draw_total(RESULT_INFO::OK,       1, total_ok,   5);
        draw_total(RESULT_INFO::BAD,      1, total_bad,  6);
        draw_total(RESULT_INFO::DRUMROLL, 1, total_dr,   7);

        tex.draw_texture(RESULT_INFO::MAX_COMBO, {.fade=fade});
        draw_digit_counter(std::to_string(rd.max_combo), margin, RESULT_INFO::COUNTER,
                           8, 0.0f, fade, 1.0f, margin, roll_t);

        int total_hits = total_good + total_ok + total_bad + total_dr;
        tex.draw_texture(RESULT_INFO::MAX_HITS, {.fade=fade});
        draw_digit_counter(std::to_string(total_hits), margin, RESULT_INFO::COUNTER,
                           9, 0.0f, fade, 1.0f, margin, roll_t);
    } else {
        // Board up, counters still "none": the icons/captions are baked into
        // the panels, which are already visible.
        tex.draw_texture(RESULT_INFO::GOOD,     {.fade=fade, .index=1});
        tex.draw_texture(RESULT_INFO::OK,       {.fade=fade, .index=1});
        tex.draw_texture(RESULT_INFO::BAD,      {.fade=fade, .index=1});
        tex.draw_texture(RESULT_INFO::DRUMROLL, {.fade=fade, .index=1});
        tex.draw_texture(RESULT_INFO::MAX_COMBO,{.fade=fade});
        tex.draw_texture(RESULT_INFO::MAX_HITS, {.fade=fade});
    }

    tex.draw_texture(RESULT_INFO::EXAM_HEADER, {.fade=fade});

    // Score box (rolls with the same clock, its own margin)
    tex.draw_texture(RESULT_INFO::SCORE_BOX, {.fade=fade});
    float sm = tex.skin_config[SC::DAN_SCORE_BOX_MARGIN].x;
    if (totals_landed || on_page >= totals_start)
        draw_digit_counter(std::to_string(rd.score), sm, RESULT_INFO::SCORE_COUNTER,
                           0, 0.0f, fade, 1.0f, sm, roll_t);

    // ROUND 57 — the best-score bar above the score box (DaniResultTotalBase:
    // best_score_mc GotoAndPlay("start") fires inside StartCountUp, a skip
    // lands it on "end").
    if (best_score_show)
        draw_best_score(fade, on_page);

    draw_exam_info(fade, now);

    // The stamp: alpha 0 until the whole chained sequence has played out
    // (ROUND 50 -- stamp_at replaces ROUND 47's fixed frame-305 estimate).
    if (on_page < stamp_at) return;

    // Pass / Fail verdict.
    //
    // ROUND 47 -- the arcade's stamp is SEVEN-way, not three
    // (DaniResultTotalMain.lua SetUpStampMc: stamp_anm_fail / red_01..03 /
    // gold_01..03): the 0..6 g_odaiResult_ computed by GAME_DAN's
    // save_result_data (red/gold from the per-exam tier arithmetic, the
    // _02/_03 embellishments from course-total 可=0 / 不可=0).
    // ROUND 50 -- the four variant stamps are now baked from the cabinet's own
    // stamp_anm_{red,gold}_{02,03}_mc settled frames (white enso ring = plain,
    // gold ring = FC, rainbow ring = 全良), so the fallback below only covers a
    // skin that strips the art back out. A result from an older build
    // (odai_result == -1) keeps the pre-R47 3-way logic.
    if (rd.odai_result >= 0) {
        const int r = rd.odai_result;
        if (r == 0) {
            tex.draw_texture(EXAM_INFO::FAIL, {.fade=fade});
        } else {
            const bool gold = r >= 4;
            const int  sub  = gold ? r - 3 : r;           // 1 plain, 2 FC, 3 全良
            const std::string base = gold ? "exam_info/gold_clear" : "exam_info/red_clear";
            const std::string variant = base + "_0" + std::to_string(sub);
            if (sub > 1 && tex.has_texture(variant))
                tex.draw_texture(tex.get_enum(variant), {.fade=fade});
            else
                tex.draw_texture(gold ? EXAM_INFO::GOLD_CLEAR : EXAM_INFO::RED_CLEAR, {.fade=fade});
        }
    } else {
        bool any_failed = std::any_of(rd.exam_data.begin(), rd.exam_data.end(),
                                       [](const DanResultExam& e){ return e.failed; });
        bool all_gold   = !any_failed && !rd.exams.empty() && !rd.exam_data.empty();
        if (all_gold) {
            for (int i = 0; i < (int)rd.exams.size() && i < (int)rd.exam_data.size(); i++) {
                if (rd.exam_data[i].progress < (float)rd.exams[i].gold / (float)(rd.exams[i].red > 0 ? rd.exams[i].red : 1)) {
                    all_gold = false; break;
                }
            }
        }
        if (any_failed)
            tex.draw_texture(EXAM_INFO::FAIL,       {.fade=fade});
        else if (all_gold)
            tex.draw_texture(EXAM_INFO::GOLD_CLEAR, {.fade=fade});
        else
            tex.draw_texture(EXAM_INFO::RED_CLEAR,  {.fade=fade});
    }
}

// ROUND 57 — the best-score bar (dani_result.nulm MC 230, `best_score_mc`
// inside total_score_mc; reg screen (683,162), the same authored clip as the
// plain-result bar the skin already samples in Scripts/anim/best_score.lua).
// Value = score − stored best (counter_bestScore_); art baked by
// scratchpad/r57/bake_r57.py: pill (shape 224, incl. the dark counter box and
// the ↑), its glow copy (221), and the clip's own 16x24 digit set (shapes
// 199..217 = 0..9, digit MC 219's f5+2n frame map; ones digit at screen
// (852,159), pitch 14.17 leftward). Clip schedule from the dump: pill
// drops/bounces f5..23 (ty 24 → −22 → 0 with overshoot, alpha in over f5..10),
// digits fade in f18..21, glow burst f23..34 (scale → ~1.4/1.9, alpha → 0).
// The f34..52 masked sheen sweep is NOT ported (the engine has no mask path;
// documented cut). GotoAndPlay("start") = totals_start on the page-2 clock;
// a skip lands everything ("end").
void DanResultScreen::draw_best_score(double fade, double on_page) {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;
    if (!tex.has_texture("bestscore/pill")) return;

    // clip frame: "start" is f5; play begins at totals_start.
    double f = page2_skipped ? 52.0 : 5.0 + std::max(0.0, on_page - totals_start) / FRAME_MS;
    if (f > 52.0) f = 52.0;
    if (f < 5.0) return;                          // not started yet

    // pill ty/alpha keys (dani_result.nulm MC 230 #224 track, == the plain
    // result clip's #196 rows in anim/best_score.lua).
    static const float K[][3] = {
        {5, 24, 0}, {6, 7.45f, 0.36f}, {7, -5.4f, 0.64f}, {8, -14.6f, 0.84f},
        {9, -20.1f, 0.96f}, {10, -21.95f, 1}, {11, -20.55f, 1}, {12, -16.4f, 1},
        {13, -9.5f, 1}, {14, 0, 1}, {18, 7, 1}, {19, 4.5f, 1}, {20, 2.5f, 1},
        {21, 1.1f, 1}, {22, 0.3f, 1}, {23, 0, 1}, {52, 0, 1},
    };
    float ty = 0, a = 1;
    for (int i = 0; i + 1 < (int)(sizeof(K) / sizeof(K[0])); i++) {
        if (f >= K[i][0] && f <= K[i + 1][0]) {
            float u = (float)((f - K[i][0]) / (K[i + 1][0] - K[i][0]));
            ty = K[i][1] + (K[i + 1][1] - K[i][1]) * u;
            a  = K[i][2] + (K[i + 1][2] - K[i][2]) * u;
            break;
        }
    }

    tex.draw_texture(tex.get_enum("bestscore/pill"), {.y=ty, .fade=fade * a});

    // digits + (numbers appear f18..21)
    const double na = std::max(0.0, std::min(1.0, (f - 18.0) / 3.0));
    if (na > 0.0 && tex.has_texture("bestscore/num")) {
        TexID nt = tex.get_enum("bestscore/num");
        std::string s = std::to_string(rd.score - prev_best_score);
        for (int i = 0; i < (int)s.size(); i++) {
            const int digit = s[(int)s.size() - 1 - i] - '0';
            tex.draw_texture(nt, {.frame=digit, .x=-(float)(i * 14.17), .y=ty,
                                  .fade=fade * na});
        }
    }

    // glow burst f23..34
    if (f >= 23.0 && f < 34.0 && tex.has_texture("bestscore/glow")) {
        const float u = (float)((f - 23.0) / 11.0);
        tex.draw_texture(tex.get_enum("bestscore/glow"),
                         {.scale=1.0f + 0.6f * u, .center=true, .y=ty,
                          .fade=fade * (1.0f - u)});
    }
}

// ROUND 50 — the 昇段だドン celebration (dani_shoudan.nulm; DaniResultReward's
// Shodan state). Backgrounds and glyphs are the cabinet's own
// (bake_shoudan.py): bg W/G/R keyed exactly per StartShoudanBG
// (odai_result 1|4 -> 白, 2|5 -> 金, 3|6 -> 虹), text white for a red pass /
// gold for a gold pass (SetShodanText), four 336x360 glyph quadrants at
// centres (462/794/1126/1458, 422). The Don and the (freshly-updated)
// nameplate sit at the cabinet's own (960,775)/(960,960) registration points,
// carried across with the ROUND 16 anchor offsets. The cabinet's Efc glow
// copies, don_smoke/don_kira bursts and the dress-change costume flow are NOT
// ported (documented cut line — see ENGINE_BINDINGS.md ROUND 50).
// ROUND 57 (r57-dani-leftovers) — the r50 overlay's flat 300 ms fade replaced
// by the clip's own schedule (all frames dumped from dani_shoudan.nulm sprite
// 210, 60 fps): bg + Don are on from frame 0 (a hard cut, no fade); the
// nameplate fades in f21..29; the dan-name glyphs (LL/LR) POP at f35 (scale
// 1.5 -> 1 over 7 f) and 合格 (RL/RR) at f63; each pop is chased by its Efc
// GLOW COPY (the *_Efc_mc layers, char 133 — the same glyph MC drawn again):
// a burst over the 6 frames after the pop lands (scale 1 -> 1.25, alpha
// 1 -> 0), and one slow synchronized glow on all four quadrants f110..133
// (scale 1 -> 1.96, alpha 1 -> 0). The clip's don_smoke / don_kira /
// syogo_smoke layers stay UNPORTED — dumping proves they never fire in the
// base celebration (zero visible children across f0..320): they are
// GotoAndPlay-triggered by DaniResultRewardPanel (the costume / reward-panel
// flow, ROUND 50's item 8, still unported), not by the shoudan timeline.
void DanResultScreen::draw_celebration(double now) {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;
    const double on_cel = now - celebrate_start_ms;
    const double fF = on_cel / FRAME_MS;          // 60 fps clip frames

    const int r = rd.odai_result;
    const char* bg = (r == 2 || r == 5) ? "shoudan/bg_g"
                   : (r == 3 || r == 6) ? "shoudan/bg_r" : "shoudan/bg_w";
    if (tex.has_texture(bg))
        tex.draw_texture(tex.get_enum(bg), {});

    const char* text = r > 3 ? "shoudan/text_gold" : "shoudan/text_white";
    if (tex.has_texture(text)) {
        TexID tt = tex.get_enum(text);
        int ll = -1, lr = -1;
        const bool named = rd.dan_index >= 0 && shoudan_glyphs(rd.dan_index, ll, lr);
        // quadrant x offsets 0/332/664/996 (centres 462/794/1126/1458).
        struct Q { int frame; float x; double t0; };
        Q quads[4] = {{ll, 0.0f, 35.0}, {lr, 332.0f, 35.0},
                      {18, 664.0f, 63.0}, {19, 996.0f, 63.0}};
        for (const Q& q : quads) {
            if (q.frame < 0 || (!named && q.x < 600.0f)) continue;
            if (fF < q.t0) continue;
            const float pop = fF < q.t0 + 7.0
                ? 1.5f - 0.5f * (float)((fF - q.t0) / 7.0) : 1.0f;
            tex.draw_texture(tt, {.frame=q.frame, .scale=pop, .center=true, .x=q.x});
            // Efc burst: the 6 frames after the pop lands.
            const double e0 = q.t0 + 7.0;
            if (fF >= e0 && fF < e0 + 6.0) {
                const float u = (float)((fF - e0) / 6.0);
                tex.draw_texture(tt, {.frame=q.frame, .scale=1.0f + 0.25f * u,
                                      .center=true, .x=q.x, .fade=1.0 - u});
            }
            // Efc slow glow, all four in sync, f110..133.
            if (fF >= 110.0 && fF < 133.0) {
                const float u = (float)((fF - 110.0) / 23.0);
                tex.draw_texture(tt, {.frame=q.frame, .scale=1.0f + 0.96f * u,
                                      .center=true, .x=q.x, .fade=1.0 - u});
            }
        }
    }

    // Don + the updated nameplate (reg (960,775)/(960,960) + the ROUND 16
    // offsets: chara feet = reg + (-33,+148), plate TL = reg - (196.6,48)).
    // Don is on from frame 0; the plate fades in f21..29.
    if (chara) chara->draw(927.0f, 923.0f);
    const double plate_fade = std::max(0.0, std::min(1.0, (fF - 21.0) / 8.0));
    if (on_cel >= 3000.0) {
        // nameplate_dani_rankup beat (the playhead reaching f180, see
        // update_sounds): rebuild the plate from the saved data so the new
        // dan chip is what lands (ChangeNamePlateDani -> SetPlayerInfo).
        if (auto pd = scores_manager.get_player_data(get_player_id(global_data.player_num))) {
            static int last_built_dan = -2;
            if (last_built_dan != pd->dan) {
                nameplate = Nameplate(pd->username, pd->title, global_data.player_num,
                                      pd->dan, pd->gold, pd->rainbow, pd->title_bg);
                last_built_dan = pd->dan;
            }
        }
    }
    if (plate_fade > 0.0) nameplate.draw(763.0f, 912.0f, plate_fade);
}

// ROUND 57 — the congrats-at-top-course popup (dani_result_congrats.nulm root
// sprite 21: panel shape 1 fades in f0..19, the text f20..34, then the clip
// holds to its `end` label at f899). The clip authors TWO overlapping
// language-text MCs and the 39.06 Lua sets the language on BOTH without ever
// choosing between them; going by the messages themselves (text_01 = 「十段
// 合格…十段以降の段位は今後のアップデートで追加されます」, text_02 = 「達人
// 合格…キミが太鼓の達人の「達人」だドン！」), text_02 is the message for the
// 達人 course and text_01 for a ladder that tops out below it — recorded as an
// inference, the one undecodable piece (the switch is native-side).
void DanResultScreen::draw_congrats(double now) {
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const DanResultData& rd = sd.dan_result_data;
    const double fF = (now - congrats_start_ms) / FRAME_MS;

    // The shoudan scene stays on stage under the popup (mainMc holds its
    // "shodan" frame; dummy_congrats is an overlay at a higher depth).
    const int r = rd.odai_result;
    const char* sbg = (r == 2 || r == 5) ? "shoudan/bg_g"
                    : (r == 3 || r == 6) ? "shoudan/bg_r" : "shoudan/bg_w";
    if (tex.has_texture(sbg))
        tex.draw_texture(tex.get_enum(sbg), {});

    const double bg_fade = std::min(1.0, fF / 19.0);
    if (tex.has_texture("congrats/bg"))
        tex.draw_texture(tex.get_enum("congrats/bg"), {.fade=bg_fade});

    const char* text = (rd.dan_index == 24) ? "congrats/text_02" : "congrats/text_01";
    if (tex.has_texture(text)) {
        // language frames match the clip's own label order jpn/en_us/kor/cn_tw.
        const std::string& lang = global_data.config->general.language;
        int frame = 0;
        if      (lang.rfind("en", 0) == 0) frame = 1;
        else if (lang.rfind("ko", 0) == 0) frame = 2;
        else if (lang.rfind("zh", 0) == 0) frame = 3;
        const double tf = std::max(0.0, std::min(1.0, (fF - 20.0) / 14.0));
        if (tf > 0.0)
            tex.draw_texture(tex.get_enum(text), {.frame=frame, .fade=tf});
    }

    // The Don stays on stage under the popup (the popup is an overlay on the
    // shoudan scene, whose bg/Don layers persist — mainMc "shodan" frame).
    if (chara) chara->draw(927.0f, 923.0f);
    nameplate.draw(763.0f, 912.0f);
}

void DanResultScreen::draw() {
    double now = get_current_ms();
    if (background.has_value()) background->draw();
    // ROUND 19 -- page 1 never had its own backdrop; DAN_RESULT inherited
    // whatever the generic single-song RESULT screen's background happened to
    // be. Measured from dani_result.nulm frame 119: a flat navy 1920x1080
    // backdrop with a seigaiha wave pattern and sakura-petal corners (shape 1)
    // behind a SINGLE 1520x920 wood board (shape 4). Both drawn here, opaque.
    // ROUND 50 note: the cabinet's page-2 `result_bg_mc` (7 label frames
    // bg_fail / bg_red_01..03 / bg_gold_01..03, switched by SetUpResultBg at
    // the stamp) authors ALL SEVEN frames onto ONE identical shape — measured
    // byte-identical to this very backdrop (dan_result_p1_bg.png, pixel diff
    // 0) — so the "7-way result background" needs no art and no switch: what
    // is drawn here IS every variant.
    tex.draw_texture(BACKGROUND::DAN_RESULT_P1_BG, {});
    tex.draw_texture(BACKGROUND::DAN_RESULT_P1_BOARD, {});
    if (congrats_showing) {
        draw_congrats(now);
    } else if (celebrating) {
        draw_celebration(now);
    } else {
        draw_page1(now);
        draw_page2(page2_fade->attribute, now);
    }
    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height,
                       ray::Fade(ray::BLACK, (float)fade_out->attribute));
    coin_overlay.draw();
    allnet_indicator.draw();
}
