#include "game_dan.h"
#include <algorithm>
#include <cmath>
#include "../libs/input.h"
#include "../libs/script.h"
#include <cstdlib>

// ROUND 74 defect 1 -- temporary, opt-in phase probe for the exam-row rainbow.
// Off unless YATAIDON_R74_RB is set in the environment, so a normal run is
// bit-for-bit unaffected. It logs BOTH clocks at the draw site in one pass, so a
// single capture yields the per-frame phase advance for the frame-latched clock
// (what we now use) AND for the wall clock (what ROUND 67 used), and the two can
// be compared without a second build.
static const bool g_dan_rainbow_probe = std::getenv("YATAIDON_R74_RB") != nullptr;
// ROUND 78 -- the max-gate probe. A screenshot cannot tell "gated correctly"
// from "gated wrongly but sampled late", and the automation snapshot carries no
// note counts, so the gate logs its own inputs and outputs once per computed
// frame when YATAIDON_R78_GATE is set.
static const bool g_dan_gate_probe = std::getenv("YATAIDON_R78_GATE") != nullptr;

void DanGameScreen::on_screen_start() {
    Screen::on_screen_start();
    mask_shader   = load_shader("shader/dummy.vs", "shader/mask.fs");
    ms_from_start = 0;
    start_ms      = 0;
    // ROUND 17: GameScreen::on_screen_start resets this (game.cpp:15) and the dan
    // override did not. A stale value from the previous run skews `frame_delta`
    // (game.cpp:314) on the first corrected frame.
    last_resync_ms = 0;
    start_delay   = 4000.0;
    song_started  = false;
    paused        = false;
    score_saved   = false;
    pause_time    = 0;
    song_index    = 0;
    prev_good = prev_ok = prev_bad = prev_drumroll = 0;

    JudgePos::X = tex.skin_config[SC::JUDGE_POS].x;
    JudgePos::Y = tex.skin_config[SC::JUDGE_POS].y;

    if (global_data.config->general.nijiiro_notes) {
        tex.load_folder("game", "notes_nijiiro");
    }
    auto rainbow_mask = std::dynamic_pointer_cast<SingleTexture>(tex.textures[BALLOON::RAINBOW_MASK]);
    auto rainbow      = std::dynamic_pointer_cast<SingleTexture>(tex.textures[BALLOON::RAINBOW]);
    if (rainbow_mask && rainbow) {
        SetShaderValueTexture(mask_shader, GetShaderLocation(mask_shader, "texture0"), rainbow_mask->texture);
        SetShaderValueTexture(mask_shader, GetShaderLocation(mask_shader, "texture1"), rainbow->texture);
    }

    init_dan();
    load_hitsounds();

    // ROUND 17: the cabinet's own dan loading movie instead of the rainbow
    // curtain with an empty title band (see Transition::draw_dan).
    transition.emplace("", "", true);
    {
        const SessionData& _sd = global_data.session_data[(int)global_data.player_num];
        transition->set_dan(_sd.dan_color, _sd.song_title);
    }
    transition->start();
    //dan_transition = DanTransition();
    //dan_transition.start();

    result_transition = ResultTransition(PlayerNum::DAN);
    allnet_indicator  = AllNetIcon();
}

void DanGameScreen::init_dan() {
    SessionData& sd = global_data.session_data[(int)global_data.player_num];

    total_notes = 0;
    for (const auto& entry : sd.selected_dan) {
        try {
            SongParser sp(entry.song_path);
            auto [notes, bm, be, bn] = sp.notes_to_position(entry.difficulty);
            for (const Note& n : notes.notes)
                if (n.type >= NoteType::DON && n.type <= NoteType::KAT_L) total_notes++;
            for (auto& sec : bm)
                for (const Note& n : sec.notes)
                    if (n.type >= NoteType::DON && n.type <= NoteType::KAT_L) total_notes++;
        } catch (...) {}
    }
    if (total_notes == 0) total_notes = 1;

    dan_color = sd.dan_color;
    failed_out    = false;      // ROUND 47
    failed_out_at = 0.0;
    between.stop();             // ROUND 69 -- also the restart_key path
    exam_failed.assign(sd.selected_dan_exam.size(), false);
    // ROUND 32 (r32-audit-gamedan): per-song fail tracking for non-gothrough
    // exams, reset alongside exam_failed.
    exam_song_failed.assign(sd.selected_dan_exam.size(), {false, false, false});
    dan_info_cache.reset();
    song_max_combo = 0;         // ROUND 50: per-song g_maxComboNum_[j]

    // Set up gauge
    dan_gauge = Gauge(GaugeMode::DAN, global_data.player_num, total_notes);

    // Create player for first song
    const auto& first = sd.selected_dan[0];
    sd.selected_difficulty = first.difficulty;
    parser.emplace(first.song_path, (int)start_delay);
    if (fs::exists(parser->metadata.wave))
        song_music = audio.load_sound(parser->metadata.wave, "song");

    players.clear();
    players.push_back(std::make_unique<Player>(
        parser, global_data.player_num, first.difficulty, false,
        get_player_modifiers(global_data.player_num)));
    players[0]->set_is_dan(true);
    players[0]->gauge.reset();
    players[0]->dan_gauge = &dan_gauge;

    // ROUND 20 (r19-danskip): arm 演奏スキップ for song 1 of the course. Must
    // run AFTER players[0] exists -- init_skip()'s lane search reads
    // player->is_skip_enabled() over `players`.
    init_skip();

    bpm        = parser->metadata.bpm;
    scene_preset = parser->metadata.scene_preset;
    background.emplace(global_data.player_num, bpm, "DAN");

    const std::string& lang = global_data.config->general.language;
    std::string title = sd.song_title;
    hori_name = std::make_unique<OutlinedText>(title, tex.skin_config[SC::DAN_TITLE].font_size, ray::WHITE, ray::BLACK, false);

    current_song_title = parser->metadata.title.count(lang) ? parser->metadata.title.at(lang) : parser->metadata.title.at("en");
    // ROUND 17: the course length feeds the pill's "M曲" half (the cabinet's
    // `text_song_max`), so a dan run reads "1曲目 / 3曲" as on the cabinet.
    std::string subtitle = parser->metadata.subtitle.count(lang) ? parser->metadata.subtitle.at(lang) : "";
    song_info = SongInfo(current_song_title, subtitle, parser->metadata.subtitle_full_display, first.genre_index - 1, 1, (int)sd.selected_dan.size());

    // ROUND 17: `- audio_offset` was missing here but IS applied by the gate at
    // game.cpp:171 and by resync_song's target at game.cpp:307, so any non-zero
    // user offset desynced a dan run by exactly that many ms. game.cpp:75/:215
    // are the two matching lines on the ordinary game screen.
    start_ms = get_current_ms() - parser->metadata.offset * 1000 - (double)global_data.config->general.audio_offset;
}

void DanGameScreen::change_song() {
    SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const auto& entry = sd.selected_dan[song_index];
    sd.selected_difficulty = entry.difficulty;

    if (song_music.has_value()) {
        audio.stop_sound(song_music.value());
        song_music.reset();
    }

    parser.emplace(entry.song_path, (int)start_delay);
    if (fs::exists(parser->metadata.wave))
        song_music = audio.load_sound(parser->metadata.wave, "song");

    song_started = false;

    players[0]->reload_for_dan(parser, entry.difficulty);
    players[0]->dan_gauge = &dan_gauge;

    // ROUND 20 (r19-danskip): re-arm skip for the next song -- each dan song
    // is its own SceneEnsoGame-equivalent load on the cabinet, so the ten-hit
    // counter (and the option's live/gone state, in case the skin surfaces a
    // 2P-join-style toggle mid-course) starts fresh, exactly like init_skip()
    // already does for a normal enso's on_screen_start()/restart_song().
    init_skip();

    const std::string& lang = global_data.config->general.language;
    current_song_title = parser->metadata.title.count(lang) ? parser->metadata.title.at(lang) : parser->metadata.title.at("en");
    std::string subtitle = parser->metadata.subtitle.count(lang) ? parser->metadata.subtitle.at(lang) : "";
    song_info = SongInfo(current_song_title, subtitle, parser->metadata.subtitle_full_display, entry.genre_index - 1, song_index + 1, (int)sd.selected_dan.size());

    // ROUND 69 (r69-dan-song-transition) -- the REAL between-song interstitial,
    // and the resolution of ROUND 68b's open question.
    //
    // ROUND 47 re-armed the course-start `transition` here as a stand-in for the
    // missing 襖 beat, and said so in its own comment ("The existing dan
    // transition ... is re-armed here as that interstitial"). That stand-in is a
    // SECOND, REDUNDANT visual, not the same beat, and it is now removed:
    //
    //  * `transition->draw_dan` plays `enso_dani/loading_dani/loading_dani_song.nulm`
    //    -- a FULL-SCREEN night scene with the rank plaque, which `LoadingHelper`
    //    selects as the dojo's LOADING screen (ROUND 17). A course loads once.
    //  * the between-song beat is a different asset in a different tree:
    //    `enso_dani/enso/dani_enso/dani_enso_between.nulm`, a 1920x264 LANE STRIP,
    //    driven by `App::DojoEnsoGraphicFusuma::Process` -- an EnsoGraphic object
    //    that only exists while the enso scene is running. The cabinet never
    //    replays the loading movie mid-course.
    //
    // So: one visual, the right one. Full decode in Graphics/game/MAPPING_dan_info.md
    // ROUND 69 and in src/objects/game/dan_between.h.
    between.start(get_current_ms(), current_song_title, subtitle,
                  parser->metadata.subtitle_full_display);
    // ROUND 17: `- audio_offset` was missing here but IS applied by the gate at
    // game.cpp:171 and by resync_song's target at game.cpp:307, so any non-zero
    // user offset desynced a dan run by exactly that many ms. game.cpp:75/:215
    // are the two matching lines on the ordinary game screen.
    //
    // ROUND 69: `+ SONG_OPEN_MS`. The chart clock must not start until the fusuma
    // OPENS (the cabinet sets `EnsoData+95 = 1` on the same line as
    // goto_label("open"), DojoEnsoGraphicFusuma::Process:511-529). Without the
    // shift, `ms_from_start` would already be past `start_delay` by the time the
    // gate below let start_song() run, the music would come in ~2 s behind the
    // chart, resync_song would hard-slam the clock backwards, and every note in
    // the first two seconds of the next song would have been judged 不可 behind a
    // blacked-out lane. Shifting start_ms instead makes ms_from_start simply
    // negative for the length of the interstitial: nothing loads, nothing is
    // judged, and 0 lands exactly on the open.
    start_ms = get_current_ms() + DanBetween::SONG_OPEN_MS
             - parser->metadata.offset * 1000
             - (double)global_data.config->general.audio_offset;
}

void DanGameScreen::fill_unplayed_songs() {
    SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const std::string& lang = global_data.config->general.language;

    for (int i = (int)sd.dan_result_data.songs.size(); i < (int)sd.selected_dan.size(); i++) {
        const DanSongEntry& entry = sd.selected_dan[i];
        DanResultSong res;
        res.genre_index         = entry.genre_index;
        res.selected_difficulty = entry.difficulty;
        res.diff_level          = entry.level;

        // ROUND 47 -- an unplayed song is the cabinet's g_unreach_ row: ZERO
        // judgements plus an unreach flag (DaniResult.lua SetUnReach: a song
        // with good==ok==bad==0 is unreached, and DaniResultSongBase plays the
        // unreach presentation for it). The previous behaviour here fabricated
        // "bad = every note of the chart", which no cabinet path produces --
        // it also silently busted any 不可-未満 exam's displayed numbers on the
        // result screen. Title/genre/difficulty stay: the cabinet's unreach
        // board still names the song.
        res.unreached = true;
        res.hidden    = entry.hidden;   // ROUND 57: mask key for page 1
        try {
            SongParser sp(entry.song_path);
            const auto& titles = sp.metadata.title;
            res.song_title = titles.count(lang) ? titles.at(lang)
                           : titles.count("en")  ? titles.at("en")
                           : titles.empty()      ? "" : titles.begin()->second;
        } catch (...) {
            spdlog::warn("Dan result: could not read {}", entry.song_path.string());
        }

        sd.dan_result_data.songs.push_back(res);
    }
}

// `dan_exam_info` is ONE row pitch and ONE bar length shared by DAN_SELECT,
// GAME_DAN and DAN_RESULT, and the arcade wants three different pairs
// (132/159/154 and -/656/984). A skin may now give each screen its own entry --
// "dan_exam_info_game" here, "dan_exam_info_result" on DAN_RESULT -- and a skin
// that declares neither keeps the single shared key, unchanged.
const SkinInfo& DanGameScreen::dan_exam_info() {
    if (const SkinInfo* e = tex.skin_entry("dan_exam_info_game")) return *e;
    return tex.skin_config[SC::DAN_EXAM_INFO];
}

int DanGameScreen::get_exam_progress(const Exam& exam) {
    Player* p = players[0].get();
    float gauge_pct = (dan_gauge.gauge_max > 0)
        ? (dan_gauge.gauge_length / dan_gauge.gauge_max) * 100.0f : 0.0f;

    if (exam.type == "gauge")        return (int)gauge_pct;
    if (exam.type == "judgeperfect") return p->get_good();
    if (exam.type == "judgegood")    return p->get_ok();
    if (exam.type == "judgebad")     return p->get_bad();
    if (exam.type == "hit")          return p->get_good() + p->get_ok() + p->get_total_drumroll();
    if (exam.type == "score")        return p->get_score();
    if (exam.type == "combo")        return p->get_max_combo();
    // ROUND 47 -- 連打数. The arcade's odai clear type 6 (DaniResult.lua
    // SetOdaiNum: g_dramrollNum_) is its own condition type, distinct from
    // "hit" (type 8, good+ok+renda); this engine had never wired it even
    // though both screens already ship the `exam_roll` icon art.
    if (exam.type == "renda")        return p->get_total_drumroll();
    return 0;
}

// ROUND 19 -- the same measurement as get_exam_progress but restricted to one
// song of the course, which is what a NON-gothrough condition is scored on
// (39.06 DaniResult.lua SetOdaiNum: g_odaiType_[i] == 3 indexes g_goodNum_[j],
// g_okNum_[j], ... per song, while type 0 sums them).
// song_stats holds every finished song; the current song is measured live.
// Two of the seven types cannot be split this way and fall back to the run
// total, stated here rather than silently faked:
//   * "gauge" -- the soul gauge is one continuous value across the course.
//   * "combo" -- Player::max_combo is never reset per song, so there is no
//                per-song maximum to read (the cabinet has g_maxComboNum_[j]).
int DanGameScreen::get_exam_progress_song(const Exam& exam, int song_idx) {
    Player* p = players[0].get();
    int good, ok, bad, drum, score;
    if (song_idx < (int)song_stats.size()) {
        good  = song_stats[song_idx].good;  ok   = song_stats[song_idx].ok;
        bad   = song_stats[song_idx].bad;   drum = song_stats[song_idx].drumroll;
        score = song_stats[song_idx].score;
    } else if (song_idx == song_index) {
        good  = p->get_good() - prev_good;  ok   = p->get_ok() - prev_ok;
        bad   = p->get_bad()  - prev_bad;   drum = p->get_total_drumroll() - prev_drumroll;
        score = p->get_score() - prev_score;
    } else {
        return 0;                                   // not played yet
    }
    if (exam.type == "judgeperfect") return good;
    if (exam.type == "judgegood")    return ok;
    if (exam.type == "judgebad")     return bad;
    if (exam.type == "hit")          return good + ok + drum;
    if (exam.type == "score")        return score;
    if (exam.type == "renda")        return drum;   // ROUND 47
    // ROUND 50 -- per-song max combo, the cabinet's g_maxComboNum_[j]. The
    // ROUND 19 caveat ("Player::max_combo is never reset per song") is closed:
    // finished songs carry their own snapshot, the current one is polled live.
    if (exam.type == "combo") {
        if (song_idx < (int)song_stats.size()) return song_stats[song_idx].max_combo;
        if (song_idx == song_index)            return song_max_combo;
        return 0;
    }
    return get_exam_progress(exam);                 // gauge (course-wide by nature)
}

DanInfoCache DanGameScreen::calculate_dan_info() {
    DanInfoCache cache;
    Player* p = players[0].get();
    int used = p->get_good() + p->get_ok() + p->get_bad();
    cache.remaining_notes = std::max(0, total_notes - used);
    // ROUND 78 -- App::TaikoCorePlayer::IsNearEndSong (0x1401459B0) is
    // `total_onpu * 0.10f > remaining_onpu`; IsJustBeforeEndSong (0x140145AB0)
    // is the same with 0.05f. `total_notes` is this song's own don/ka count
    // (set in on_screen_start), which is the same population CountOnpuNum walks.
    const bool near_end        = (float)total_notes * 0.10f > (float)cache.remaining_notes;
    const bool just_before_end = (float)total_notes * 0.05f > (float)cache.remaining_notes;

    const auto& exams = global_data.session_data[(int)global_data.player_num].selected_dan_exam;
    for (int i = 0; i < (int)exams.size(); i++) {
        const Exam& exam = exams[i];
        DanExamInfo info;
        info.exam_type  = exam.type;
        info.exam_range = exam.range;
        info.red_value  = exam.red;

        int val = get_exam_progress(exam);
        float progress = (exam.red > 0) ? (float)val / exam.red : 0.0f;

        if (exam.range == "less") {
            progress = 1.0f - progress;
            info.counter_value = std::max(0, exam.red - val);
        } else {
            info.counter_value = std::max(0, val);
        }
        progress = std::max(0.0f, std::min(1.0f, progress));
        info.progress = progress;

        float bar_full_w = dan_exam_info().width;
        info.bar_width = bar_full_w * progress;

        if (progress >= 1.0f)        info.bar_texture = "exam_max";
        else if (progress >= 0.5f)   info.bar_texture = "exam_gold";
        else                          info.bar_texture = "exam_red";
        // ROUND 67 -- the cabinet's own six-state palette, off the RAW count
        // (`val`), not off the normalised progress: a 未満 row's remaining
        // allowance is not what CheckMax tests.
        // ROUND 78 -- the LIVE gate (CHN05 DojoEnsoGraphicNormaGage cases 3/4 +
        // TaikoCorePlayer::IsNearEndSong/IsJustBeforeEndSong). See
        // dan_bar_state() in global_data.h. A 未満 row is not promoted to the
        // rainbow until the song is nearly over.
        info.bar_state = dan_bar_state(exam, val, true, near_end, just_before_end);

        // ROUND 19 -- per-song breakdown for a non-gothrough condition. The
        // cabinet shows the CURRENT song in the big bar and every finished song
        // in a dimmed sub-bar, so both are collected here.
        info.gothrough  = exam.gothrough;
        info.song_count = std::min(song_index, 3);
        if (!exam.gothrough) {
            for (int j = 0; j < 3 && j <= song_index; j++) {
                int   sv = get_exam_progress_song(exam, j);
                float sp = (exam.red > 0) ? (float)sv / exam.red : 0.0f;
                if (exam.range == "less") { sp = 1.0f - sp; sv = std::max(0, exam.red - sv); }
                info.song_value[j]    = std::max(0, sv);
                info.song_progress[j] = std::max(0.0f, std::min(1.0f, sp));
                // Only the song being played is "live"; a finished song's row is
                // settled and takes the plain value test.
                const bool live_j = (j == song_index);
                info.song_state[j]    = dan_bar_state(exam, get_exam_progress_song(exam, j),
                                                      live_j, near_end, just_before_end);
            }
            // The big bar tracks the song being played, not the run.
            int cur = std::min(song_index, 2);
            info.counter_value = info.song_value[cur];
            info.progress      = info.song_progress[cur];
            info.bar_width     = bar_full_w * info.progress;
            if (info.progress >= 1.0f)      info.bar_texture = "exam_max";
            else if (info.progress >= 0.5f) info.bar_texture = "exam_gold";
            else                            info.bar_texture = "exam_red";
            info.bar_state = info.song_state[cur];
        }

        if (g_dan_gate_probe)
            spdlog::warn("[r78gate] total={} remaining={} frac={:.4f} near={} "
                         "just={} exam={} type={} range={} red={} gold={} "
                         "val={} state={}",
                         total_notes, cache.remaining_notes,
                         (double)cache.remaining_notes / (double)std::max(1, total_notes),
                         near_end, just_before_end, i, exam.type, exam.range,
                         exam.red, exam.gold, val, info.bar_state);

        cache.exam_data.push_back(info);
    }
    return cache;
}

// ROUND 32 (r32-audit-gamedan): a non-gothrough ("01") exam is a PER-SONG
// requirement -- 39.06's own DaniData.lua:SearchSetBestQuota keeps one
// bestscore_quota[idx][i] SLOT PER SONG i for `is_gothrough == false`
// conditions (song_score[i].good/bad/... for i = 1..arrival_song), rather
// than the single cumulative slot a gothrough condition uses. That per-song
// record only makes sense if each song's own count is independently judged
// against the border -- a condition that only needed "sum across all 3
// songs >= N" would have no reason to keep three separate best-score slots.
// Before this round, `get_exam_progress(exam)` -- the CUMULATIVE, never-
// reset-per-song Player counters (see the ROUND 19 comment on
// get_exam_progress_song, which already fixed the DISPLAYED value/progress
// for non-gothrough rows but left this function untouched) -- was used here
// for every exam regardless of scope, so a non-gothrough exam's PASS/FAIL
// determination silently used the course-wide total while its own bar and
// live counter (calculate_dan_info) showed a completely different,
// song-scoped number. E.g. a per-song "bad < 90" condition would fail the
// instant cumulative bad crossed 90 across songs 1+2+3 combined, even if no
// single song individually exceeded 90 -- the opposite of what the per-song
// UI next to it was displaying.
//
// Fix: for a non-gothrough exam, judge the CURRENT song's own count
// (get_exam_progress_song(exam, song_index), which already returns a live,
// per-song-reset value). "less" (未満) is still checked every frame, so a
// per-song cap is now busted (and the WHOLE exam fails, matching the
// existing sticky/global exam_failed[] semantics -- there is no shipped
// course or cabinet evidence of a "this song missed, the others still
// count" partial-credit model) the instant THIS song's own count exceeds
// it, not diluted by carry-over from earlier songs. "more" (以上) cannot be
// known to have failed mid-song (more hits could still come), so it is only
// judged at a BOUNDARY -- now the boundary of EACH song for a non-gothrough
// exam (`song_finished`), not only the boundary of the whole course
// (`course_finished`) as before.
//
// Not changed: gothrough exams keep the exact previous behaviour (cumulative
// value, "more" only checked at course_finished) -- this round found no
// evidence that behaviour was wrong, and the ROUND 19 ship note that "Player
// counters run cumulatively across the whole course, matching a whole-run
// condition" is correct for that case.
//
// Caveat, stated so it is not mistaken for a live-verified cabinet fact: no
// shipped `dan.json` sets `"gothrough": false` on any of this skin's 25
// courses (grepped `build/bin/Songs` this round -- zero matches), because
// 39.06 does not ship the per-course exam table locally (Cabinet.DaniInfo is
// server-only, per ROUND 19's own note) and this project has never had real
// data to justify authoring a per-song condition on any specific course.
// This fix is therefore a code-correctness fix for a real internal
// inconsistency (the display and the fail-check disagreeing on what "this
// exam's value" even means), not a re-measurement against a captured
// cabinet frame -- there is no such frame to capture, since no non-gothrough
// exam has ever shipped. Verified this round with a temporary local-only
// test course (reverted after use, not shipped) -- see MAPPING_dan_info.md
// ROUND 32.
void DanGameScreen::check_exam_failures(bool course_finished, bool song_finished) {
    if (!dan_info_cache.has_value()) return;
    const auto& exams = global_data.session_data[(int)global_data.player_num].selected_dan_exam;
    for (int i = 0; i < (int)exams.size(); i++) {
        if (exam_failed[i]) continue;
        const Exam& exam = exams[i];
        int val = exam.gothrough ? get_exam_progress(exam)
                                  : get_exam_progress_song(exam, song_index);
        bool at_boundary = exam.gothrough ? course_finished
                                           : (song_finished || course_finished);

        if (exam.range == "more" && !at_boundary) continue;

        if (exam.range == "more" && val < exam.red) {
            exam_failed[i] = true;
            if (!exam.gothrough && i < (int)exam_song_failed.size() && song_index < 3)
                exam_song_failed[i][song_index] = true;
            audio.play_sound("exam_failed", VolumePreset::SOUND);
            spdlog::info("Dan exam {} ({}) failed: {} < {} ({}) at {} ms (song ends {} ms)",
                         i, exam.type, val, exam.red,
                         exam.gothrough ? "gothrough" : "per-song",
                         ms_from_start, players.empty() || !players[0] ? -1.0 : players[0]->end_time);
        } else if (exam.range == "less") {
            int remaining = std::max(0, exam.red - val);
            if (remaining == 0) {
                exam_failed[i] = true;
                if (!exam.gothrough && i < (int)exam_song_failed.size() && song_index < 3)
                    exam_song_failed[i][song_index] = true;
                audio.play_sound("dan_failed", VolumePreset::SOUND);
                spdlog::info("Dan exam {} ({}) failed: {} of {} used up ({}) at {} ms (song ends {} ms)",
                             i, exam.type, val, exam.red,
                             exam.gothrough ? "gothrough" : "per-song",
                             ms_from_start, players.empty() || !players[0] ? -1.0 : players[0]->end_time);
            }
        }
    }
}

// ROUND 47 (r47-dani-full-replication) -- the arcade's per-exam verdict
// arithmetic, straight from 39.06 script_lua/dani_result/DaniResult.lua
// CheckResultDetail (the authoritative evaluator: the cabinet re-derives the
// verdict on DAN_RESULT from raw counts + the two borders):
//   more (g_odaiGaugeType_ == 0): border <= num -> pass; goodBorder <= num -> GOLD
//   less (g_odaiGaugeType_ == 1): num < border  -> pass; num < goodBorder  -> GOLD
// A course that never authored a real gold border (dan.json "value" second
// element 0, or equal to red) can only ever be red.
int DanGameScreen::exam_tier(const Exam& exam, int value) {
    const bool has_gold = exam.gold > 0 && exam.gold != exam.red;
    if (exam.range == "less") {
        if (value >= exam.red) return 0;
        return has_gold && value < exam.gold ? 2 : 1;
    }
    if (value < exam.red) return 0;
    return has_gold && value >= exam.gold ? 2 : 1;
}

// ROUND 47 -- one writer for sd.dan_result_data, shared by the natural course
// end and the fail-out path so both produce the same record shape. Also
// computes what the cabinet computes on the result screen:
//   * per-exam tier (gothrough: the course total; per-song: the arcade's
//     all-three rule -- all songs gold -> gold, all at least red -> red,
//     any short -> fail; a fail-out forces every tier to 0, DaniResult.lua's
//     `if g_maxReachNum_ ~= 3 then return 0`),
//   * the overall g_odaiResult_ 0..6 (SetOdaiResult): all-gold `check==2`
//     splits 4/5/6 on course 可+不可 totals, all-red `check==1` splits 1/2/3,
//     and a 演奏スキップ run is forced to 0 (SetUnReach's g_isSkip_ rule).
void DanGameScreen::save_result_data(bool all_failed) {
    SessionData& sd = global_data.session_data[(int)global_data.player_num];
    const int course_songs = std::min((int)sd.selected_dan.size(), 3);

    sd.dan_result_data.dan_color    = dan_color;
    sd.dan_result_data.dan_rank     = sd.dan_rank;
    sd.dan_result_data.dan_index    = sd.dan_index;      // ROUND 50
    sd.dan_result_data.dan_index_max = sd.dan_index_max; // ROUND 57
    sd.dan_result_data.is_gaiden     = sd.dan_gaiden;    // ROUND 57
    sd.dan_result_data.dan_title    = sd.song_title;
    sd.dan_result_data.score        = players[0]->get_score();
    sd.dan_result_data.gauge_length = dan_gauge.gauge_length;
    sd.dan_result_data.max_combo    = players[0]->get_max_combo();
    sd.dan_result_data.exams        = sd.selected_dan_exam;

    sd.dan_result_data.exam_data.clear();
    int check = 2;                              // min tier over every exam
    const auto& exams = sd.selected_dan_exam;
    if (dan_info_cache.has_value()) {
        for (int i = 0; i < (int)dan_info_cache->exam_data.size(); i++) {
            DanResultExam re;
            re.progress      = dan_info_cache->exam_data[i].progress;
            re.counter_value = dan_info_cache->exam_data[i].counter_value;
            re.bar_texture   = dan_info_cache->exam_data[i].bar_texture;
            re.bar_state     = dan_info_cache->exam_data[i].bar_state;   // ROUND 67
            for (int j = 0; j < 3; j++)
                re.song_state[j] = dan_info_cache->exam_data[i].song_state[j];
            re.failed        = i < (int)exam_failed.size() && exam_failed[i];
            re.song_count    = dan_info_cache->exam_data[i].song_count;
            for (int j = 0; j < 3; j++) {
                re.song_value[j]    = dan_info_cache->exam_data[i].song_value[j];
                re.song_progress[j] = dan_info_cache->exam_data[i].song_progress[j];
            }
            if (i < (int)exams.size()) {
                const Exam& exam = exams[i];
                if (all_failed) {
                    re.tier = 0;
                } else if (exam.gothrough) {
                    re.tier = exam_tier(exam, get_exam_progress(exam));
                } else {
                    re.tier = 2;
                    for (int j = 0; j < course_songs; j++)
                        re.tier = std::min(re.tier, exam_tier(exam, get_exam_progress_song(exam, j)));
                }
                if (re.tier == 0) re.failed = true;
                // ROUND 67 -- the settled bar colours, off the RAW course /
                // per-song counts (dan_bar_state()); the in-play cache only
                // filled the slots for songs already started.
                re.bar_state = dan_bar_state(exam, get_exam_progress(exam));
                if (!exam.gothrough)
                    for (int j = 0; j < course_songs; j++)
                        re.song_state[j] = dan_bar_state(exam, get_exam_progress_song(exam, j));
            }
            check = std::min(check, re.tier);
            sd.dan_result_data.exam_data.push_back(re);
        }
    }
    if (exams.empty()) check = 0;               // a course with no exams cannot pass

    fill_unplayed_songs();

    int total_ok = 0, total_bad = 0;
    for (const auto& s : sd.dan_result_data.songs) { total_ok += s.ok; total_bad += s.bad; }

    int result;
    if (check == 2)      result = total_bad == 0 ? (total_ok == 0 ? 6 : 5) : 4;
    else if (check == 1) result = total_bad == 0 ? (total_ok == 0 ? 3 : 2) : 1;
    else                 result = 0;
    if (skipped) result = 0;                    // g_isSkip_ -> g_odaiResult_ = 0
    sd.dan_result_data.odai_result = result;
    spdlog::info("Dan course verdict: check={} ok={} bad={} odai_result={}{}",
                 check, total_ok, total_bad, result, all_failed ? " (fail-out)" : "");
}

// ROUND 47 -- the cabinet's course interruption. Judgement counts freeze at
// the bust value (update() stops feeding the Player), the music stops, the
// remaining songs become unreach rows, and the result ribbon follows shortly.
void DanGameScreen::trigger_fail_out(double current_ms) {
    failed_out    = true;
    failed_out_at = current_ms;
    if (song_music.has_value()) { audio.stop_sound(*song_music); song_music.reset(); }
    if (movie.has_value()) movie->stop();
    // ROUND 69 -- ROUND 68b's hand-off note: `update_skip_dan()` is not gated on
    // the interstitial, so a 演奏スキップ can land while the between-song clip is
    // still playing. Killing it here (the same line that already stops the movie)
    // is what stops the doors being left mid-slide under the result ribbon.
    between.stop();

    SessionData& sd = global_data.session_data[(int)global_data.player_num];
    if ((int)sd.dan_result_data.songs.size() <= song_index) {
        DanResultSong song_res;
        song_res.song_title          = current_song_title;
        song_res.genre_index         = sd.selected_dan[song_index].genre_index;
        song_res.selected_difficulty = sd.selected_dan[song_index].difficulty;
        song_res.diff_level          = sd.selected_dan[song_index].level;
        song_res.good     = players[0]->get_good()           - prev_good;
        song_res.ok       = players[0]->get_ok()             - prev_ok;
        song_res.bad      = players[0]->get_bad()            - prev_bad;
        song_res.drumroll = players[0]->get_total_drumroll() - prev_drumroll;
        sd.dan_result_data.songs.push_back(song_res);
    }
    // ROUND 68 -- was hardcoded `true`. `all_failed` is DaniResult.lua's
    // `if g_maxReachNum_ ~= 3 then return 0` (ROUND 47), i.e. "not every song of
    // the course was reached", so derive it instead of asserting it. ROUND 87:
    // after the mid-song exit was deleted there are exactly TWO callers -- the
    // song-BOUNDARY exam failure (CheckEnsoEnd state 6), which by construction
    // is only ever a NON-final song and so still yields `true` exactly as
    // before, and ROUND 68's dan skip (state 4), which can also fire ON the
    // final song -- there every song HAS been reached, so the per-exam tiers stay
    // honest while save_result_data()'s g_isSkip_ rule still forces the overall
    // verdict to 0.
    const bool reached_every_song = song_index >= (int)sd.selected_dan.size() - 1;
    save_result_data(!reached_every_song);
    score_saved = true;
    spdlog::info("Dan course ended early ({}): song {}/{} at {} ms",
                 skipped ? "skip" : "fail-out", song_index + 1,
                 (int)sd.selected_dan.size(), ms_from_start);
}

Screens DanGameScreen::on_screen_end(Screens next_screen) {
    dan_info_cache.reset();
    hori_name.reset();
    between.stop();   // ROUND 69 -- its OutlinedText must go before the font does
    exam_captions.clear();   // ROUND 70 -- same rule, same reason
    return GameScreen::on_screen_end(next_screen);
}

std::optional<Screens> DanGameScreen::update() {
    Screen::update();
    double current_ms = get_current_ms();
    allnet_indicator.update(current_ms);

    transition->update(current_ms);
    // ROUND 69 -- the between-song fusuma. Updated before the start_song() gate
    // because `song_may_start()` is what releases it from song 2 onwards.
    between.update(current_ms);
    ms_from_start = current_ms - start_ms;
    //dan_transition.update(current_time);

    // ROUND 69: `transition` is now ONLY the course-start loading movie (song 1);
    // `between.song_may_start()` is the cabinet's EnsoData+95, set on the same
    // line as the fusuma's `open` label, and is vacuously true whenever the
    // between-song clip is not running.
    if (transition->is_finished() && between.song_may_start() /*&& dan_transition.is_finished()*/)
        start_song(ms_from_start);

    update_background(current_ms);

    // ROUND 17 -- was `resync_song(ms_from_start)`. `GameScreen::resync_song`
    // takes the ABSOLUTE clock: it ends with `last_resync_ms = current_ms;
    // start_ms = current_ms - ms_from_start;` (game.cpp:318-319). Fed the
    // RELATIVE clock, start_ms collapses to ~0, so the next frame's
    // `ms_from_start = current_ms - start_ms` is the whole wall clock, the drift
    // blows past the 100 ms threshold (game.cpp:310) and the chart is hard-slammed
    // to raw `audio_ms_adjusted` EVERY frame instead of taking the smoothing
    // branch. The dan chart therefore rode raw audio-buffer jitter and notes
    // landed visibly early/late against a correctly-drawn judgement ring --
    // the user's 「判定的位置對不上」. `GameScreen::update` passes `current_ms`
    // (game.cpp:530); so does this now. (Traced by r18-play, verified here.)
    resync_song(current_ms);

    // ROUND 47 -- once the course has failed out, the run is frozen: the
    // Player stops updating (judgement counts and the exam bars keep their
    // bust-time values, as the cabinet freezes them when the fusuma closes)
    // and only the transitions keep running.
    if (!failed_out) {
        players[0]->update(ms_from_start, current_ms, background);
        // ROUND 50: the cabinet's per-song combo maximum (g_maxComboNum_[j]).
        song_max_combo = std::max(song_max_combo, players[0]->get_combo());
        song_info.update(current_ms);
        // ROUND 20 (r19-danskip): dan-aware skip poll (see game_dan.h for why
        // this can't just be the inherited update_skip()).
        update_skip_dan();
    }
    result_transition.update(current_ms);

    if (!failed_out) {
        dan_info_cache = calculate_dan_info();
        check_exam_failures();

        // ROUND 87 -- ROUND 47's mid-song `trigger_fail_out()` USED TO LIVE HERE
        // and it was WRONG. Its comment claimed "the cabinet interrupts the
        // course the moment an exam becomes unpassable"; the decompile says the
        // opposite. App::DojoEnsoGameManager::CheckEnsoEnd
        // (DojoEnsoGameManager.obj.c:1473) writes the course end-state
        // `ed+112` in exactly four places, and the 1/5/6 block that consults
        // the norma results (:1580-1598) sits entirely inside
        // `if (*(_BYTE*)(ed + 4332))` -- a flag raised ONLY at :1622-1627, once
        // `App::TaikoCorePlayer::IsAllOnpuEnd()` has held for 10 consecutive
        // frames, and cleared to 0 (:1631) on any frame it is false. So the
        // norma verdict is read at the song's NATURAL END and nowhere else:
        //   idx == 3 (final song)            -> state 1, ordinary completion,
        //                                       norma NOT consulted (:1583-1585)
        //   non-final + a norma slot == 1    -> state 6, course terminates
        //                                       WITHOUT advancing      (:1594)
        //   non-final + all norma ok         -> state 5, advance        (:1596)
        //   `ed+336 == 10` (演奏スキップ)     -> state 4, the ONLY mid-song cut
        //                                       (:1660-1663, KeyOff the music)
        // (`idx` is 1-based: :1692 reads the finished song at `4*idx - 4`.)
        // DaniResult.lua's g_unreach_ rows are a CONSEQUENCE of state 6 skipping
        // the remaining songs, not evidence of a mid-song bail-out -- ROUND 47
        // read that backwards and conflated "the run is failed" with "the song
        // is cut short". The user's report ("等到當前歌曲結束，再進result") is
        // state 6 exactly.
        //
        // `exam_failed[]` is sticky, so a failure detected here by
        // check_exam_failures() is still standing when the song reaches
        // `players[0]->end_time` below, where the boundary check
        // (the state-6 analogue) terminates the course. Nothing to do here but
        // let the song keep playing.
        push_dan_state();
    } else {
        // The fusuma-to-result beat: mirrors the skip path's short delay (the
        // cabinet plays no ending performance on a failed-out course).
        constexpr double FAILOUT_RESULT_DELAY = 2800.0;
        if (current_ms >= failed_out_at + FAILOUT_RESULT_DELAY && !result_transition.is_started) {
            result_transition.start();
            audio.play_sound("dan_transition", VolumePreset::VOICE);
        }
    }

    if (result_transition.is_finished && !audio.is_sound_playing("dan_transition"))
        return on_screen_end(Screens::DAN_RESULT);

    SessionData& sd = global_data.session_data[(int)global_data.player_num];

    if (!failed_out && ms_from_start >= players[0]->end_time) {
        // ROUND 47 -- judge the per-song (non-gothrough) "more" quotas at THIS
        // song's own boundary FIRST, before anything advances: a miss here on a
        // non-final song is a course interruption (the remaining songs become
        // unreach rows), not "play one more song and then stop", which is what
        // the pre-R47 else-branch placement produced. ROUND 32's
        // song_finished semantics are unchanged -- only the call site moved up.
        check_exam_failures(false, true);
        {
            // ROUND 87 -- this is now the ONE and ONLY exam-failure exit, and it
            // is CheckEnsoEnd's state 6 verbatim (DojoEnsoGameManager.obj.c:1594,
            // reached only once `ed+4332`/IsAllOnpuEnd has raised at the song's
            // natural end). `exam_failed[]` is sticky, so a condition that busted
            // MID-song is still true here -- the song played on to its end, which
            // is what the cabinet does and what the user asked for. `is_final_song`
            // is CheckEnsoEnd's `idx == 3` (:1583-1585): the final song's end is
            // state 1, ordinary completion, and never state 6.
            bool boundary_failed = std::any_of(exam_failed.begin(), exam_failed.end(),
                                               [](bool f) { return f; });
            bool is_final_song = song_index >= (int)sd.selected_dan.size() - 1;
            if (boundary_failed && !is_final_song) {
                spdlog::info("Dan course fails out at the END of song {}/{} "
                             "(state 6): song was played to its natural end at {} ms",
                             song_index + 1, (int)sd.selected_dan.size(), ms_from_start);
                trigger_fail_out(current_ms);
                return std::nullopt;
            }
        }

        // Save per-song result if not yet saved for this song
        if ((int)sd.dan_result_data.songs.size() <= song_index) {
            DanResultSong song_res;
            song_res.song_title         = current_song_title;
            song_res.genre_index        = sd.selected_dan[song_index].genre_index;
            song_res.selected_difficulty= sd.selected_dan[song_index].difficulty;
            song_res.diff_level         = sd.selected_dan[song_index].level;
            song_res.good    = players[0]->get_good()           - prev_good;
            song_res.ok      = players[0]->get_ok()             - prev_ok;
            song_res.bad     = players[0]->get_bad()            - prev_bad;
            song_res.drumroll= players[0]->get_total_drumroll() - prev_drumroll;
            sd.dan_result_data.songs.push_back(song_res);
        }

        bool any_failed = std::any_of(exam_failed.begin(), exam_failed.end(),
                                      [](bool failed) { return failed; });
        bool is_last = (song_index == (int)sd.selected_dan.size() - 1) || any_failed;
        if (is_last) {
            if (ms_from_start >= players[0]->end_time + 1000 && !score_saved) {
                // ROUND 47 -- the record fill (formerly inline here) moved to
                // save_result_data(), shared with the fail-out path, and now
                // also computes the per-exam tiers + the arcade's 0..6 verdict.
                check_exam_failures(true, true);
                save_result_data(false);
                players[0]->spawn_ending_anim();
                score_saved = true;
            }
            // ROUND 20 (r19-danskip): mirrors GameScreen::end_song()'s
            // SKIP_RESULT_DELAY (game.cpp, round 19/r19-skip2) -- a skipped
            // final song gets no ending performance (Player::spawn_ending_anim
            // already early-returns on was_skipped() above) so there is
            // nothing to wait 8533.34 ms for; only the skin's own skip panel
            // reveal (~2.25s at 60fps) needs room before the result ribbon.
            constexpr double SKIP_RESULT_DELAY = 2800.0;
            double transition_delay = players[0]->was_skipped() ? SKIP_RESULT_DELAY : 8533.34;
            if (ms_from_start >= players[0]->end_time + transition_delay && !result_transition.is_started) {
                result_transition.start();
                audio.play_sound("dan_transition", VolumePreset::VOICE);
            }
        } else {
            // ROUND 32's boundary check now runs at the TOP of this end-of-song
            // block (ROUND 47) so a per-song quota miss interrupts the course
            // before anything advances.

            // Advance to next song. ROUND 19: snapshot this song's own totals
            // first -- the cabinet's per-song ("01") condition rows need them.
            SongStats st;
            st.good     = players[0]->get_good()           - prev_good;
            st.ok       = players[0]->get_ok()             - prev_ok;
            st.bad      = players[0]->get_bad()            - prev_bad;
            st.drumroll = players[0]->get_total_drumroll() - prev_drumroll;
            st.score    = players[0]->get_score()          - prev_score;
            st.max_combo = song_max_combo;                 // ROUND 50
            if ((int)song_stats.size() <= song_index) song_stats.push_back(st);
            // A combo carried across the boundary keeps counting on the next
            // song's own counter (it is still alive there).
            song_max_combo = players[0]->get_combo();

            prev_good     = players[0]->get_good();
            prev_ok       = players[0]->get_ok();
            prev_bad      = players[0]->get_bad();
            prev_drumroll = players[0]->get_total_drumroll();
            prev_score    = players[0]->get_score();

            song_index++;
            song_started = false;
            change_song();
        }
    }

    // Global keys (back / restart)
    if (check_key_pressed(global_data.config->keys.back_key)) {
        if (song_music.has_value()) audio.stop_sound(song_music.value());
        return on_screen_end(Screens::DAN_SELECT);
    }
    if (check_key_pressed(global_data.config->keys.restart_key)) {
        if (song_music.has_value()) { audio.stop_sound(song_music.value()); song_music.reset(); }
        song_index = 0;
        prev_good = prev_ok = prev_bad = prev_drumroll = prev_score = 0;
        song_stats.clear();
        sd.dan_result_data = DanResultData();
        // The flag still says the music is on from before the restart, and
        // with it set the first song never gets its playback started.
        song_started = false;
        score_saved  = false;
        init_dan();
        audio.play_sound("restart", VolumePreset::SOUND);
    }

    return std::nullopt;
}

// Hands the live exam state to Background:handle_dan(player, state) so a skin
// can repaint the 段位道場 panel in draw_fore() -- the one Lua draw that runs
// after the whole GAME_DAN HUD. Costs one table build per frame ONLY when the
// skin's Background actually declares handle_dan; every other skin (PyTaikoGreen
// included) short-circuits on wants_dan() and pays a valid() check.
void DanGameScreen::push_dan_state() {
    if (!background.has_value() || !background->wants_dan()) return;
    if (!dan_info_cache.has_value()) return;

    sol::state& lua = *script_manager.lua;
    sol::table st   = lua.create_table();
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];

    st["dan_color"]       = dan_color;
    st["dan_title"]       = sd.song_title;
    st["song_index"]      = song_index;
    st["song_count"]      = (int)sd.selected_dan.size();
    st["total_notes"]     = total_notes;
    st["remaining_notes"] = dan_info_cache->remaining_notes;
    st["gauge"]           = dan_gauge.gauge_length;
    st["gauge_max"]       = dan_gauge.gauge_max;

    sol::table rows = lua.create_table();
    const auto& exams = sd.selected_dan_exam;
    for (int i = 0; i < (int)dan_info_cache->exam_data.size(); i++) {
        const DanExamInfo& info = dan_info_cache->exam_data[i];
        sol::table row = lua.create_table();
        row["type"]     = info.exam_type;
        row["range"]    = info.exam_range;
        row["red"]      = info.red_value;
        row["gold"]     = (i < (int)exams.size()) ? exams[i].gold : 0;
        row["value"]    = info.counter_value;
        row["progress"] = info.progress;
        row["bar"]      = info.bar_texture;
        row["failed"]   = (i < (int)exam_failed.size()) ? exam_failed[i] : false;
        rows[i + 1]     = row;
    }
    st["exams"] = rows;

    background->handle_dan(global_data.player_num, st);
}

void DanGameScreen::draw_digit_counter(const std::string& digits, float margin_x, TexID tex_id, int index, float y, float x_offset) {
    for (int j = 0; j < (int)digits.size(); j++) {
        float x = -(float)(digits.size() - j) * margin_x + x_offset;
        tex.draw_texture(tex_id, {.frame=digits[j]-'0', .x=x, .y=y, .index=index});
    }
}

// ROUND 19 -- one condition row, in the cabinet's own geometry.
//
// Source of every number: 39.06 enso_dani/enso/dani_enso/dani_enso_detail.nulm,
// sprite 239 (gage1/2/3) whose three frame labels are `all` / `01` / `none`.
// `all` is a whole-run condition (Exam::gothrough) and `01` a per-song one; the
// two differ in the badge, the bar length and whether sub-bars are drawn:
//
//                       all (gothrough)          01 (per-song)
//   badge  (30,62)      shape197  gold tomoe     shape105/188/192  1st/2nd/3rd
//   frame  (387/389,16) shape199/235  1008x128   shape5/34  680x128
//   fill   (405,37)     972x86                   648x86
//   sub-bars            none                     (1096,27) and (1096,77)
//
// Everything else (row plate 1560x160, the theme-name pill, the right-aligned
// threshold, the 80x88 live digits whose rightmost cell is at (744,24) pitch 56)
// is identical in both and lives in game/dan_info/texture.json.
void DanGameScreen::draw_exam_row(const DanExamInfo& info, const Exam& exam, int index, float y) {
    const SkinInfo& dei = dan_exam_info();
    // DAN_SCORE_BOX_MARGIN is DAN_RESULT.s big score-counter pitch; the dan
    // panel needs its own, so this is a by-name key with that as the fallback.
    const SkinInfo* bm = tex.skin_entry("dan_exam_border_margin");
    float border_margin = bm ? bm->x : tex.skin_config[SC::DAN_SCORE_BOX_MARGIN].x;
    // The live count uses the cabinet's 80x88 digit at pitch 56; the threshold
    // uses a 36 px text digit. Two different cells, so two different margins.
    const SkinInfo* vm = tex.skin_entry("dan_value_counter_margin");
    float value_margin = vm ? vm->x : border_margin;

    tex.draw_texture(DAN_INFO::EXAM_BG, {.y = y});
    // frame 0 = tomoe (whole run), 1..3 = 1st/2nd/3rd (the song being played).
    tex.draw_texture(DAN_INFO::EXAM_BADGE,
                     {.frame = info.gothrough ? 0 : 1 + std::min(song_index, 2), .y = y});

    const bool all = info.gothrough;
    tex.draw_texture(all ? DAN_INFO::EXAM_FRAME_BACK_ALL : DAN_INFO::EXAM_OVERLAY_1, {.y = y});

    // Bar length differs by scope, so `dan_exam_info.width` is the per-song 648
    // and the whole-run bar takes its own by-name key.
    const SkinInfo* wa = tex.skin_entry("dan_exam_bar_all");
    float bar_full = all && wa ? wa->width : dei.width;

    // ROUND 67 -- one fill draw, the cabinet's own six-state palette. See
    // dan_bar_state() (global_data.h) for the source; `max` is NOT "the exam is
    // met" but "the GOLD border is met", and it draws the scrolling rainbow
    // instead of a flat tint. The tile ships at two gradient periods so the
    // source rect can slide a whole period (80 frames = 1.333 s) without
    // wrapping.
    auto have = [&](TexID id) {
        return tex.textures.find((uint32_t)id) != tex.textures.end();
    };
    auto fill = [&](const std::string& state, float w, float fy, bool sub) {
        if (w <= 0 || state == "empty") return;
        TexID rb         = sub ? DAN_INFO::EXAM_SUB_RAINBOW : DAN_INFO::EXAM_RAINBOW;
        // ROUND 74 defect 1 -- the WHOLE-RUN row has its own rainbow, and it is
        // NOT the 656-period art ROUND 67 shipped. `dani_enso_detail` carries
        // two rainbow clips and ROUND 67 only found one of them:
        //   char  25 -- shapes 9/11/../23, the child of `score_gauge_bar_mc`
        //               char 33, which `1st/2nd/3rd` (char 196) uses: the
        //               648-wide PER-SONG bar. Period 656. Correct as shipped.
        //   char 233 -- `score_rainbow_mc`, shapes 201/203/../231, the child of
        //               `score_gauge_bar_mc` char 234, which is what
        //               `score_all_mc` (char 238, frame label `all`) carries.
        // Rendering char 233 (`lumen_shape_dump dani_enso_detail.nulm ... -m
        // -r 233 -f 0`) gives ink x 474..1445 = **972 wide**, y 497..582 = 86
        // tall: one whole gradient period exactly as wide as the 972 bar, the
        // same period DAN_RESULT uses. Sampling the 656 tile for a 972 bar was
        // both the wrong period AND an out-of-tile read -- see the split below.
        if (!sub && all && have(DAN_INFO::EXAM_RAINBOW_ALL))
            rb = DAN_INFO::EXAM_RAINBOW_ALL;
        const TexID d80  = sub ? DAN_INFO::EXAM_SUB_DOWN80  : DAN_INFO::EXAM_DOWN80;
        const TexID up50 = sub ? DAN_INFO::EXAM_SUB_RED     : DAN_INFO::EXAM_RED;
        const TexID up80 = sub ? DAN_INFO::EXAM_SUB_GOLD    : DAN_INFO::EXAM_GOLD;
        const TexID p100 = sub ? DAN_INFO::EXAM_SUB_MAX     : DAN_INFO::EXAM_MAX;
        // ROUND 90 -- CORRECTS ROUND 78's mapping (its GATE was right; only this
        // was wrong). ROUND 78 recorded "the cabinet's own max_soon/max_soon2
        // tints are NOT in the movie ... native code applies the tint", and on
        // that basis modelled max_soon -> up_80 and max_soon2 -> the rainbow.
        // Both halves are wrong, and the movie says so plainly once you walk it
        // with `lumen_anim_dump --all --leaves` instead of sampling the clip's
        // own colour transform:
        //
        //   dani_enso_detail sprite 33, per 20-frame state block, the base bar
        //   shape #7@0 cross-fades cmul 256 -> ~0 while cadd ramps to the state's
        //   colour; for down_100 (110), max_soon (130), max_soon2 (150) AND max
        //   (170) that colour is the SAME (256,162,183) = #FFA2B7, ROUND 67's
        //   pink. What differs between them is an EXTRA CHILD faded in on top:
        //     down_100  (110..129) : none                      -> flat pink
        //     max_soon  (130..149) : score_gauge_bar_type_blink_mc  (char 26)
        //     max_soon2 (150..169) : score_gauge_bar_type_blink2_mc (char 27)
        //     max       (170..189) : the rainbow strip             (char 25)
        //
        // i.e. the two "soon" states are PINK WITH A BLINK OVERLAY, and the
        // rainbow belongs to `max` alone -- which on a 未満 row is exactly the
        // user's 「快要達成金合格數值，應該是粉色底+一個閃爍樣式，直到段位結束才是
        // 顯示彩色」. ROUND 78's gate already refuses `max` in play, so the
        // rainbow now really does wait for the course to end (DAN_RESULT).
        //
        // NOT PORTED THIS ROUND, and honestly flagged: the blink overlays
        // themselves (chars 26/27, each with its own `type_up`/`type_down`
        // sub-labels) are real art this skin does not have. Until a later round
        // bakes them, max_soon/max_soon2 draw the flat pink with no blink --
        // right colour, missing animation. That is a KNOWN GAP, not a match.
        if (state == "max" && have(rb)) {
            auto it = tex.textures.find((uint32_t)rb);
            const float th   = (float)it->second->height;
            const float tw   = (float)it->second->width;
            const float perd = tw * 0.5f;
            // ROUND 74 defect 1 -- the phase MUST come from the frame-latched
            // clock, not from a fresh wall-clock read taken at draw time.
            // `get_current_ms()` is `high_resolution_clock::now()` (animation.h
            // :9); calling it here samples the scroll at whatever instant this
            // particular row's draw call happens to run, so the advance between
            // two presented frames carries the frame's whole CPU-time variance.
            // `get_frame_ms()` is `g_frame_ms`, latched once at the top of
            // run_frame() (YataiDON.cpp:362), and animation.h:18 says in as many
            // words that it exists "so render-time variance doesn't cause
            // jitter". Measured drift is in MAPPING_dan_info.md ROUND 74 s1.
            const double phase_ms = get_frame_ms();
            const float ph   = perd - (float)std::fmod(phase_ms / 1000.0
                                                       * (perd * 60.0 / 80.0), (double)perd);
            if (g_dan_rainbow_probe)
                spdlog::warn("[r74rb] frame_ms={:.4f} wall_ms={:.4f} perd={} ph={:.4f}",
                             phase_ms, get_current_ms(), perd, ph);
            // ROUND 74 defect 1, the actual "sticking pink" -- WRAP SAFETY.
            // `SetTextureWrap(..., TEXTURE_WRAP_CLAMP)` is applied to every
            // texture this engine loads (texture.h:100/119), so a source rect
            // that runs past the tile does NOT wrap: it repeats the last texel
            // column. With the 656-period tile on the 972 bar the window
            // [ph, ph+972] left the 1312-wide tile whenever ph > 340, i.e. for
            // 316 of every 656 phase units = 48 % of each 1.333 s loop, and the
            // frozen edge column is tile x 1311 = #FF519A -- PINK. That is the
            // user's "粉紅色看起來會卡住" exactly: a static pink block on the
            // right end of the bar for half the cycle, snapping back each loop.
            // Even with the correct 972 tile the window can only just reach the
            // end, so the draw is split rather than relying on the fit: the tile
            // is exactly two periods, so content past its right edge is the same
            // as content from x 0, and the two quads join seamlessly.
            const float avail = tw - ph;
            if (w <= avail) {
                tex.draw_texture(rb, {.y = fy, .x2 = w,
                                      .src = ray::Rectangle{ph, 0.0f, w, th}});
            } else {
                tex.draw_texture(rb, {.y = fy, .x2 = avail,
                                      .src = ray::Rectangle{ph, 0.0f, avail, th}});
                tex.draw_texture(rb, {.x = avail, .y = fy, .x2 = w - avail,
                                      .src = ray::Rectangle{0.0f, 0.0f, w - avail, th}});
            }
            return;
        }
        TexID id = p100;
        if (state == "up_50")           id = up50;
        else if (state == "up_80")      id = up80;
        // ROUND 90: both "soon" states are the down_100 PINK on the cabinet
        // (p100 is that pink), not the up_80 gold. The blink overlay that
        // distinguishes them from a flat down_100 row is not shipped -- see the
        // block above.
        else if (state == "max_soon")   id = p100;
        else if (state == "max_soon2")  id = p100;
        else if (state == "down_80")    id = have(d80) ? d80 : up80;
        tex.draw_texture(id, {.y = fy, .x2 = w});
    };
    // A failed condition is NOT a dimmed row on the cabinet: the bar goes to the
    // `fail` state (#404040, dani_enso_detail sprite 33 label `fail`) and the
    // live number is replaced by tx_fail. Nothing else changes.
    if (exam_failed[index]) {
        tex.draw_texture(DAN_INFO::EXAM_FAIL, {.y = y, .x2 = bar_full});
    } else {
        fill(info.bar_state, bar_full * info.progress, y, false);
    }

    tex.draw_texture(all ? DAN_INFO::EXAM_FRAME_FRONT_ALL : DAN_INFO::EXAM_OVERLAY_2, {.y = y});

    // Theme name, centred on the cream pill baked into the row plate.
    static const std::unordered_map<std::string, TexID> exam_ids = {
        {"gauge",        DAN_INFO::EXAM_GAUGE},
        {"combo",        DAN_INFO::EXAM_COMBO},
        {"hit",          DAN_INFO::EXAM_HIT},
        {"judgebad",     DAN_INFO::EXAM_JUDGEBAD},
        {"judgegood",    DAN_INFO::EXAM_JUDGEGOOD},
        {"judgeperfect", DAN_INFO::EXAM_JUDGEPERFECT},
        {"score",        DAN_INFO::EXAM_SCORE},
        {"renda",        DAN_INFO::EXAM_ROLL},       // ROUND 47
    };
    auto icon_it = exam_ids.find(info.exam_type);
    if (icon_it != exam_ids.end())
        tex.draw_texture(icon_it->second, {.y = y});

    // Threshold. The cabinet writes it as ONE right-aligned proportional text
    // field (`dani_enso_detail.nulm` char 186, size 36, align=1=RIGHT, border
    // 4.00, box right edge at row-local x 378 = screen 498); see
    // objects/game/exam_caption.h for the decode and the wordlist rows.
    //
    // ROUND 70 -- the sprite composition below is kept for a skin that never
    // declares `dan_exam_text_more` (PyTaikoGreen), and is bit-for-bit what it
    // was. The measured defect it caused on this skin (scratchpad/r70/m_cmp2.py,
    // row 0 of scratchpad/r67/shots/game_gold/game_02.png against the cabinet's
    // own render at the same screen origin):
    //
    //     ours    "50 % 以上"  ink x 306..497, runs 306-330,332-356,369-405,
    //                          427-497  -- 12 px and 21 px INTERNAL GAPS
    //     cabinet "50 ％以上"   ink x 331..502, runs 331-384,392-502
    //                          -- one 7 px gap, the format string's own space
    //
    // i.e. 25 px too wide, sprawling left toward the tomoe badge, which is the
    // reported 「xxx以上的位置偏了」.
    // Opt-in: the skin declares `dan_game_exam_border_text` with the field's
    // RIGHT edge in x, the glyph-origin top in y, `font_size` and `outline`.
    const SkinInfo* bt = tex.skin_entry("dan_game_exam_border_text");
    OutlinedText* cap = nullptr;
    float bt_ol = 4.0f / tex.screen_scale;
    if (bt) {
        if (bt->outline >= 0) bt_ol = bt->outline;
        cap = exam_captions.get(
            exam_threshold_text(tex, info.exam_type, info.exam_range,
                                info.red_value, global_data.config->general.language),
            bt->font_size > 0 ? bt->font_size : 36, bt_ol);
    }
    if (cap) {
        const float pad = ExamCaptionCache::pad_for(bt_ol, tex.screen_scale);
        cap->draw({.x = bt->x - cap->width + pad, .y = bt->y - pad + y});
    } else {
        if (info.exam_range == "less")      tex.draw_texture(DAN_INFO::EXAM_LESS, {.y = y});
        else if (info.exam_range == "more") tex.draw_texture(DAN_INFO::EXAM_MORE, {.y = y});

        float gauge_shift = 0.0f;
        if (info.exam_type == "gauge") {
            tex.draw_texture(DAN_INFO::EXAM_PERCENT, {.y = y, .index = 0});
            const SkinInfo* gs = tex.skin_entry("dan_exam_gauge_shift");
            gauge_shift = -(gs ? gs->x : border_margin);
        }
        draw_digit_counter(std::to_string(info.red_value), border_margin,
                           DAN_INFO::EXAM_BORDER_COUNTER, 0, y, gauge_shift);
    }

    // Live count, inside the dark track.
    //
    // ROUND 35 -- reverted ROUND 26's scope-dependent right-alignment.
    // This function's own opening comment (line ~623) already documents the
    // cabinet's measurement: the live digits' rightmost cell sits at
    // row-local (744,24), and is IDENTICAL for both the 972-wide `all` bar
    // and the 648-wide per-song bar -- it is a fixed position, not an anchor
    // relative to either bar's own right edge. `exam_bg` (the row plate) is
    // placed at absolute x 120 (texture.json), so row-local 744 is absolute
    // x 864 -- and the shipped `value_counter` texture.json x (920), run
    // through `draw_digit_counter`'s own -(digit_count)*margin formula for a
    // single digit, already lands exactly there (920 - 56 = 864). So the
    // ROUND 6-20 default was already geometrically correct and scope-blind
    // on purpose. ROUND 26 read a user report ("the value should be
    // right-aligned") as "flush against the bar's own right edge" and
    // computed a per-scope position from `bar_full`; for the 972-wide `all`
    // bar (used by every currently-shipped gothrough course) that pushed the
    // digits ~480px right of the cabinet's own fixed anchor, past the
    // visual centre of the bar -- which is exactly this round's own report
    // ("the number should sit between the label pill and the bar, not
    // embedded at the bar's trailing edge"). `EXAM_FAILED` is untouched: it
    // is one baked 360x96 PNG, already independently confirmed correct.
    // Full derivation: MAPPING_dan_info.md ROUND 35.
    if (exam_failed[index]) {
        tex.draw_texture(DAN_INFO::EXAM_FAILED, {.y = y});
    } else {
        // ── ROUND 64b -- 「當前數字要靠左」, settled from the cabinet's own data.
        //
        // This alignment has flip-flopped twice (ROUND 26 moved it, ROUND 35
        // reverted), both times argued from OUR geometry rather than the
        // cabinet's, so here is the primary source, once, in full.
        //
        // `dani_enso_detail.nulm` gives the live count a FIXED SEVEN-CELL digit
        // array, not a string: sprite 57 (`score_back_mc`'s inner clip) names
        // `digit1`..`digit7` at depths 0..6, and `lumen_shape_dump -m -r 239
        // -f 5 -c -v` places those seven cells at row-relative x
        //     +4, -52, -108, -164, -220, -276, -332
        // i.e. pitch 56 running RIGHT TO LEFT from digit1. The array therefore
        // spans -332 .. +4, and its LEFT edge is the cell at -332.
        //
        // Where that lands in our frame: ROUND 35 established the mapping from
        // this same clip -- the array's rightmost cell is row-local x 744, and
        // `exam_bg` is placed at absolute x 120, so row-local 744 is absolute
        // 864 (which is exactly what `value_counter` x 920 minus one 56 pitch
        // produces for a one-digit value). Row-local 744 is the +4 cell, so the
        // -332 cell is row-local 408 and absolute 408 + 120 = 528.
        //
        // 528 is the independent confirmation that settles it: the bar art
        // (`exam_max`/`exam_gold`/`exam_red`) is placed at absolute x 525, so
        // the digit array's left cell sits 3 px inside the bar's own left edge.
        // The cabinet fills that array from the left, which is why a one- or
        // two-digit live value reads flush against the bar's left end on a real
        // machine while ours -- filling from the +4 end -- sat mid-bar, 336 px
        // to the right. ROUND 35 was right that 864 is a fixed anchor and right
        // to reject ROUND 26's bar-relative recompute; what neither round had
        // was the other end of the array.
        //
        // Skin-gated (`dan_exam_value_left`), so PyTaikoGreen keeps the
        // right-aligned draw byte-for-byte.
        const SkinInfo* vl = tex.skin_entry("dan_exam_value_left");
        const std::string live = std::to_string(info.counter_value);
        if (vl) {
            auto it = tex.textures.find((uint32_t)DAN_INFO::VALUE_COUNTER);
            const float json_x = (it != tex.textures.end() && !it->second->x.empty())
                               ? it->second->x[0] : 920.0f;
            for (int j = 0; j < (int)live.size(); j++)
                tex.draw_texture(DAN_INFO::VALUE_COUNTER,
                                 {.frame = live[j] - '0',
                                  .x = vl->x + j * value_margin - json_x, .y = y});
            if (info.exam_type == "gauge") {
                auto pit = tex.textures.find((uint32_t)DAN_INFO::EXAM_PERCENT);
                const float pjson = (pit != tex.textures.end() && pit->second->x.size() > 1)
                                  ? pit->second->x[1] : 944.0f;
                tex.draw_texture(DAN_INFO::EXAM_PERCENT,
                                 {.x = vl->x + live.size() * value_margin - pjson,
                                  .y = y, .index = 1});
            }
        } else {
            draw_digit_counter(live, value_margin, DAN_INFO::VALUE_COUNTER, 0, y);
            if (info.exam_type == "gauge")
                tex.draw_texture(DAN_INFO::EXAM_PERCENT, {.y = y, .index = 1});
        }
    }

    // Per-song sub-bars: one for every song ALREADY finished, in the order the
    // cabinet stacks them (upper = 1st song, lower = 2nd).
    if (!all) {
        const SkinInfo* sp = tex.skin_entry("dan_exam_sub_row");
        float sub_pitch  = sp ? sp->y : 50.0f;
        float sub_margin = sp ? sp->x : 20.0f;
        // ROUND 64b -- 「如果一個條件是有分1st 2nd 3rd, 那應該要有圖二的灰色那一塊」.
        //
        // BOTH sub-rows are ALWAYS on the cabinet; an unreached one is a GREY
        // PLACEHOLDER (a grey filled circle + a grey rounded bar), not an
        // absence. Ours drew `j < info.song_count` only, so a course on its
        // first song showed one live row and empty space where the cabinet
        // shows two grey stubs -- the short, unbalanced block in the report.
        //
        // Source: `enso_dani/enso/dani_enso/dani_enso_detail.nulm` sprite 196
        // (`score_01_mc`, the per-song row variant) places `score_gauge_s1_mc`
        // at depth 11 and `score_gauge_s2_mc` at depth 8 UNCONDITIONALLY -- they
        // are part of the `01` composite, not something the script adds. Its
        // labels are init@0 / 1st@5 / 1st_end@10 / 2nd_start@75 / 2nd@90 /
        // 2nd_end@95 / 3rd_start@160 / 3rd@175, and rendering
        // `lumen_shape_dump -m -r 196 -f 5` (the `1st` state, i.e. song 1 of 3
        // in progress) shows both sub-rows as grey stubs, while `-f 175` (`3rd`)
        // shows the 1st and 2nd rows filled with their green/blue chips and pink
        // bars. `exam_sub_bg.png` -- already in the skin since ROUND 19, drawn
        // under every live row -- IS that grey stub, chip circle included, so no
        // new art was needed.
        for (int j = 0; j < 2; j++) {
            float sy = y + j * sub_pitch;
            tex.draw_texture(DAN_INFO::EXAM_SUB_BG,    {.y = sy});
            if (j >= info.song_count) continue;   // not reached yet: stub only
            tex.draw_texture(DAN_INFO::EXAM_SUB_TRACK, {.y = sy});
            const SkinInfo* sb = tex.skin_entry("dan_exam_sub_bar");
            float sub_full = sb ? sb->width : 234.0f;
            fill(info.song_state[j], sub_full * info.song_progress[j], sy, true);
            tex.draw_texture(DAN_INFO::EXAM_SUB_FRONT, {.y = sy});
            tex.draw_texture(DAN_INFO::EXAM_SUB_CHIP,  {.frame = j, .y = sy});
            draw_digit_counter(std::to_string(info.song_value[j]), sub_margin,
                               DAN_INFO::EXAM_SUB_COUNTER, 0, sy);
        }
    }
}

void DanGameScreen::draw_dan_info() {
    if (!dan_info_cache.has_value()) return;
    const DanInfoCache& cache = *dan_info_cache;
    const SessionData& sd = global_data.session_data[(int)global_data.player_num];

    // TOTAL_NOTES is also the panel field behind the rows (the only full-screen
    // draw available under the HUD) -- see Graphics/game/MAPPING_dan_info.md
    // ROUND 16 D -- so it stays drawn even though the remaining-notes digit
    // counter itself is skinned out for HSS-Zhong (r27-danremaining-notes,
    // user request: 段位道場不需要殘餘note數).
    tex.draw_texture(DAN_INFO::TOTAL_NOTES, {});
    // draw_digit_counter(std::to_string(cache.remaining_notes),
    //                    tex.skin_config[SC::DAN_TOTAL_NOTES_MARGIN].x,
    //                    DAN_INFO::TOTAL_NOTES_COUNTER, 0, 0);

    // ROUND 78 defect 4 -- the in-play condition block is THREE slots and it does
    // NOT carry the soul-gauge condition. Two independent facts from 39.06/CHN05:
    //
    //   * `dani_enso_detail.nulm` char 240 (the panel) places exactly three row
    //     instances of char 239 -- `gage1`, `gage2`, `gage3`
    //     (`lumen_anim_dump --list`, the `names` table). There is no fourth slot
    //     and no scroll.
    //   * `App::DojoEnsoGraphicNormaGage::CheckGageColor` (0x14010EEE0) opens
    //     with `if (a2 > 2) return;` -- the row index is 0..2 by construction,
    //     and so does `ChangeGageColor` (0x14010F180).
    //   * the soul-gauge condition belongs to a DIFFERENT graphic:
    //     `App::DojoEnsoGraphicTamashiiGage` is the one that searches
    //     `gauge_border` / `tx_border` and drives the `border_<N>` marker
    //     frames, i.e. the 「魂ゲージ NN％以上」 plate over the gauge. This engine
    //     already draws that plate, so a `gauge` row in the block is a DUPLICATE
    //     of the header AND the thing that pushed the fourth row off the panel.
    //
    // So the gauge condition is header-only ALWAYS, not just when the block
    // would overflow, and the block takes the first three non-gauge conditions.
    // The exam INDEX is kept for `exam_failed[]` and the per-exam state; only
    // the row SLOT is renumbered.
    float offset_y = dan_exam_info().y;
    const auto& exams = sd.selected_dan_exam;
    int slot = 0;
    for (int i = 0; i < (int)cache.exam_data.size(); i++) {
        if (i >= (int)exams.size()) break;
        if (exams[i].type == "gauge") continue;     // drawn on the gauge plate
        if (slot >= 3) break;                       // char 240 has three slots
        draw_exam_row(cache.exam_data[i], exams[i], i, slot * offset_y);
        slot++;
    }

    // The rank plaque. When the skin ships `rank_plate` and the course declares
    // a `rank_art` index, that art already carries the brush kanji AND the
    // romaji (39.06 com_dani_mc), so the generated course-name text is dropped.
    if (sd.dan_rank >= 0 && tex.skin_entry("dan_game_rank_plate")) {
        tex.draw_texture(DAN_INFO::RANK_PLATE, {.frame = sd.dan_rank});
    } else {
        tex.draw_texture(DAN_INFO::FRAME, {.frame = dan_color});
        if (hori_name) {
            SkinInfo hn = tex.skin_config[SC::DAN_GAME_HORI_NAME];
            hori_name->draw({
                .x = hn.x - hori_name->width / 2.0f,
                .y = hn.y,
                .x2 = std::min(hori_name->width, hn.width) - hori_name->width
            });
        }
    }
}

void DanGameScreen::draw() {
    if (background.has_value()) background->draw_back();
    draw_dan_info();
    dan_gauge.draw();
    // ROUND 90 (r90-danplate-brown-and-place): the 魂ゲージ condition header
    // plate is NOT a free-floating overlay -- on the cabinet it is `gauge_border`,
    // a child of `gauge_mc` INSIDE tamashii_gauge.nulm (sprite 146, depth 53), so
    // it is part of the soul gauge and shares the gauge's layer. 39.06's own
    // `datatable/enso_post.bin` target 2 (the dojo set) lists
    // `enso_dani/enso/tamashiigauge/tamashii_gauge` at draw-order row 10 of 29,
    // i.e. BELOW don_enso / lane_left / taiko / onp_jump / hit_effect / course /
    // score / combo_number / renda_number / song_info. The skin was painting it
    // from `Background:draw_fore()`, which runs AFTER the whole HUD *and* after
    // Player::draw()'s own draw_notes(), so the plate covered the notes -- the
    // user's report 「魂guage那個白色的景，不應該把音符蓋過去」.
    //
    // ROUND 80 already added `Background::draw_gauge(PlayerNum)` and glued it to
    // Player::draw()'s gauge slot for normal GAME. GAME_DAN never reached it,
    // because the dan gauge is `DanGameScreen::dan_gauge` drawn here and the
    // per-Player `gauge` optional is empty on this screen, so the ROUND 80 call
    // site inside `Player::draw()` never fires. Same hook, DAN's own gauge slot.
    // Fail-soft: a skin with no `draw_gauge` hook is not called (background.cpp
    // :165 checks `fn_draw_gauge.valid()`), so this changes nothing for skins
    // that do not define it.
    if (background.has_value()) background->draw_gauge(PlayerNum::P1);
    if (players.size() == 1)
        players[0]->draw(ms_from_start, 0, 184 * tex.screen_scale, mask_shader);
    // ROUND 69 -- the between-song fusuma sits ON the lane, so it draws straight
    // after the Player that owns that lane and with the SAME lane offset (it must
    // cover the notes; the cabinet's own quad is exactly the lane_background rect).
    between.draw(184 * tex.screen_scale);
    //dan_transition.draw();
    if (background.has_value()) background->draw_fore();
    draw_overlay();  // GameScreen::draw_overlay() already calls draw_skip() (game.h,
                      // generic, no dan-specific data needed) -- reused unmodified.
}

// --- 演奏スキップ for 段位道場 (ROUND 20, r19-danskip) ------------------------
// Whether the cabinet allows this at all was the open question this round
// started from. `EnsoGraphicEnsoSkip::Preparing` (CHN05 39.06 engine, see
// tlb_test_harness/research/enso_options.md §7 -- the authoritative pass on
// what every 演奏オプション row actually does) arms the feature purely off
// `PlayerSettings[+36] == 1` on the SAME `App::EnsoData::Settings` struct
// that every enso mode shares (Settings+88 carries the mode enum, "0 = normal,
// 1 = dojo/dan-i, ..."); the skip consumer never reads that mode field. There
// is exactly one SceneEnsoGame class for both normal enso and 段位道場 (no
// separate "SceneDaniGame" exists in the decompiled source tree), and
// SceneDaniSelect.obj.c itself reads `optionData_1_ensoSkip` into the same
// per-player settings block a dan run hands to that shared scene. So the
// option is a real, functional, per-player gameplay flag in 段位道場 on the
// real cabinet -- the same way the equivalent GameScreen comment's "2P: never"
// rule is real (round 17b, `song_select_option_selector.lua`'s GrayOut).
//
// The stale claim in game.cpp's init_skip() comment ("DanGameScreen ... is
// also the only case the arcade allows [1P plain enso]") was NOT backed by any
// dan-specific evidence when checked this round -- round 17b's citation is
// scoped to the NORMAL song-select option board, and no GrayOut/lockout tied
// to Settings+88's mode enum was found anywhere in EnsoGraphicEnsoSkip,
// EnsoInput::CountEnsoSkipWait or DaniScoreReceiver (which has zero "skip"
// references at all -- it just receives whatever score the shared game scene
// hands it, skip-adjusted or not, like any other song). That comment should be
// corrected by whoever next touches game.cpp; flagged here rather than edited
// there per this round's file-ownership split with r19-skip2.
//
// --- ROUND 68 (r68-dan-skip): what a skip DOES in 段位道場 -------------------
// ROUND 20 asserted, WITHOUT a dojo-side citation, that a dan skip "ends the
// CURRENT song only" and lets the ordinary end-of-song branch advance to the
// next song of the course. That is wrong, and the cabinet says so directly.
// The user's report --「他是一次跳過全部歌，進到段位結算」-- is the cabinet's
// actual behaviour: ONE skip ends the ENTIRE course and goes to 段位結果.
//
// PRIMARY SOURCE -- D:\tlb_test_harness\decompiled\src\DojoEnsoGameManager.obj.c
// (CHN05 build of the 39.06 engine), App::DojoEnsoGameManager::Process:
//
//   :1659  v15 = *((_QWORD *)this + 11);            // the shared EnsoData block
//   :1660  if ( *(_DWORD *)(v15 + 336) == 10 )      // <-- the SAME skip counter
//   :1662      *(_DWORD *)(v15 + 112) = 4;          //     enso state := 4 (SKIP)
//   :1663      nus3::HTone::KeyOff(...)             //     song music off
//   :1664      *((_DWORD *)this + 93) = 600;        //     end-wait preloaded to
//                                                   //     its terminal value
//
// `+336` is exactly the counter App::EnsoInput::CountEnsoSkipWait (EnsoInput.obj.c
// :855-925) drives: it increments only when the new rim side differs from the
// stored one (`if ( (_BYTE)a2 != *((_BYTE *)this + 76) )`), so the same rim twice
// is a NO-OP rather than a reset; a play-hit calls it with a3 = 1, which zeroes
// the counter UNLESS it has already reached 10 (`if ( *(_DWORD *)(v3 + 336) != 10 )`).
// That is the identical ten-alternating-rim rule GameScreen::poll_skip() already
// implements, and DojoEnsoGameManager reads the very same field -- so the TRIGGER
// is unchanged in dan mode. (There is no dojo-specific EnsoInput; DojoEnsoGameManager
// has zero "skip" symbols of its own, it only reads +336.)
//
// The decisive part is where state 4 lands. Immediately below, the same Process:
//
//   :1670  v17 = *(_DWORD *)(v15 + 112);
//   :1671  if ( v17 == 1 || (unsigned)(v17 - 4) <= 1 || v17 == 6 ) ++*((_DWORD*)this+92);
//   :1678  if ( *((int *)this + 92) >= 300 || (v18 = state) == 3 || v18 == 2 ) {
//   :1687      if ( v19 == 5 ) {                    // <-- ADVANCE-TO-NEXT-SONG
//   :1689          SaveFinishSongResult(this);
//   :1691          *((_DWORD *)this + 199) = v20 + 1;   //   ++song index
//   :1717          EnsoData::Settings::Settings(v87, v69 + 816 * songIndex);  // load next
//              ...  return;                         //     (state 5 = natural song end)
//   :1943      }
//   :1944      if ( v19 > 6 ) return;
//   :1945      v55 = 82;                            // 0b1010010 -> bits {1,4,6}
//   :1946      if ( !_bittest(&v55, v19) ) return;  // <-- state 4 arrives HERE
//   :1948      SaveFinishSongResult(this);          //     bank the CURRENT song
//   :1949      if ( *(_DWORD *)(shared + 112) == 4 ) {     // skip-only fixups
//   :1952          this[104*songIdx + 372] = 0;
//   :1957-1968      for (i = 0; i < songIdx; i++) <per-song crown> = 0;  // ALL songs
//   :1968          *((_DWORD *)this + 26 * songIdx + 86) = 0;
//   :1988          *(_DWORD *)((char*)this + 104*songIdx + 440) += *(_DWORD *)(shared + 320);
//              }
//   :2033      EnsoDataManager::UpdateDojoResults(...);    // publish the course record
//   :2038      *(_BYTE *)(shared + 108) = 1;              // scene finished -> DAN RESULT
//
// State 4 is NOT bit-tested into the `v19 == 5` advance branch; it falls into the
// terminating branch whose members are {1, 4, 6} and which ends with the
// scene-finished flag. So the course stops on the spot, the songs after it are
// never loaded (no SaveFinishSongResult ever runs for them -> zero judgements ->
// DaniResult.lua's SetUnReach g_unreach_ rows, exactly what fill_unplayed_songs()
// already produces), and the per-song CROWN array is additionally zeroed for every
// song already played (`v65 = 26*songIdx + 86` is read straight back out into the
// AppUsio cabinet-LED call at :2000-2028 with 1 = clear / 2,3 = better) -- the dojo
// twin of EnsoGameManager.obj.c:1337's `*(_DWORD *)v17 = 0` crown-clear.
//
// THIS song's own record is still kept honestly: EnsoGameManager.obj.c:1333-1355
// (the shared per-player record fill, already cited on Player::cut_to_end) charges
// every unreached note as 不可 -- `*((_DWORD *)v17 + 24) = v30 - v27 - v26`, i.e.
// BAD = total - GOOD - OK -- and zeroes the gauge and the crown. cut_to_end already
// does exactly that, and its ROUND 20 per-song baseline arguments (prev_good /
// prev_ok / prev_bad) still matter, because the dan Player's counters are
// cumulative across the course.
//
// So the implementation below is: cut this song's chart (unchanged), then take
// ROUND 47's trigger_fail_out() exit -- freeze the run, stop music/movie, bank the
// current song's row, fill the remaining songs as unreach, write the verdict, and
// start the result ribbon. ONE code path now owns "the course ended early".
// `dan_gauge` is still deliberately left untouched (no cabinet evidence zeroes the
// dan-wide soul gauge on a skip; the recount + the forced 0 verdict is the whole
// mechanism).
//
// The verdict: DaniResult.lua's g_isSkip_ rule already implemented in
// save_result_data() forces g_odaiResult_ = 0 for any skipped run, and a
// mid-course skip additionally leaves g_maxReachNum_ ~= 3, which is the rule
// ROUND 47 wired as `all_failed`. trigger_fail_out() now derives that flag from
// whether every song of the course was actually reached instead of hardcoding
// true, so a skip on the FINAL song (all songs reached) keeps its per-exam tiers
// honest while still landing on the forced-0 overall verdict. For every existing
// fail-out call site the derived value is `true`, exactly as before, because
// update() only ever fails out on a NON-final song.

void DanGameScreen::update_skip_dan() {
    if (skip_lane == PlayerNum::ALL) return;
    if (!skipped && !paused) poll_skip_dan();
    push_skip_state();  // inherited, generic (game.h/game.cpp) -- reused as-is.
}

// Verbatim copy of GameScreen::poll_skip() (game.cpp) up to the tenth-hit
// branch -- see game_dan.h for why the copy is unavoidable (that call is
// baked in non-virtually) -- redirecting only the final action to
// do_skip_dan().
void DanGameScreen::poll_skip_dan() {
    size_t play_hits = 0;
    for (const auto& player : players)
        if (player) play_hits += player->input_log.size();
    if (play_hits != skip_play_hits) {
        skip_play_hits = play_hits;
        if (skip_count > 0 && skip_count < SKIP_HITS) {
            skip_count = 0;
            skip_last  = -1;
            skip_text.reset();
        }
    }

    int value = -1;
    if      (is_l_kat_pressed(skip_lane)) value = 1;
    else if (is_r_kat_pressed(skip_lane)) value = 0;
    while (is_l_kat_pressed(skip_lane) || is_r_kat_pressed(skip_lane)) {}
    if (value < 0) return;

    if (skip_count == 0) {
        skip_count = 1;
        skip_last  = value;
    } else if (value != skip_last) {
        skip_count++;
        skip_last = value;
    } else {
        return;
    }

    const bool skin_draws = background.has_value() && background->wants_skip();
    const SkinInfo* pos = skin_draws ? nullptr : tex.skin_entry("skip_counter");
    if (pos) {
        skip_text = std::make_unique<OutlinedText>(
            std::to_string(skip_count) + "/" + std::to_string(SKIP_HITS),
            (int)(pos->font_size > 0 ? pos->font_size : 40),
            ray::WHITE, ray::BLACK, false, 4);
    }

    if (skip_count >= SKIP_HITS) do_skip_dan();
}

void DanGameScreen::do_skip_dan() {
    skipped = true;
    const bool skin_draws = background.has_value() && background->wants_skip();
    const SkinInfo* used = skin_draws ? nullptr : tex.skin_entry("skip_used_text");
    if (used) {
        skip_text = std::make_unique<OutlinedText>(
            tex.skin_text("skip_used", global_data.config->general.language, ""),
            (int)(used->font_size > 0 ? used->font_size : 40),
            ray::WHITE, ray::BLACK, false, 4);
    } else {
        skip_text.reset();
    }
    spdlog::info("Dan enso skipped on song {} of {} at {} ms -- ending the whole course",
                 song_index + 1,
                 global_data.session_data[(int)global_data.player_num].selected_dan.size(),
                 ms_from_start);
    // 1. Cut THIS song's chart. The multi-song baseline (prev_good/prev_ok/
    //    prev_bad, already tracked for the per-song "01" exam rows -- see the
    //    ROUND 19 comment on song_stats) keeps Player::cut_to_end's bad-note
    //    recount scoped to this song instead of corrupting bads already banked
    //    by earlier songs in the course. This mirrors the cabinet's shared
    //    record fill (EnsoGameManager.obj.c:1333-1355, state 4).
    if (players.size() == 1 && players[0])
        players[0]->cut_to_end(ms_from_start, prev_good, prev_ok, prev_bad);
    // 2. End the COURSE, not the song (ROUND 68 -- DojoEnsoGameManager.obj.c
    //    :1944-2038, the {1,4,6} terminating branch that state 4 falls into,
    //    NOT the state-5 advance branch). trigger_fail_out() is already exactly
    //    that exit: freeze the run, stop the music/movie, bank the current
    //    song's row, turn the rest of the course into unreach rows via
    //    save_result_data()/fill_unplayed_songs(), and start the result ribbon
    //    after 2800 ms -- the same beat the pre-ROUND-68 skip path used.
    trigger_fail_out(get_current_ms());
}
