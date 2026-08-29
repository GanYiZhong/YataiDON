#include "game_dan.h"
#include <algorithm>
#include "../libs/input.h"
#include "../libs/script.h"

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
    song_info = SongInfo(current_song_title, first.genre_index - 1, 1, (int)sd.selected_dan.size());

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
    song_info = SongInfo(current_song_title, entry.genre_index - 1, song_index + 1, (int)sd.selected_dan.size());

    // ROUND 47 -- the cabinet closes the 襖 between the songs of a course and
    // reopens it on the next one (the enso_dani fusuma interstitial); this
    // engine cut straight from song N's last frame to song N+1's first with no
    // visual at all. The existing dan transition (the course-start loading
    // movie, already dan-styled via set_dan in on_screen_start) is re-armed
    // here as that interstitial: update()'s `transition->is_finished()` gate
    // then holds start_song() until it has played out, and the fresh start_ms
    // + start_delay lead-in below gives it exactly the same time budget the
    // course-start transition already proves sufficient.
    {
        const SessionData& sd_ro = global_data.session_data[(int)global_data.player_num];
        transition->set_dan(sd_ro.dan_color, sd_ro.song_title);
        transition->start();
    }
    // ROUND 17: `- audio_offset` was missing here but IS applied by the gate at
    // game.cpp:171 and by resync_song's target at game.cpp:307, so any non-zero
    // user offset desynced a dan run by exactly that many ms. game.cpp:75/:215
    // are the two matching lines on the ordinary game screen.
    start_ms = get_current_ms() - parser->metadata.offset * 1000 - (double)global_data.config->general.audio_offset;
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
            }
            // The big bar tracks the song being played, not the run.
            int cur = std::min(song_index, 2);
            info.counter_value = info.song_value[cur];
            info.progress      = info.song_progress[cur];
            info.bar_width     = bar_full_w * info.progress;
            if (info.progress >= 1.0f)      info.bar_texture = "exam_max";
            else if (info.progress >= 0.5f) info.bar_texture = "exam_gold";
            else                            info.bar_texture = "exam_red";
        }

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
            spdlog::info("Dan exam {} ({}) failed: {} < {} ({})", i, exam.type, val, exam.red,
                         exam.gothrough ? "gothrough" : "per-song");
        } else if (exam.range == "less") {
            int remaining = std::max(0, exam.red - val);
            if (remaining == 0) {
                exam_failed[i] = true;
                if (!exam.gothrough && i < (int)exam_song_failed.size() && song_index < 3)
                    exam_song_failed[i][song_index] = true;
                audio.play_sound("dan_failed", VolumePreset::SOUND);
                spdlog::info("Dan exam {} ({}) failed: {} of {} used up ({})", i, exam.type, val, exam.red,
                             exam.gothrough ? "gothrough" : "per-song");
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
    save_result_data(true);
    score_saved = true;
    spdlog::info("Dan course fail-out: song {}/{} at {} ms", song_index + 1,
                 (int)sd.selected_dan.size(), ms_from_start);
}

Screens DanGameScreen::on_screen_end(Screens next_screen) {
    dan_info_cache.reset();
    hori_name.reset();
    return GameScreen::on_screen_end(next_screen);
}

std::optional<Screens> DanGameScreen::update() {
    Screen::update();
    double current_ms = get_current_ms();
    allnet_indicator.update(current_ms);

    transition->update(current_ms);
    ms_from_start = current_ms - start_ms;
    //dan_transition.update(current_time);

    if (transition->is_finished() /*&& dan_transition.is_finished()*/)
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

        // ROUND 47 -- the cabinet interrupts the course the moment an exam
        // becomes unpassable (未満 cutoff busted mid-song, or a per-song 以上
        // quota missed at its own boundary): DaniResult.lua's g_unreach_ rows
        // and its `g_maxReachNum_ ~= 3 -> every exam fails` rule only exist
        // because of this early-out. The one case that does NOT divert is a
        // failure at the FINAL song's own end -- that is the natural course
        // end and takes the ordinary result flow below.
        const SessionData& sd_ro = global_data.session_data[(int)global_data.player_num];
        bool any_failed_now = std::any_of(exam_failed.begin(), exam_failed.end(),
                                          [](bool f) { return f; });
        bool final_song_over = song_index >= (int)sd_ro.selected_dan.size() - 1 &&
                               ms_from_start >= players[0]->end_time;
        if (any_failed_now && !final_song_over)
            trigger_fail_out(current_ms);

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
            bool boundary_failed = std::any_of(exam_failed.begin(), exam_failed.end(),
                                               [](bool f) { return f; });
            bool is_final_song = song_index >= (int)sd.selected_dan.size() - 1;
            if (boundary_failed && !is_final_song) {
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

    static const std::unordered_map<std::string, TexID> bar_ids = {
        {"exam_red",  DAN_INFO::EXAM_RED},
        {"exam_gold", DAN_INFO::EXAM_GOLD},
        {"exam_max",  DAN_INFO::EXAM_MAX},
    };
    // A failed condition is NOT a dimmed row on the cabinet: the bar goes to the
    // `fail` state (#404040, dani_enso_detail sprite 33 label `fail`) and the
    // live number is replaced by tx_fail. Nothing else changes.
    if (exam_failed[index]) {
        tex.draw_texture(DAN_INFO::EXAM_FAIL, {.y = y, .x2 = bar_full});
    } else {
        auto it = bar_ids.find(info.bar_texture);
        if (it != bar_ids.end())
            tex.draw_texture(it->second, {.y = y, .x2 = bar_full * info.progress});
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

    // Threshold: the cabinet writes "<N> 以上" as one right-aligned field whose
    // right edge is row-local x 378. Ours is digits + suffix, both right-aligned
    // to the same edge from texture.json, so nothing here depends on the value.
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
        draw_digit_counter(std::to_string(info.counter_value), value_margin,
                           DAN_INFO::VALUE_COUNTER, 0, y);
        if (info.exam_type == "gauge")
            tex.draw_texture(DAN_INFO::EXAM_PERCENT, {.y = y, .index = 1});
    }

    // Per-song sub-bars: one for every song ALREADY finished, in the order the
    // cabinet stacks them (upper = 1st song, lower = 2nd).
    if (!all) {
        const SkinInfo* sp = tex.skin_entry("dan_exam_sub_row");
        float sub_pitch  = sp ? sp->y : 50.0f;
        float sub_margin = sp ? sp->x : 20.0f;
        for (int j = 0; j < info.song_count && j < 2; j++) {
            float sy = y + j * sub_pitch;
            tex.draw_texture(DAN_INFO::EXAM_SUB_BG,    {.y = sy});
            tex.draw_texture(DAN_INFO::EXAM_SUB_TRACK, {.y = sy});
            const SkinInfo* sb = tex.skin_entry("dan_exam_sub_bar");
            float sub_full = sb ? sb->width : 234.0f;
            tex.draw_texture(info.song_progress[j] >= 1.0f ? DAN_INFO::EXAM_SUB_MAX
                             : info.song_progress[j] >= 0.5f ? DAN_INFO::EXAM_SUB_GOLD
                                                             : DAN_INFO::EXAM_SUB_RED,
                             {.y = sy, .x2 = sub_full * info.song_progress[j]});
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

    float offset_y = dan_exam_info().y;
    const auto& exams = sd.selected_dan_exam;
    for (int i = 0; i < (int)cache.exam_data.size(); i++) {
        if (i >= (int)exams.size()) break;
        draw_exam_row(cache.exam_data[i], exams[i], i, i * offset_y);
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
    if (players.size() == 1)
        players[0]->draw(ms_from_start, 0, 184 * tex.screen_scale, mask_shader);
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
// What a skip DOES on the cabinet is end that scene's own song and let the
// existing "song ended" flow take it from there -- for 段位道場 that flow is
// "advance to the next of the three songs, or finish the exam if this was the
// last one" (DanGameScreen::update()'s existing end-of-song branch), so a
// dan skip ends the CURRENT song only, never the whole exam outright. A
// skipped song's judgements are recorded honestly per round 17b's policy
// (every unreached note -> 不可), which for a non-gothrough ("01", per-song)
// exam condition on that song is a de facto fail of that song's condition
// -- exactly as strict as actually playing it badly would be, and nothing
// dan-specific has to special-case that: get_exam_progress_song() already
// reads Player::get_bad()/get_good()/get_ok() straight through. A gothrough
// (whole-run) condition keeps accumulating normally from the next song
// onward; there is no evidence the cabinet also zeroes the dan-wide soul
// gauge for a mid-course skip (that would be an unrelated, much harsher
// mechanism than the per-song record recount this round's evidence supports),
// so `dan_gauge` is deliberately left untouched here beyond simply not
// receiving further add_good()/add_ok() calls once the chart is cut.

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
    spdlog::info("Dan enso skipped (song {} of {}) at {} ms", song_index + 1,
                 global_data.session_data[(int)global_data.player_num].selected_dan.size(),
                 ms_from_start);
    if (song_music.has_value()) audio.stop_sound(song_music.value());
    if (movie.has_value()) movie->stop();
    // Cut THIS song only. The multi-song baseline (prev_good/prev_ok/prev_bad,
    // already tracked for the per-song "01" exam rows -- see the ROUND 19
    // comment on song_stats) keeps Player::cut_to_end's bad-note recount
    // scoped to this song instead of corrupting bads already banked by
    // earlier songs in the course. The existing "song ended" branch in
    // update() (ms_from_start >= players[0]->end_time) then does the rest --
    // advance to the next song, or finish the exam if this was the last one --
    // exactly like a song that ended on its own.
    if (players.size() == 1 && players[0])
        players[0]->cut_to_end(ms_from_start, prev_good, prev_ok, prev_bad);
}
