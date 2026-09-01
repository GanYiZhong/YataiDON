#include "player.h"
#include "../../libs/audio.h"
#include "../../libs/input.h"
#include "../../libs/scores.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>  // r45: std::getenv for the gated judge-evidence log

// ROUND 45 verification-only escape hatch: YATAIDON_R45_DISABLE reverts every
// ROUND 45 mechanism port (stale-head look-ahead, gogo x1.2 score, kusudama
// 2P tick split) to the pre-round behaviour, so before/after evidence can be
// driven from ONE binary. Default off = arcade behaviour.
static bool r45_disabled() {
    static const bool v = std::getenv("YATAIDON_R45_DISABLE") != nullptr;
    return v;
}

// ROUND 55 verification-only escape hatch: YATAIDON_R55_DISABLE reverts every
// ROUND 55 note-pipeline port (1422 px travel span, per-frame draw-list drain,
// CHN05 roll-intensity accumulator, combo-state note-face animation, balloon
// squash pulse) to the pre-round PyTaikoGreen behaviour for one-binary A/B.
// Default off = arcade behaviour. CHN05 constants user-authorized (ROUND 45
// policy); sources cited at each site.
static bool r55_disabled() {
    static const bool v = std::getenv("YATAIDON_R55_DISABLE") != nullptr;
    return v;
}

// ROUND 55: the arcade note-ride travel span. CHN05 OnpuDraw::Process draws
// every lane object at X = judgeX + progress * 1422 * hiSpeed in the 1920-wide
// stage (decompiled/src/OnpuDraw.obj.c:5084/5150; data 0x140D4B538 = 1422.0f,
// verified by direct exe read; decompiled/specs/note_geometry.md section 0).
// progress==1.0 (spawn) therefore lands at 620+1422=2042, OFF the right edge --
// the span is NOT "screen width minus judge X" (PyTaikoGreen's rule, = 1302 px
// here), it is a fixed 1422 px. The 9.2% difference compresses the whole lane:
// every on-screen velocity and note-to-note pixel spacing was too small by
// 1422/1302 (the exact ratio note_judgement.md section 14.2 measured when the
// harness made the same fix). Judge X itself (618) stays the 39.06-measured
// layout value per the layout-vs-logic rule (MAPPING_notes.md).
static float r55_travel_span(float screen_width, float judge_x_pos) {
    if (r55_disabled()) return screen_width - judge_x_pos;   // pre-r55 rule
    return screen_width * (1422.0f / 1920.0f);
}

// ROUND 55: map the CHN05 roll-intensity accumulator (0..150) onto this
// engine's roll-head tint channel (255 = plain yellow, 0 = fully red). CHN05
// renders the intensity as the alpha of the yellow body over the red variant:
// DrawOnpParts alpha = 1 - 0.01*level, floored at 0 (OnpuDraw.obj.c:5905 /
// note_geometry.md section 3), i.e. redness saturates at level 100 and the
// 100..150 headroom only delays recovery. color = 255 * (1 - min(level,100)/100).
static int r55_roll_color(float intensity) {
    float redness = std::min(intensity, 100.0f) / 100.0f;
    return (int)std::lround(255.0f * (1.0f - redness));
}

Player::Player(std::optional<SongParser>& parser_ref, PlayerNum player_num_param, int difficulty_param,
       bool is_2p_param, const Modifiers& modifiers_param)
    : is_2p(is_2p_param)
    , is_dan(false)
    , player_num(player_num_param)
    , difficulty(difficulty_param)
    , visual_offset(global_data.config->general.visual_offset)
    , score_method(global_data.config->general.score_method)
    , modifiers(modifiers_param)
    , parser(parser_ref)
    , good_count(0)
    , ok_count(0)
    , bad_count(0)
    , combo(0)
    , score(0)
    , max_combo(0)
    , total_drumroll(0)
    , arc_points(25)
    , judge_x(0)
    , judge_y(0)
    , is_gogo_time(false)
    , autoplay_hit_side(Side::LEFT)
    , last_subdivision(-1)
    , combo_display(combo, 0)
    , score_counter(0, is_2p)
{
    // r45b verification-only escape hatch (config.toml is user-owned and this
    // machine runs shinuchi): force the GEN3 ladder so the gogo x1.2 path can
    // be exercised live without touching the user's config. Default off.
    if (std::getenv("YATAIDON_R45_FORCE_GEN3")) score_method = ScoreMethod::GEN3;
    reset_chart();
    don_hitsound = "hitsound_don_" + std::to_string((int)player_num) + "p";
    kat_hitsound = "hitsound_kat_" + std::to_string((int)player_num) + "p";

    std::string pnum = std::to_string((int)player_num);
    lane_cover_tex_id = tex.get_enum("lane/" + pnum + "p_lane_cover");
    lane_icon_tex_id  = tex.get_enum("lane/" + pnum + "p_icon");
    for (int t = 1; t <= 9; ++t) {
        auto it = tex_id_map.find("notes/" + std::to_string(t));
        note_tex_ids[t] = (it != tex_id_map.end()) ? it->second : TexID(0);
    }

    if (parser.has_value() && !parser->metadata.course_data.empty()) {
        if (parser->metadata.course_data[difficulty].is_branching) {
        branch_indicator = BranchIndicator();
        }
    }
    int player_id = get_player_id(player_num);
    auto pd = scores_manager.get_player_data(player_id);
    nameplate = Nameplate(
        pd ? pd->username : "", pd ? pd->title : "",
        global_data.player_num,
        pd ? pd->dan : -1, pd ? pd->gold : false, pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
    chara = make_chara_from_player_data(pd ? &*pd : nullptr);
    if (pd) {
        chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
        chara->apply_face(pd->chara_face_index);
    } else {
        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
    }
    chara->set_anim(AnimIndex::DON_NORMAL);
    // ROUND 19 (r19-gauge): `JudgeCounter::draw()` takes no position params at
    // all -- unlike every other HUD element in this file (gauge, nameplate,
    // auto_icon, ...), it always draws at its own fixed texture.json/skin_config
    // coordinates, with no is_2p offset. In a one-player game that is fine (one
    // instance, drawn once). In a two-player game BOTH players build their own
    // JudgeCounter, and the SECOND player's copy draws at the exact same fixed
    // spot as the first's -- which happens to be where 2P's own score digit and
    // drum badge live (parent PyTaikoGreen authored the panel assuming solo
    // play, at y=540 in this child's 1920x1080 canvas, i.e. exactly the top of
    // the 2P half). Confirmed by capture (`scratchpad/r19gg/twop_fullheight_leftcol.png`,
    // agent r19-gauge): 1P's judge_counter text ("たたけた率/良/可/不可") garbled
    // by 2P's own score number and a stray drum-icon fragment on top of it --
    // a small, oddly-placed graphic bleeding into the far-left HUD column,
    // matching a user report of "a small stray red block" there. Until the
    // panel is given a real 2P layout, only ever show ONE copy (the primary/1P
    // slot) instead of drawing a second one directly on top of 2P's own HUD.
    if (global_data.config->general.judge_counter && !is_2p) {
        judge_counter = JudgeCounter();
    }
}

ResultData Player::get_result_score() {
    ResultData result = ResultData();
    result.score = score;
    result.good = good_count;
    result.ok = ok_count;
    result.bad = bad_count;
    result.max_combo = max_combo;
    result.total_drumroll = total_drumroll;
    if (dan_gauge) result.gauge_length = dan_gauge->gauge_length;
    else if (gauge.has_value()) result.gauge_length = gauge->gauge_length;
    // *((_DWORD *)v17 + 6) = 0 in the same skip branch: the RECORD's gauge is
    // zero however full the live gauge was when the player skipped.
    if (skipped_run) result.gauge_length = 0.0f;
    return result;
}

void Player::spawn_ending_anim() {
    // ROUND 19 (r19-skip2): the cabinet shows NO ending performance at all on a
    // skipped run -- not clear, not fail, nothing. Traced through
    // `EnsoGameManager.obj.c`: `CheckEnsoEnd` sets state(+112)=4 and, in the SAME
    // branch, jams the elapsed-frame counter straight to its completion floor
    // (`*((_DWORD*)this + 93) = 840;`, :1255) the instant the tenth rim hit lands
    // (`*(_DWORD*)(v13+336) == 10`, :1251). That counter is exactly the gate
    // `EnsoGameManager::Process` (:706-728) waits on before it will let the scene
    // advance to `SceneEnsoResult` -- so it is already satisfied on this frame.
    // The only other condition left is `EnsoSound::IsEndSong()`, and `do_skip()`
    // already kills the song with `nus3::HTone::KeyOff(song, 0)` -- "no fade" is
    // the existing citation for that call -- so playback reports "ended"
    // essentially immediately too. Net effect: the scene transitions to the
    // result screen within a frame or two of the tenth hit, which leaves no time
    // for `FailAnimation`/`ClearAnimation`/`FCAnimation` (each several hundred ms
    // of chained tweens plus a voice line) to ever start. This is also why the
    // arcade skip banner itself plays no SE/voice (Round 12's finding) -- the
    // whole path is built to be silent and instant, not a small delay before a
    // normal ending. Do not spawn one; end_song() shortens its own transition
    // wait to match (game.cpp).
    if (skipped_run) {
        ending_anim.reset();
        return;
    }
    if (!gauge.has_value() && !dan_gauge) return;
    bool is_clear = dan_gauge ? dan_gauge->get_is_clear() : gauge->get_is_clear();
    if (!is_clear) {
        ending_anim = FailAnimation(is_2p);
    } else if (bad_count == 0) {
        // All-良 gets the ドンダフルコンボ variant if the skin provides it (FCAnimation
        // falls back to the plain フルコンボ art/sound otherwise).
        ending_anim = FCAnimation(is_2p, ok_count == 0);
    } else {
        ending_anim = ClearAnimation(is_2p);
    }
}

void Player::reload_for_dan(std::optional<SongParser>& new_parser, int new_difficulty) {
    parser = new_parser;
    difficulty = new_difficulty;

    don_notes.clear();
    kat_notes.clear();
    other_notes.clear();
    draw_note_list.clear();
    draw_note_buffer.clear();
    branch_m.clear();
    branch_e.clear();
    branch_n.clear();
    timeline.clear();
    timeline_buffer.clear();
    draw_judge_list.clear();

    gauge.reset();
    reset_chart();
    gauge.reset();  // reset_chart recreates gauge; discard it for dan mode

    // ROUND 20 (r19-danskip): `skipped_run` is a per-song flag on the cabinet
    // (each dan song is its own SceneEnsoGame load with its own state(+112)).
    // This Player object is reused across all three dan songs, so without a
    // reset here, skipping song 1 would leave `skipped_run` stuck true and
    // silently blank the course's ending performance (Player::spawn_ending_anim,
    // called once after the LAST song) even if songs 2-3 finished normally.
    // Judgement/bad-count consequences of an earlier skip are NOT undone here
    // -- those already-recorded 不可 stay cumulative in bad_count, exactly as
    // the cabinet keeps the record honest song-to-song. Only the flag that
    // gates "was the song that just ended cut short" resets.
    skipped_run = false;
}

void Player::handle_timeline(double ms_from_start) {
    if (timeline.empty()) return;
    TimelineObject timeline_object = timeline.front();
    if (ms_from_start > timeline_object.start_time) {
        timeline.pop_front();
        timeline_buffer.push_back(timeline_object);
    }

    for (int i = timeline_buffer.size() - 1; i >= 0; i--) {
        auto& timeline_object = timeline_buffer[i];
        handle_scroll_type_commands(ms_from_start, timeline_object, i);
        handle_bpmchange(ms_from_start, timeline_object, i);
        handle_judgeposition(ms_from_start, timeline_object, i);
        handle_gogotime(ms_from_start, timeline_object, i);
        handle_branch_param(ms_from_start, timeline_object, i);
        handle_lyric(ms_from_start, timeline_object, i);
        handle_section(ms_from_start, timeline_object, i);
    }
}

void Player::autoplay_manager(double ms_from_start, double current_ms, std::optional<Background>& background) {
    if (!modifiers.auto_play) return;

    double subdivision_in_ms;
    DrumType hit_type;
    if (is_drumroll || is_balloon) {
        if (bpm == 0) {
            subdivision_in_ms = 0;
        } else {
            subdivision_in_ms = static_cast<int>(ms_from_start / ((240000.0 / bpm) / 24.0));
        }
        // ROUND 76 (r76-gamedan-band-and-autoplay): env-gated trace for the
        // "AUTO stops hitting rolls/balloons from dan song 2" report. Prints
        // only while a roll/balloon is actually active, so it costs nothing on
        // a normal run. `sub` <= `last` here IS the bug signature: the
        // subdivision counter restarted with the new song's clock while
        // `last_subdivision` still carried song 1's final value.
        static const bool r76_trace = std::getenv("YATAIDON_R76_AUTO") != nullptr;
        if (r76_trace) {
            spdlog::info("[r76auto] ms_from_start={:.1f} bpm={:.3f} sub={:.0f} last={} "
                         "roll={} balloon={} fire={}",
                         ms_from_start, bpm, subdivision_in_ms, last_subdivision,
                         (int)is_drumroll, (int)is_balloon,
                         (int)(subdivision_in_ms > last_subdivision));
        }
        if (subdivision_in_ms > last_subdivision) {
            last_subdivision = subdivision_in_ms;
            hit_type = DrumType::DON;
            autoplay_hit_side = autoplay_hit_side == Side::LEFT ? Side::RIGHT : Side::LEFT;
            spawn_hit_effects(hit_type, autoplay_hit_side);
            audio.play_sound(don_hitsound, VolumePreset::HITSOUND);
            check_note(ms_from_start, hit_type, current_ms, background);
        }
    } else {
        // A big note is struck with both hands, like a player would, so light
        // up both sides and leave the alternating hand where it was for the
        // next single note. Drumrolls keep alternating even when big.
        auto autoplay_hit = [&](DrumType type, bool big) {
            if (big) {
                spawn_hit_effects(type, Side::LEFT);
                spawn_hit_effects(type, Side::RIGHT);
            } else {
                autoplay_hit_side = autoplay_hit_side == Side::LEFT ? Side::RIGHT : Side::LEFT;
                spawn_hit_effects(type, autoplay_hit_side);
            }
        };

        // These loops only make progress when check_note() consumes the note
        // they are looking at, and check_note() returns without consuming
        // anything once the frame is later than `hit_ms + bad_window`. Any
        // frame longer than that window - an automation `shot` costs ~330 ms
        // against a 108 ms BAD window, but a stall, a page fault or a driver
        // hiccup does it too - therefore used to spin here forever on the same
        // note: the render loop never came back, and spawn_hit_effects() /
        // play_sound() allocating per iteration took the process from 1 GB to
        // 5.7 GB of private memory in about a second. That is the whole of the
        // "repeated `shot` wedges the render loop" bug.
        //
        // Notes whose window has already closed are left to
        // play_note_manager(), which retires them through the normal miss path,
        // exactly as it does when a human player freezes. The size check below
        // is the belt-and-braces version of the same guarantee: if check_note()
        // ever returns without consuming for any other reason, the loop ends
        // instead of hanging.
        const double bad_window = (difficulty <= (int)Difficulty::NORMAL)
                                ? Timing::BAD_EASY : Timing::BAD;

        while (!don_notes.empty() && ms_from_start >= don_notes.front().hit_ms) {
            if (ms_from_start > don_notes.front().hit_ms + bad_window) break;
            const size_t remaining = don_notes.size();
            hit_type = DrumType::DON;
            autoplay_hit(hit_type, don_notes.front().type == NoteType::DON_L);
            audio.play_sound(don_hitsound, VolumePreset::HITSOUND);
            check_note(ms_from_start, hit_type, current_ms, background);
            last_note_hit = current_ms;
            if (don_notes.size() == remaining) break;
        }

        while (!kat_notes.empty() && ms_from_start >= kat_notes.front().hit_ms) {
            if (ms_from_start > kat_notes.front().hit_ms + bad_window) break;
            const size_t remaining = kat_notes.size();
            hit_type = DrumType::KAT;
            autoplay_hit(hit_type, kat_notes.front().type == NoteType::KAT_L);
            audio.play_sound(kat_hitsound, VolumePreset::HITSOUND);
            check_note(ms_from_start, hit_type, current_ms, background);
            if (kat_notes.size() == remaining) break;
        }
    }
}

void Player::merge_branch_section(const NoteList& branch_section, double current_ms) {
    draw_note_list.insert(draw_note_list.end(),
                          branch_section.notes.begin(),
                          branch_section.notes.end());

    std::sort(draw_note_list.begin(), draw_note_list.end(),
              [](const Note& a, const Note& b) { return a.load_ms < b.load_ms; });

    timeline.insert(timeline.begin(), branch_section.timeline.begin(), branch_section.timeline.end());

    std::sort(timeline.begin(), timeline.end(),
              [](const TimelineObject& a, const TimelineObject& b) { return a.start_time < b.start_time; });

    for (const auto& note : branch_section.notes) {

        if (note.type == NoteType::DON || note.type == NoteType::DON_L) {
            auto pos = std::lower_bound(don_notes.begin(), don_notes.end(), note,
                [](const auto& a, const auto& b) { return a.hit_ms < b.hit_ms; });
            don_notes.insert(pos, note);
        } else if (note.type == NoteType::KAT || note.type == NoteType::KAT_L) {
            auto pos = std::lower_bound(kat_notes.begin(), kat_notes.end(), note,
                [](const auto& a, const auto& b) { return a.hit_ms < b.hit_ms; });
            kat_notes.insert(pos, note);
        } else if (note.type != NoteType::BARLINE) {
            auto pos = std::lower_bound(other_notes.begin(), other_notes.end(), note,
                [](const auto& a, const auto& b) { return a.hit_ms < b.hit_ms; });
            other_notes.insert(pos, note);
        }
    }
}

void Player::evaluate_branch(double current_ms) {
    float e_req = std::get<0>(curr_branch_reqs);
    float m_req = std::get<1>(curr_branch_reqs);
    double branch_end_ms = std::get<2>(curr_branch_reqs);
    if (current_ms >= branch_end_ms) {
        is_branch = false;
        if (branch_condition == "p") {
            branch_p_count = branch_note_count != 0 ? std::max(std::min((int)((double)branch_p_count / branch_note_count * 100), 100), 0) : 0;
        } else if (branch_condition == "r") {
            branch_r_count = std::max(curr_drumroll_count, branch_r_count);
        }
        float count = branch_condition == "p" ? branch_p_count : branch_r_count;
        if (branch_indicator.has_value()) {
            spdlog::info("Branch set to {} based on conditions {}, {}, {}", branch_diff_to_string(branch_indicator->difficulty), count, e_req, m_req);
        }
        if (count >= e_req && count < m_req && e_req >= 0) {
            if (!branch_e.empty()) {
                merge_branch_section(branch_e.front(), current_ms);
                branch_e.pop_front();
                if (branch_indicator.has_value() and branch_indicator->difficulty != BranchDifficulty::EXPERT) {
                    if (branch_indicator->difficulty == BranchDifficulty::MASTER) {
                        branch_indicator->level_down(BranchDifficulty::EXPERT);
                    } else {
                        branch_indicator->level_up(BranchDifficulty::EXPERT);
                    }
                }
            }
            if (!branch_m.empty()) {
                branch_m.pop_front();
            }
            if (!branch_n.empty()) {
                branch_n.pop_front();
            }
        } else if (count >= m_req) {
            if (!branch_m.empty()) {
                merge_branch_section(branch_m.front(), current_ms);
                branch_m.pop_front();
                if (branch_indicator.has_value() and branch_indicator->difficulty != BranchDifficulty::MASTER) {
                    branch_indicator->level_up(BranchDifficulty::MASTER);
                }
            }
            if (!branch_n.empty()) {
                branch_n.pop_front();
            }
            if (!branch_e.empty()) {
                branch_e.pop_front();
            }
        } else {
            if (!branch_n.empty()) {
                merge_branch_section(branch_n.front(), current_ms);
                branch_n.erase(branch_n.begin());
                if (branch_indicator.has_value() and branch_indicator->difficulty != BranchDifficulty::NORMAL) {
                    branch_indicator->level_down(BranchDifficulty::NORMAL);
                }
            }
            if (!branch_m.empty()) {
                branch_m.pop_front();
            }
            if (!branch_e.empty()) {
                branch_e.pop_front();
            }
        }
        branch_p_count = 0;
        branch_r_count = 0;
        branch_note_count = 0;
    }
}

void Player::update(double ms_from_start, double current_ms, std::optional<Background>& background) {
    // ROUND 80 (r80-gauge-layering-recheck): cache the live Background so
    // Player::draw() can call its per-lane gauge-overlay hook at the RIGHT
    // POINT IN THE FRAME (immediately after this lane's Gauge::draw), instead
    // of the skin painting it from Background:draw_fore() -- the last Lua
    // paint of the whole GAME HUD. See MAPPING_hud ROUND 80 defect 1: on the
    // cabinet the soul gauge is ONE graphic (`tamashiigage/tamashii_gage`,
    // datatable/enso_post.bin target 0 row 12 of 33), drawn UNDER don_enso,
    // lane_left, taiko, onp_jump, hit_effect, course, score, combo_number and
    // the rest -- not on top of them. `draw()` takes no Background parameter
    // and giving it one would touch every caller (game.cpp / game_2p.cpp /
    // game_dan.cpp / game_practice.cpp, three of them owned by other agents
    // this session), so the pointer is latched here, in the function that
    // already receives it every frame. Never dangles: update() and draw() run
    // in the same frame from the same screen, and the pointer is only ever
    // read between them.
    bg_hook = background.has_value() ? &background.value() : nullptr;

    // Live counters for the automation `state` command (see global_data.h).
    // Data-out only; player 1 / the single player of a 1P game.
    // `is_2p` (not player_num) is the lane flag: a 1P game started on the right
    // seat carries player_num == P2 while still being the only/first player.
    if (!is_2p) {
        global_data.live_combo    = combo;
        global_data.live_score    = score;
        global_data.live_drumroll = total_drumroll;
        global_data.live_gogo     = is_gogo_time;
        // ROUND 52: live gauge data-out (see global_data.h).
        if (gauge.has_value()) {
            global_data.live_soul       = gauge->get_soul();
            global_data.live_is_clear   = gauge->get_is_clear();
            global_data.live_is_rainbow = gauge->get_is_rainbow();
        }
    }
    note_manager(ms_from_start, background);
    combo_display.update(current_ms, combo);
    if (combo_announce.has_value()) {
        combo_announce->update(current_ms);
    }
    drumroll_counter_manager(current_ms);
    balloon_counter_manager(current_ms);
    kusudama_counter_manager(current_ms);
    for (auto it = draw_judge_list.begin(); it != draw_judge_list.end(); ) {
        it->update(current_ms);
        if (it->is_finished()) {
            it = draw_judge_list.erase(it);
        } else {
            ++it;
        }
    }
    if (gogo_time.has_value()) {
        gogo_time->update(current_ms);
    }
    if (fireworks.has_value()) {
        fireworks->update(current_ms);
        if (fireworks->is_finished()) {
            fireworks.reset();
        }
    }
    if (lane_hit_effect.has_value()) {
        lane_hit_effect->update(current_ms);
        if (lane_hit_effect->is_finished()) {
            lane_hit_effect.reset();
        }
    }
    for (auto it = draw_drum_hit_list.begin(); it != draw_drum_hit_list.end(); ) {
        (*it)->update(current_ms);
        if ((*it)->is_finished()) {
            it = draw_drum_hit_list.erase(it);
        } else {
            ++it;
        }
    }
    handle_timeline(ms_from_start);
    if (delay_start.has_value() && delay_end.has_value()) {
        if (ms_from_start >= delay_end.value()) {
            double delay = delay_end.value() - delay_start.value();
            for (auto& note : draw_note_buffer) note.load_ms += delay;
            for (auto& note : draw_note_list) note.load_ms += delay;
            for (auto& note : barlines) note.load_ms += delay;
            delay_start.reset();
            delay_end.reset();
        }
    }

    for (auto it = draw_arc_list.begin(); it != draw_arc_list.end(); ) {
        it->update(current_ms);
        if (it->is_finished()) {
            NoteType note_type = it->note_type;
            bool is_big = it->is_big;
            it = draw_arc_list.erase(it);
            // ROUND 19 (r19-gauge): the cabinet's badge burst
            // (enso_normal/enso/onpu/onp_jump.nulm -- tamashi_effect_1p) is
            // ONE named movieclip instance (lumen --list shows a single
            // depth-0 child, not one spawned per note), so a fast renda
            // RESTARTS it on every landing instead of layering N independent
            // bursts. gauge_hit_effect used to push_back unconditionally, so
            // two arcs landing inside the ~800ms burst duration of each other
            // drew two overlapping instances at once (a fresh small/bright
            // one over an older, bigger/dimmer one still fading out) -- the
            // reported "two overlapping flowers" on the badge. Clearing
            // first before the push restarts the singleton the same way.
            gauge_hit_effect.clear();
            gauge_hit_effect.push_back(GaugeHitEffect(note_type, is_big, player_num == PlayerNum::P2));
        } else {
            ++it;
        }
    }

    for (auto it = gauge_hit_effect.begin(); it != gauge_hit_effect.end(); ) {
        it->update(current_ms);
        if (it->is_finished()) {
            it = gauge_hit_effect.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = base_score_list.begin(); it != base_score_list.end(); ) {
        it->update(current_ms);
        if (it->is_finished()) {
            it = base_score_list.erase(it);
            if (tex.options[SCO::DELAY_SCORE_ADDITION])
                score_counter.update_count(score);
        } else {
            ++it;
        }
    }
    if (!tex.options[SCO::DELAY_SCORE_ADDITION]) {
        score_counter.update_count(score);
    }
    score_counter.update(current_ms);
    autoplay_manager(ms_from_start, current_ms, background);
    handle_input(ms_from_start, current_ms, background);
    nameplate.update(current_ms);
    if (dan_gauge) {
        dan_gauge->update(current_ms);
    } else if (gauge.has_value()) {
        gauge->update(current_ms);
        if (background.has_value()) {
            background->handle_gauge(player_num, gauge->get_progress(), gauge->get_is_clear(), gauge->get_is_rainbow(),
                                     gauge->get_clear_progress(), gauge->get_flash_attribute());
        }
        // ROUND (r-chara3d-fullanim): AnimIndex::DON_FULL_GAGE was a dead
        // enum value (defined in chara_3d.h, never set anywhere) until this
        // round. Gauge::is_rainbow is the engine's existing "soul gauge is
        // at 100%" signal (gauge.cpp: `is_rainbow = (gauge_length ==
        // gauge_max)`), already driving the arcade rainbow-gauge visual via
        // handle_gauge() above -- it is the correct trigger for the mascot's
        // matching "gauge full" pose. Edge-triggered (fire once on
        // false->true) so it doesn't re-fight DON_SABI/DON_COMBO every
        // frame the gauge happens to sit at max; DON_SABI (gogo) and
        // DON_COMBO (10-combo ticks) still take priority on whichever frame
        // they themselves fire, matching this engine's existing
        // last-set_anim-wins pose model (see chara_3d.cpp set_anim()).
        bool gauge_full_now = gauge->get_is_rainbow();
        if (gauge_full_now && !was_gauge_full) {
            chara->set_anim(AnimIndex::DON_FULL_GAGE);
        }
        was_gauge_full = gauge_full_now;
    }
    if (judge_counter.has_value()) {
        judge_counter->update(good_count, ok_count, bad_count, total_drumroll);
    }
    if (branch_indicator.has_value()) {
        branch_indicator->update(current_ms);
    }
    if (ending_anim.has_value()) {
        std::visit([&current_ms](auto& anim) { anim.update(current_ms); }, ending_anim.value());
    }

    if (is_branch) {
        evaluate_branch(ms_from_start);
    }
    chara->update(current_ms);
}

void Player::draw(double ms_from_start, float x, float y, ray::Shader& mask_shader) {
    tex.draw_texture(LANE::LANE_BACKGROUND, {.y=y});
    if (player_num == PlayerNum::AI) tex.draw_texture(LANE::AI_LANE_BACKGROUND, {.y=y});
    if (branch_indicator.has_value()) {
        branch_indicator->draw(y);
    }
    if (gauge.has_value()) {
        if (is_2p) {
            gauge->draw(y + tex.skin_config[SC::GAUGE_2P_OFFSET].y);
        } else {
            gauge->draw(y);
        }
        // ROUND 80 (r80-gauge-layering-recheck): the skin's arcade gauge
        // overlay (the 50x21 segment redraw) belongs HERE, glued to the
        // engine's own gauge, not at the end of the frame. Fail-soft: a skin
        // without a `draw_gauge` hook is never called and nothing changes.
        if (bg_hook) bg_hook->draw_gauge(player_num);
    }
    if (lane_hit_effect.has_value()) {
        lane_hit_effect->draw(y);
    }
    tex.draw_texture(LANE::LANE_HIT_CIRCLE, {.x = judge_x, .y = y + judge_y});

    if (gogo_time.has_value()) {
        gogo_time->draw(judge_x, y + judge_y);
    }
    if (fireworks.has_value()) {
        fireworks->draw();
    }
    draw_lane_cover(y);

    for (Judgment& anim : draw_judge_list) {
        anim.draw(judge_x, y + judge_y);
    }

    {
        int scissor_x = virtual_to_screen_x(static_cast<float>(tex.textures[lane_cover_tex_id]->x2[0]));
        int win_w = ray::GetScreenWidth();
        ray::BeginScissorMode(scissor_x, 0, win_w - scissor_x, ray::GetScreenHeight());
        draw_notes(ms_from_start, y);
        ray::EndScissorMode();
    }

    draw_overlays(y, mask_shader);

    if (global_data.config->general.song_timer) {
        draw_song_timer(ms_from_start, y);
    }
}

void Player::draw_practice(double ms_from_start, float x, float y, ray::Shader& mask_shader, bool draw_notes_on) {
    tex.draw_texture(LANE::LANE_BACKGROUND, {.y=y});
    if (player_num == PlayerNum::AI) tex.draw_texture(LANE::AI_LANE_BACKGROUND, {.y=y});
    if (branch_indicator.has_value()) {
        branch_indicator->draw(y);
    }
    // The soul gauge is LIVE in practice, it just used to be invisible: this
    // function is a copy of Player::draw() that lost the gauge block. The gauge
    // object is constructed for practice like any other run (reset_chart ->
    // `gauge = Gauge(GaugeMode::NORMAL, ...)`), it is updated every frame, and
    // add_good/add_ok/add_bad all feed it - and Player::update() even pushes its
    // value into the skin every frame via Background::handle_gauge(), which the
    // child skin's Scripts/background/bg_objects/arcade_gauge.lua uses to paint
    // the fill segments. So the skin was drawing the arcade gauge's FILL onto a
    // plate that was never drawn underneath it. Measured on GAME_PRACTICE with
    // --auto at 98% good: the gauge band (rows 204..272, the same rect normal
    // GAME uses) was empty background, while the only bar on screen was the
    // practice PROGRESS bar at rows 546..572 - which is what "the gauge moved"
    // actually was. Drawn at the same place and in the same order as in
    // Player::draw() so the two screens stay pixel-identical in this band.
    if (gauge.has_value()) {
        if (is_2p) {
            gauge->draw(y + tex.skin_config[SC::GAUGE_2P_OFFSET].y);
        } else {
            gauge->draw(y);
        }
        // ROUND 80 (r80-gauge-layering-recheck): the skin's arcade gauge
        // overlay (the 50x21 segment redraw) belongs HERE, glued to the
        // engine's own gauge, not at the end of the frame. Fail-soft: a skin
        // without a `draw_gauge` hook is never called and nothing changes.
        if (bg_hook) bg_hook->draw_gauge(player_num);
    }
    if (lane_hit_effect.has_value()) {
        lane_hit_effect->draw(y);
    }
    tex.draw_texture(LANE::LANE_HIT_CIRCLE, {.x = judge_x, .y = y + judge_y});

    if (gogo_time.has_value()) {
        gogo_time->draw(judge_x, y + judge_y);
    }
    if (fireworks.has_value()) {
        fireworks->draw();
    }
    draw_lane_cover(y);

    for (Judgment& anim : draw_judge_list) {
        anim.draw(judge_x, y + judge_y);
    }

    if (draw_notes_on) {
        int scissor_x = virtual_to_screen_x(static_cast<float>(tex.textures[lane_cover_tex_id]->x2[0]));
        int win_w = ray::GetScreenWidth();
        ray::BeginScissorMode(scissor_x, 0, win_w - scissor_x, ray::GetScreenHeight());
        draw_notes(ms_from_start, y);
        ray::EndScissorMode();
    }

    draw_overlays(y, mask_shader);
}

void Player::get_load_time(Note& note) {
    int note_half_w = tex.textures[NOTES::_9]->width / 2;
    // ROUND 55: load/unload horizon must use the same 1422 px span as
    // get_position_x, or notes would load late (span grew) / unload early.
    // CHN05 emits records at progress <= 1.2 and culls left at offset < -300
    // (note_geometry.md section 0, re_scroll_time.md section 3); this engine's
    // load-at-right-edge / symmetric unload brackets that window (load happens
    // when the note is still off-screen at 618+1422+halfW, cull happens behind
    // the left lane chrome either way), so only the span itself is ported --
    // see ENGINE_BINDINGS.md ROUND 55 for why -300 is deliberately NOT ported
    // (no lane scissor pass in this engine; an early pop would be visible).
    float travel_distance = r55_travel_span(tex.screen_width, JudgePos::X);
    float base_pixels_per_ms = (note.bpm / 240000 * abs(note.scroll_x) * travel_distance);
    if (base_pixels_per_ms == 0) {
        base_pixels_per_ms = (note.bpm / 240000 * abs(note.scroll_y) * travel_distance);
    }
    if (base_pixels_per_ms == 0) {
        note.load_ms = note.hit_ms;
        note.unload_ms = note.hit_ms;
        return;
    }
    float normal_travel_ms = (travel_distance + note_half_w) / base_pixels_per_ms;

    if (!note.sudden_appear_ms.has_value() ||
        !note.sudden_moving_ms.has_value() ||
        note.sudden_appear_ms.value() == std::numeric_limits<float>::infinity()) {
        note.load_ms = note.hit_ms - normal_travel_ms;
        note.unload_ms = note.hit_ms + normal_travel_ms;
        return;
    }
    note.load_ms = note.hit_ms - note.sudden_appear_ms.value();
    float movement_duration = note.sudden_moving_ms.value();
    if (movement_duration <= 0) {
        movement_duration = normal_travel_ms;
    }
    float sudden_pixels_per_ms = travel_distance / movement_duration;
    float unload_offset = travel_distance / sudden_pixels_per_ms;
    note.unload_ms = note.hit_ms + unload_offset;
}

void Player::reset_chart() {
    auto [notes, branch_m_temp, branch_e_temp, branch_n_temp] = parser->notes_to_position(difficulty);
    apply_modifiers(notes, modifiers);

    Note* last_note = nullptr;
    end_time = 0;
    bpm = parser->metadata.bpm;
    scroll_multiplier = 1.0f;

    for (Note& note: notes.notes) {
        get_load_time(note);
        if (note.type == NoteType::TAIL && last_note != nullptr) {
            note.load_ms = last_note->load_ms;
            last_note->unload_ms = note.unload_ms;
            auto it = std::find_if(draw_note_list.begin(), draw_note_list.end(),
                [&](const Note& n) { return n.index == last_note->index; });
            if (it != draw_note_list.end()) {
                it->unload_ms = note.unload_ms;
            }
        }
        if (note.type == NoteType::DON || note.type == NoteType::DON_L) {
            don_notes.push_back(note);
        } else if (note.type == NoteType::KAT || note.type == NoteType::KAT_L) {
            kat_notes.push_back(note);
        } else if (note.type != NoteType::BARLINE) {
            other_notes.push_back(note);
        }
        draw_note_list.push_back(note);
        if (note.type != NoteType::BARLINE) {
            last_note = &note;
        }

        if (note.hit_ms > end_time) {
            end_time = note.hit_ms;
        }
    }

    std::sort(draw_note_list.begin(), draw_note_list.end(),
              [](const Note& a, const Note& b) { return a.load_ms < b.load_ms; });

    this->branch_m = branch_m_temp;
    this->branch_e = branch_e_temp;
    this->branch_n = branch_n_temp;
    std::vector<std::reference_wrapper<std::deque<NoteList>>> branches = {
        std::ref(this->branch_m),
        std::ref(this->branch_e),
        std::ref(this->branch_n)
    };

    for (auto& branch_ref : branches) {
        std::deque<NoteList>& branch = branch_ref.get();

        if (!branch.empty()) {
            for (NoteList& section : branch) {
                apply_modifiers(section, modifiers);
                for (Note& note: section.notes) {
                    get_load_time(note);
                    if (note.type == NoteType::TAIL && last_note != nullptr) {
                        note.load_ms = last_note->load_ms;
                        last_note->unload_ms = note.unload_ms;
                    }
                    last_note = &note;

                    if (note.hit_ms > end_time) {
                        end_time = note.hit_ms;
                    }
                }
            }
        }
    }

    this->timeline = notes.timeline;

    std::sort(this->timeline.begin(), this->timeline.end(),
              [](const TimelineObject& a, const TimelineObject& b) { return a.start_time < b.start_time; });

    is_drumroll = false;
    curr_drumroll_count = 0;
    is_balloon = false;
    curr_balloon_count = 0;
    kusudama_shared_hits = 0;
    // ROUND 76 (r76-gamedan-band-and-autoplay): `last_subdivision` belongs to
    // THIS block and was the only member of it that never got reset.
    //
    // autoplay_manager() drives drumrolls/balloons off a 1/24-measure counter
    // derived from `ms_from_start` -- the PER-SONG clock (game_dan.cpp:621,
    // `ms_from_start = current_ms - start_ms`, with start_ms re-based by
    // change_song()). Every other autoplay timestamp on Player is taken from
    // `current_ms`, the monotonic wall clock, so nothing else noticed. A dan
    // course reuses ONE Player across all songs (reload_for_dan -> reset_chart),
    // so at the end of dan song 1 `last_subdivision` held that song's final
    // subdivision index; song 2's clock then restarted at 0 (in fact NEGATIVE
    // for the length of ROUND 69's between-song interstitial) and
    // `subdivision_in_ms > last_subdivision` stayed false for the whole song.
    // Result: from song 2 onwards AUTO silently stopped hitting every drumroll,
    // balloon and kusudama, while ordinary notes -- which run off the
    // don_notes/kat_notes queues that reset_chart DOES refill -- kept working.
    // That is exactly the user's report. Same class as ROUND 39's stale
    // `movie` optional and ROUND 66's `selected_dan_folder`.
    //
    // Resetting here rather than in reload_for_dan also fixes the same freeze
    // on the practice screen's backward seek (player.cpp's seek path calls
    // reset_chart too), where a jump to an earlier bar previously left the
    // counter ahead of the new clock.
    //
    // YATAIDON_R76_DISABLE reverts to the pre-round behaviour so before/after
    // evidence can be driven from ONE binary (ROUND 45 precedent).
    {
        static const bool r76_disabled = std::getenv("YATAIDON_R76_DISABLE") != nullptr;
        if (!r76_disabled) last_subdivision = -1;
    }
    is_branch = false;
    branch_condition = "";
    branch_p_count = 0;
    branch_r_count = 0;
    branch_note_count = 0;

    NoteList total_notes; //all notes including master branch

    total_notes.notes.insert(total_notes.notes.end(), notes.notes.begin(), notes.notes.end());
    for (NoteList section : branch_m) {
        total_notes.notes.insert(total_notes.notes.end(), section.notes.begin(), section.notes.end());
    }

    //setup gauge
    int stars = 0;
    if (parser->metadata.course_data.empty()) {
        stars = 10;
    } else {
        stars = parser->metadata.course_data[difficulty].level;
    }
    if (stars == 0) {
        difficulty = 3;
        stars = 10;
    }
    int gauge_total_notes = 0;
    for (Note& note : total_notes.notes) {
        if (note.type >= NoteType::DON && note.type <= NoteType::KAT_L) {
            gauge_total_notes++;
        }
    }
    judgeable_note_count = gauge_total_notes;
    gauge = Gauge(GaugeMode::NORMAL, player_num, gauge_total_notes, difficulty, stars);

    //setup score
    base_score = 0;
    score_init = 0;
    score_diff = 0;
    if (score_method == ScoreMethod::SHINUCHI) {
        base_score = calculate_base_score(total_notes);
    } else if (score_method == ScoreMethod::GEN3) {
        score_diff = parser->metadata.course_data[difficulty].scorediff;
        if (score_diff <= 0) {
            spdlog::warn("Error: No scorediff specified or scorediff less than 0 | Using shinuchi scoring method instead");
            score_diff = 0;
        }

        std::vector<int> score_init_list = parser->metadata.course_data[difficulty].scoreinit;
        if (score_init_list.size() <= 0) {
            spdlog::warn("Error: No scoreinit specified or scoreinit less than 0 | Using shinuchi scoring method instead");
            score_init = calculate_base_score(total_notes);
            score_diff = 0;
        } else {
            score_init = score_init_list[0];
        }
    }
}

std::optional<Note> Player::get_first_note() {
    if (draw_note_list.empty()) return std::nullopt;
    return draw_note_list.front();
}

float Player::get_position_x(const Note& note, double current_ms) {
    if (delay_start.has_value()) {
        current_ms = delay_start.value();
    }
    // ROUND 55: span = 1422 px (1920-space), not screen_width - judgeX. The
    // rate factor bpm*scroll/240000 is exactly CHN05's GetSpeed/GetLength
    // progress rate (re_scroll_time.md section 0: progress = dt * scroll *
    // BPM / 240000), so this line is now term-for-term the arcade formula
    // X = judgeX + progress * 1422 * hiSpeed.
    float speedx = note.bpm * scroll_multiplier / 240000 * note.scroll_x * r55_travel_span(tex.screen_width, JudgePos::X);
    return JudgePos::X + (note.hit_ms - current_ms) * speedx;
}

float Player::get_position_y(const Note& note, double current_ms) {
    if (delay_start.has_value()) {
        current_ms = delay_start.value();
    }
    // ROUND 55: same 1422 px span on the y axis so #JPOSSCROLL diagonals keep
    // their aspect (the old expression was (screen_width-JudgeX)/screen_width
    // * screen_width = the identical x travel distance).
    float speedy = note.bpm * scroll_multiplier / 240000 * note.scroll_y * r55_travel_span(tex.screen_width, JudgePos::X);
    return (note.hit_ms - current_ms) * speedy;
}

void Player::handle_scroll_type_commands(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (timeline_object.bpmchange.has_value()) {
        scroll_multiplier *= timeline_object.bpmchange.value();
        bpm *= timeline_object.bpmchange.value();
        if (buffer_index != (int)timeline_buffer.size() - 1)
            timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
        timeline_buffer.pop_back();
        return;
    }

    if (timeline_object.delay.has_value()) {
        if (delay_start.has_value()) {
            delay_start = timeline_object.start_time;
            delay_end = timeline_object.start_time + timeline_object.delay.value();
        } else {
            spdlog::error("Needs fix: delay is currently active, but another delay is being activated");
        }
        if (buffer_index != (int)timeline_buffer.size() - 1)
            timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
        timeline_buffer.pop_back();
        return;
    }
}

void Player::handle_gogotime(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (!timeline_object.gogo_time.has_value()) return;

    is_gogo_time = timeline_object.gogo_time.value();

    if (is_gogo_time) {
        gogo_time = GogoTime();
        fireworks = Fireworks();
        chara->set_anim(AnimIndex::DON_SABI);
        chara->set_anim(AnimIndex::DON_SABI_START);
    } else {
        gogo_time.reset();
        chara->set_anim(AnimIndex::DON_NORMAL);
    }

    if (buffer_index != (int)timeline_buffer.size() - 1)
        timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
    timeline_buffer.pop_back();
}

void Player::handle_judgeposition(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (!timeline_object.judge_pos_x.has_value()) return;
    if (!timeline_object.judge_pos_y.has_value()) return;
    if (!timeline_object.delta_x.has_value()) return;
    if (!timeline_object.delta_y.has_value()) return;

    if (timeline_object.start_time <= ms_from_start && ms_from_start <= timeline_object.end_time) {
        double duration = timeline_object.end_time - timeline_object.start_time;
        if (duration > 0) {
            double t = (ms_from_start - timeline_object.start_time) / duration;
            t = std::max(0.0, std::min(1.0, t));

            judge_x = (timeline_object.judge_pos_x.value() + (timeline_object.delta_x.value() * t)) * tex.screen_scale;
            judge_y = (timeline_object.judge_pos_y.value() + (timeline_object.delta_y.value() * t)) * tex.screen_scale;
        } else {
            judge_x = (timeline_object.judge_pos_x.value() + timeline_object.delta_x.value()) * tex.screen_scale;
            judge_y = (timeline_object.judge_pos_y.value() + timeline_object.delta_y.value()) * tex.screen_scale;
        }
    }

    if (ms_from_start > timeline_object.end_time) {
        if (buffer_index != (int)timeline_buffer.size() - 1)
            timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
        timeline_buffer.pop_back();
    }
}

void Player::handle_bpmchange(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (!timeline_object.bpm.has_value()) return;

    bpm = timeline_object.bpm.value();
    chara->set_bpm(bpm);

    if (buffer_index != (int)timeline_buffer.size() - 1)
        timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
    timeline_buffer.pop_back();
}

void Player::handle_branch_param(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (!timeline_object.branch_params.has_value()) return;

    std::string params = timeline_object.branch_params.value();

    std::vector<std::string> parts;
    std::stringstream ss(params);
    std::string part;
    while (std::getline(ss, part, ',')) {
        parts.push_back(part);
    }

    if (parts.size() >= 3) {
        std::string branch_cond = parts[0];
        float e_req = std::stof(parts[1]);
        float m_req = std::stof(parts[2]);

        if (!is_branch) {
            is_branch = true;
            branch_condition = branch_cond;

            double branch_condition_end_time;
            if (!branch_m.empty() && !branch_m.front().notes.empty()) {
                branch_condition_end_time = branch_m.front().notes.front().load_ms;
            } else if (!branch_e.empty() && !branch_e.front().notes.empty()) {
                branch_condition_end_time = branch_e.front().notes.front().load_ms;
            } else if (!branch_n.empty() && !branch_n.front().notes.empty()) {
                branch_condition_end_time = branch_n.front().notes.front().load_ms;
            } else {
                branch_condition_end_time = draw_note_list.front().load_ms;
            }

            if (branch_cond == "r") {
                curr_branch_reqs = std::make_tuple(e_req, m_req, branch_condition_end_time);
            } else if (branch_cond == "p") {
                curr_branch_reqs = std::make_tuple(e_req, m_req, branch_condition_end_time);
            }
            spdlog::info("branch condition measures started with conditions {}, {}, {}, starting at {} and ending at {}", branch_cond, e_req, m_req, timeline_object.start_time, branch_condition_end_time);
        }
    }
    if (buffer_index != (int)timeline_buffer.size() - 1)
        timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
    timeline_buffer.pop_back();
}

void Player::handle_section(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (!timeline_object.section_reset.has_value()) return;

    branch_p_count = 0;
    branch_r_count = 0;
    branch_note_count = 0;
    if (buffer_index != (int)timeline_buffer.size() - 1)
        timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
    timeline_buffer.pop_back();
}

void Player::handle_lyric(double ms_from_start, const TimelineObject& timeline_object, int buffer_index) {
    if (timeline_object.start_time > ms_from_start) return;
    if (!timeline_object.lyric.has_value()) return;

    if (current_lyric.has_value()) {
        current_lyric.reset();
    }

    current_lyric.emplace(timeline_object.lyric.value(), 40, ray::WHITE, ray::BLUE, false, 4.0);
    if (buffer_index != (int)timeline_buffer.size() - 1)
        timeline_buffer[buffer_index] = std::move(timeline_buffer.back());
    timeline_buffer.pop_back();
}

void Player::play_note_manager(double current_ms, std::optional<Background>& background) {
    // ROUND 44 (r44-tlb-mechanism-crosscheck): the auto-miss threshold must be
    // the SAME window as check_note()'s outer accept bound (the arcade declares
    // a miss on the first frame after `justTime + win_bad`, with win_bad the
    // same value the accept gate uses -- tlb_test_harness
    // research/note_judgement.md section 6, mechanism-shape reference only).
    // This function previously hard-coded Timing::BAD while check_note() and
    // autoplay_manager() both select BAD_EASY for EASY/NORMAL, so on those
    // difficulties a note was retired at +108.44 ms even though the accept
    // window ran to +125.125 ms: a press in the last ~16.7 ms of the BAD
    // window found the deque already popped and silently did nothing (stray +
    // miss) instead of judging 不可. Pure logic-shape fix; both constants were
    // already in this engine (player.h Timing), nothing taken from CHN05.
    const double miss_window = (difficulty <= (int)Difficulty::NORMAL)
                             ? Timing::BAD_EASY : Timing::BAD;
    while (!don_notes.empty() && don_notes.front().hit_ms + miss_window < current_ms) {
        if (std::getenv("YATAIDON_R33_GLSTATE")) {  // r45 evidence log
            spdlog::info("[r45miss] don note={:.1f} now={:.1f}", don_notes.front().hit_ms, current_ms);
        }
        combo = 0;
        if (background.has_value()) background->handle_bad(PlayerNum(1 + is_2p));
        bad_count++;
        if (dan_gauge) dan_gauge->add_bad();
        else if (gauge.has_value()) gauge->add_bad();

        don_notes.pop_front();
        branch_note_count++;
    }

    while (!kat_notes.empty() && kat_notes.front().hit_ms + miss_window < current_ms) {
        if (std::getenv("YATAIDON_R33_GLSTATE")) {  // r45 evidence log
            spdlog::info("[r45miss] kat note={:.1f} now={:.1f}", kat_notes.front().hit_ms, current_ms);
        }
        combo = 0;
        if (background.has_value()) background->handle_bad(PlayerNum(1 + is_2p));
        bad_count++;
        if (dan_gauge) dan_gauge->add_bad();
        else if (gauge.has_value()) gauge->add_bad();

        kat_notes.pop_front();
        branch_note_count++;
    }

    if (other_notes.empty()) return;

    Note& note = other_notes.front();
    if (note.hit_ms <= current_ms) {
        if (note.type == NoteType::ROLL_HEAD || note.type == NoteType::ROLL_HEAD_L) {
            is_drumroll = true;
        } else if (note.type == NoteType::BALLOON_HEAD || note.type == NoteType::KUSUDAMA) {
            is_balloon = true;
        } else if (note.type == NoteType::TAIL) {
            other_notes.pop_front();
            is_drumroll = false;
            is_balloon = false;
            curr_drumroll_count = 0;
            curr_balloon_count = 0;
            return;
        }
        Note& tail = other_notes[1];
        if (tail.hit_ms <= current_ms) {
            other_notes.pop_front();
            other_notes.pop_front();
            is_drumroll = false;
            is_balloon = false;
            curr_drumroll_count = 0;
            curr_balloon_count = 0;
        }
    }
}

void Player::draw_note_manager(double current_ms) {
    // ROUND 55: drain EVERY due object per frame, not one. CHN05 rebuilds the
    // whole visible record set every frame (DrawInfoMan::MakeDrawInfo resets
    // the ring and ProcSyousetsu re-emits every measure/note whose progress is
    // <= 1.2 -- re_scroll_time.md section 2), so nothing can ever spawn late.
    // The old one-per-frame `if` backlogged on dense charts: any frame where a
    // note AND its measure's barline (or two 32nd notes at high BPM) became
    // due together deferred the second object a frame, and the debt compounded
    // -- notes visibly popping in mid-lane on dense passages.
    while (!draw_note_list.empty() && current_ms >= draw_note_list.front().load_ms) {
        Note current_note = draw_note_list.front();
        draw_note_list.pop_front();

        if (current_note.type >= NoteType::ROLL_HEAD && current_note.type <= NoteType::BALLOON_HEAD) {
            auto pos = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                                        current_note,
                                        [](const auto& a, const auto& b) { return a.index < b.index; });
            draw_note_buffer.insert(pos, current_note);

            auto tail_it = std::find_if(draw_note_list.begin(), draw_note_list.end(),
                                        [&current_note](const auto& note) {
                                            return note.type == NoteType::TAIL && note.index > current_note.index;
                                        });

            if (tail_it != draw_note_list.end()) {
                auto tail_note = *tail_it;

                pos = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                                      tail_note,
                                      [](const auto& a, const auto& b) { return a.index < b.index; });
                draw_note_buffer.insert(pos, tail_note);

                draw_note_list.erase(tail_it);
            }
        } else if (current_note.type == NoteType::BARLINE) {
            auto pos = std::lower_bound(barlines.begin(), barlines.end(),
                                        current_note,
                                        [](const auto& a, const auto& b) { return a.index < b.index; });
            barlines.insert(pos, current_note);
        } else {
            auto pos = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                                        current_note,
                                        [](const auto& a, const auto& b) { return a.index < b.index; });
            draw_note_buffer.insert(pos, current_note);
        }
        if (r55_disabled()) break;  // pre-r55 behaviour: one object per frame
    }

    barlines.erase(
        std::remove_if(barlines.begin(), barlines.end(),
            [current_ms](const Note& n) { return current_ms >= n.unload_ms; }),
        barlines.end());

    if (draw_note_buffer.empty()) return;

    if (is_drumroll && !other_notes.empty()) {
        int active_drumroll_index = other_notes.front().index;
        auto drumroll_it = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                                            active_drumroll_index,
                                            [](const Note& n, int idx) { return n.index < idx; });
        if (drumroll_it != draw_note_buffer.end() && drumroll_it->index == active_drumroll_index &&
            (drumroll_it->type == NoteType::ROLL_HEAD || drumroll_it->type == NoteType::ROLL_HEAD_L) &&
            drumroll_it->color.has_value() && last_drumroll_color_time + 16.67f < current_ms) {
            last_drumroll_color_time = current_ms;
            if (!r55_disabled()) {
                // ROUND 55: CHN05 roll-intensity decay is -1 per idle cabinet
                // frame (60 fps) on the 0..150 accumulator -- note_judgement.md
                // section 5, roll handler 0x1401342F0: `SetExtraData(1,
                // GetExtraData(1) - 1)` on every frame with no input. The
                // 16.67 ms tick here IS one cabinet frame. (CHN05 skips the
                // decay on frames that landed a hit; at roll rates the +20 hit
                // step dwarfs the overlapping -1, so this tick is unconditional.)
                drumroll_it->roll_intensity = std::max(0.0f, drumroll_it->roll_intensity - 1.0f);
                drumroll_it->color = r55_roll_color(drumroll_it->roll_intensity);
            } else {
                drumroll_it->color = std::min(255, drumroll_it->color.value() + 1);
            }
        }
    }

    draw_note_buffer.erase(
        std::remove_if(draw_note_buffer.begin(), draw_note_buffer.end(),
            [current_ms](const Note& n) { return current_ms >= n.unload_ms; }),
        draw_note_buffer.end());
}

void Player::note_manager(double current_ms, std::optional<Background>& background) {
    play_note_manager(current_ms, background);
    draw_note_manager(current_ms);
}

void Player::note_correct(const Note& note, double current_ms) {
    if (!don_notes.empty() && don_notes[0] == note) {
        don_notes.pop_front();
    } else if (!kat_notes.empty() && kat_notes[0] == note) {
        kat_notes.pop_front();
    } else if (!other_notes.empty() && other_notes[0] == note) {
        other_notes.pop_front();
    }

    int index = note.index;
    if (note.type == NoteType::BALLOON_HEAD || note.type == NoteType::KUSUDAMA) {
        if (!other_notes.empty()) {
            other_notes.pop_front();
        }
    }

    if (note.type < NoteType::BALLOON_HEAD) {
        combo++;
        if (combo % 10 == 0) {
            // ROUND (r-chara3d-fullanim): AnimIndex::DON_FULL_COMBO was a
            // dead enum value until this round. bad_count == 0 ("zero
            // misses so far this run") is the engine's own established
            // full-combo signal -- spawn_ending_anim() above already gates
            // the result-screen FCAnimation on this exact same condition
            // (`bad_count == 0`), so this reuses that convention rather
            // than inventing a new one. At every 10-combo tick, play the
            // full-combo variant of the pose instead of the plain one while
            // the run is still miss-free; once a single 不可 has landed,
            // every future 10-combo tick falls back to the plain DON_COMBO.
            chara->set_anim(bad_count == 0 ? AnimIndex::DON_FULL_COMBO
                                            : AnimIndex::DON_COMBO);
        }
        if (combo % 100 == 0) {
            combo_announce = ComboAnnounce(combo, current_ms, player_num);
        }
        if (combo > max_combo) {
            max_combo = combo;
        }
        if (combo % 100 == 0 && score_method == ScoreMethod::GEN3) {
            score += 10000;
            base_score_list.push_back(ScoreCounterAnimation(player_num, 10000, is_2p));
        }
    }

    if (note.type != NoteType::KUSUDAMA) {
        bool is_big = note.type == NoteType::DON_L || note.type == NoteType::KAT_L || note.type == NoteType::BALLOON_HEAD;
        draw_arc_list.push_back(NoteArc(note.type, current_ms, PlayerNum(is_2p + 1), is_big, note.type == NoteType::BALLOON_HEAD, judge_x, judge_y));
    }
    auto it = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                               note.index, [](const Note& n, int idx) { return n.index < idx; });
    if (it != draw_note_buffer.end() && *it == note) {
        draw_note_buffer.erase(it);
    }
}

void Player::check_drumroll(double current_ms, DrumType drum_type, std::optional<Background>& background) {
    draw_arc_list.push_back(NoteArc(NoteType(drum_type), current_ms, PlayerNum(is_2p + 1), (int)drum_type == 3 || (int)drum_type == 4, false));
    curr_drumroll_count++;
    total_drumroll++;
    branch_r_count++;
    if (background.has_value()) background->handle_drumroll(PlayerNum(is_2p + 1));
    // ROUND 45b (r45-gogo-score): roll tick is 120 in gogo, 100 outside --
    // `(gogo && !EachPlayer[144]) ? 120 : 100` decoded at the CHN05 roll
    // handler (note_judgement.md section 5; constants user-authorized this
    // round). Ladder (GEN3) mode only, same gate as the discrete notes; the
    // arcade roll reads the LIVE gogo state (GamePlayInfo+8), which is what
    // is_gogo_time tracks here.
    {
        const int tick = (!r45_disabled() && score_method == ScoreMethod::GEN3 && is_gogo_time) ? 120 : 100;
        score += tick;
        if (base_score_list.size() < 5) {
            base_score_list.push_back(ScoreCounterAnimation(player_num, tick, is_2p));
        }
    }
    if (draw_note_buffer.empty()) return;
    if (!other_notes.empty()) {
        int active_drumroll_index = other_notes[0].index;
        auto drumroll_it = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                                            active_drumroll_index,
                                            [](const Note& n, int idx) { return n.index < idx; });
        if (drumroll_it != draw_note_buffer.end() && drumroll_it->index == active_drumroll_index &&
            (drumroll_it->type == NoteType::ROLL_HEAD || drumroll_it->type == NoteType::ROLL_HEAD_L) &&
            drumroll_it->color.has_value()) {
            if (!r55_disabled()) {
                // ROUND 55: CHN05 roll-intensity buildup: +20 per counted hit,
                // saturating at 150 (note_judgement.md section 5, 0x1401342F0:
                // `SetExtraData(1, min(GetExtraData(1) + 20, 150))`; constants
                // user-authorized). Full red at intensity 100 = 5 hits; the
                // 100..150 headroom keeps the head saturated through the first
                // 50 idle frames of decay. The old rule (255 - 10*count) took
                // 25 hits to saturate and never recovered per-hit history.
                drumroll_it->roll_intensity = std::min(150.0f, drumroll_it->roll_intensity + 20.0f);
                drumroll_it->color = r55_roll_color(drumroll_it->roll_intensity);
            } else {
                drumroll_it->color.value() = std::max(0, 255 - (curr_drumroll_count * 10));
            }
        }
    }
}

void Player::check_balloon(double current_ms, DrumType drum_type, const Note& balloon, std::optional<Background>& background) {
    if (drum_type != DrumType::DON) return;
    if (!balloon_counter.has_value()) {
        balloon_counter = BalloonCounter(balloon.count.value(), is_2p);
        chara->set_anim(AnimIndex::DON_BALLOON_LOOP);
    }
    if (background.has_value()) background->handle_balloon(PlayerNum(is_2p + 1));
    curr_balloon_count++;
    total_drumroll++;
    // ROUND 45b (r45-gogo-score): balloon tick 120 in gogo, 100 outside, same
    // decoded 100/120 pair as the roll (note_judgement.md section 5; the
    // arcade balloon reads the note's own IsGogoTime -- TJA notes carry no
    // per-note gogo flag, so the live is_gogo_time stands in; a balloon
    // spanning a gogo boundary is the only place the two can differ).
    {
        const int tick = (!r45_disabled() && score_method == ScoreMethod::GEN3 && is_gogo_time) ? 120 : 100;
        score += tick;
        base_score_list.push_back(ScoreCounterAnimation(player_num, tick, is_2p));
    }
    if (curr_balloon_count == balloon.count.value()) {
        is_balloon = false;
        balloon_counter->update(current_ms, curr_balloon_count);
        audio.play_sound("balloon_pop", VolumePreset::HITSOUND);
        note_correct(balloon, current_ms);
        curr_balloon_count = 0;
    }
}

void Player::check_kusudama(double current_ms, DrumType drum_type, const Note& balloon, std::optional<Background>& background) {
    if (drum_type != DrumType::DON) return;

    // ROUND 25 (r25-kusudama2p): the arcade's kusudama (段位鼓) sums hits from
    // EVERY seated player toward one common target and pops once for all of
    // them simultaneously -- that shared-pool behaviour is the whole reason it
    // is a distinct note type from a single-player balloon (see
    // ENGINE_BINDINGS.md ROUND 25). `owner` is `this` whenever there is no
    // partner (1P, DAN), so this whole function reduces byte-for-byte to the
    // previous single-player logic in those modes.
    Player* owner = kusudama_owner();
    if (!owner->kusudama_counter.has_value()) {
        owner->kusudama_counter = KusudamaCounter(balloon.count.value());
        owner->kusudama_shared_hits = 0;
    }
    if (background.has_value()) background->handle_balloon(PlayerNum(is_2p + 1));

    // Score/renda bookkeeping is per-hitting-player, same as before; only the
    // pop threshold and pop moment are shared via `owner`.
    total_drumroll++;
    // ROUND 45c (r45-kusudama-split): the arcade kusudama tick is
    // `100 / playerCount` per hitting player (decoded at 0x140134A39,
    // note_judgement.md section 5 -- ROUND 44 G4; constants user-authorized
    // this round), then x1.2 in gogo (same 120/100 ratio, ladder mode). The
    // pool here is at most 2 players (this seat + kusudama_partner), so
    // playerCount is 2 exactly when a partner shares the pool: 50/tick in 2P,
    // 100/tick in 1P/DAN, 60 in 2P gogo.
    {
        const int players = (!r45_disabled() && kusudama_partner && kusudama_partner != this) ? 2 : 1;
        int tick = 100 / players;
        if (!r45_disabled() && score_method == ScoreMethod::GEN3 && is_gogo_time)
            tick = 10 * (12 * tick / 100);
        if (std::getenv("YATAIDON_R33_GLSTATE")) {  // r45c evidence log
            spdlog::info("[r45kusu] p{} tick={} players={} gogo={}",
                         1 + is_2p, tick, players, is_gogo_time);
        }
        score += tick;
        base_score_list.push_back(ScoreCounterAnimation(player_num, tick, is_2p));
    }

    owner->kusudama_shared_hits++;
    owner->kusudama_counter->update(current_ms, owner->kusudama_shared_hits);

    if (owner->kusudama_shared_hits == balloon.count.value()) {
        audio.play_sound("kusudama_pop", VolumePreset::HITSOUND);

        // Resolve this player's own note.
        is_balloon = false;
        note_correct(balloon, current_ms);

        // The pop is shared, so resolve the partner's in-flight kusudama note
        // too, even if the partner did not land the final hit themselves.
        if (kusudama_partner && kusudama_partner != this && kusudama_partner->is_balloon &&
            !kusudama_partner->other_notes.empty() &&
            kusudama_partner->other_notes.front().type == NoteType::KUSUDAMA) {
            Note partner_note = kusudama_partner->other_notes.front();
            kusudama_partner->is_balloon = false;
            kusudama_partner->note_correct(partner_note, current_ms);
        }

        owner->kusudama_shared_hits = 0;
    }
}

void Player::check_note(double ms_from_start, DrumType drum_type, double current_ms, std::optional<Background>& background) {
    if (don_notes.empty() && kat_notes.empty() && other_notes.empty()) return;
    if (std::getenv("YATAIDON_R33_GLSTATE")) {
        // r45 evidence/calibration log: every press that reaches the judge,
        // before any routing, so a driver can map wall clock -> song time.
        spdlog::info("[r45press] press={:.1f} type={} don_head={:.1f} kat_head={:.1f}",
                     ms_from_start, (int)drum_type,
                     don_notes.empty() ? -1.0 : don_notes.front().hit_ms,
                     kat_notes.empty() ? -1.0 : kat_notes.front().hit_ms);
        // Flush immediately so a closed-loop driver can use this line as live
        // clock feedback (the hidden automation window is not vsync-paced and
        // its song-clock rate varies with render load). Gated path only.
        spdlog::default_logger()->flush();
    }

    float good_window_ms;
    float ok_window_ms;
    float bad_window_ms;
    if (difficulty <= (int)Difficulty::NORMAL) {
        good_window_ms = Timing::GOOD_EASY;
        ok_window_ms = Timing::OK_EASY;
        bad_window_ms = Timing::BAD_EASY;
    } else {
        good_window_ms = Timing::GOOD;
        ok_window_ms = Timing::OK;
        bad_window_ms = Timing::BAD;
    }
    if (score_method == ScoreMethod::GEN3) {
        base_score = score_init;
        if (9 < combo && combo < 30) {
            base_score = std::floor(score_init + 1 * score_diff);
        } else if (29 < combo && combo < 50) {
            base_score = std::floor(score_init + 2 * score_diff);
        } else if (49 < combo && combo < 100) {
            base_score = std::floor(score_init + 4 * score_diff);
        } else if (99 < combo) {
            base_score = std::floor(score_init + 8 * score_diff);
        }
    }

    Note curr_note;
    if (is_drumroll && !other_notes.empty()) {
        check_drumroll(current_ms, drum_type, background);
        return;
    } else if (is_balloon && !other_notes.empty()) {
        curr_note = other_notes.front();
        if (curr_note.type == NoteType::BALLOON_HEAD) {
            check_balloon(current_ms, drum_type, curr_note, background);
        }
        if (curr_note.type == NoteType::KUSUDAMA) {
            check_kusudama(current_ms, drum_type, curr_note, background);
        }
        return;
    } else if (drum_type == DrumType::DON) {
        if (don_notes.empty()) return;
        curr_note = don_notes.front();
    } else if (drum_type == DrumType::KAT) {
        if (kat_notes.empty()) return;
        curr_note = kat_notes.front();
    }

    // ROUND 45 (r45-stalehead-lookahead): stale-head look-ahead, the arcade's
    // dense-passage recovery rule (tlb_test_harness research/note_judgement.md
    // section 3, CorePlayer::ProcessMainNextOnpu 0x140560070 -- mechanism-shape
    // reference only, no CHN05 constants; 39.06 keeping this exact rule is
    // inherited-likelihood, not binary-confirmed -- see ENGINE_BINDINGS.md
    // ROUND 44 G2 / ROUND 45). Once the head note is STALE -- the press is
    // later than `hit_ms + win_ok`, i.e. inside the head's late 不可-only
    // region -- the cabinet offers the press to the FOLLOWING note first,
    // which is eligible from its own `hit_ms - win_ok` onward. Only if that
    // note declines (press before its accept start) does the stale head take
    // the press and judge 不可. Before this round a slightly-late recovery
    // stroke in any passage denser than ~win_bad+win_ok always BAD'd the
    // stale head (combo break) where the cabinet charges a plain miss on the
    // head plus a clean 良/可 on the next note.
    //
    // CHN05's decoded exceptions (no skip when the measure opens on a
    // balloon; OnpuTypes 6/9/12 get no early allowance) have no TJA-model
    // equivalent here BY CONSTRUCTION: rolls/balloons/kusudama live in
    // other_notes and are routed to their own handlers above before the
    // colour split, so the per-colour deques -- and therefore both the stale
    // head and the look-ahead target -- only ever hold regular judged notes
    // (DON/KAT, small or big). Big notes need no special case either
    // (mechanism M12: strength never gates accept). Only the immediate
    // SECOND note is consulted, matching the one-step shape ROUND 44
    // recorded; the head stays in the deque for play_note_manager()'s miss
    // scan, which retires it at `hit_ms + win_bad` exactly as before.
    std::deque<Note>& colour_deque = (drum_type == DrumType::DON) ? don_notes : kat_notes;
    bool stale_lookahead = false;
    if (!r45_disabled() &&
        ms_from_start > curr_note.hit_ms + ok_window_ms && colour_deque.size() >= 2 &&
        ms_from_start >= colour_deque[1].hit_ms - ok_window_ms) {
        stale_lookahead = true;
        if (std::getenv("YATAIDON_R33_GLSTATE")) {
            spdlog::info("[r45judge] lookahead press={:.1f} stale_head={:.1f} -> next={:.1f}",
                         ms_from_start, curr_note.hit_ms, colour_deque[1].hit_ms);
        }
        curr_note = colour_deque[1];
    }

    {
        if (ms_from_start > (curr_note.hit_ms + bad_window_ms)) return;

        bool big = curr_note.type == NoteType::DON_L || curr_note.type == NoteType::KAT_L;
        if ((curr_note.hit_ms - good_window_ms <= ms_from_start) && (ms_from_start <= curr_note.hit_ms + good_window_ms)) {
            if (draw_judge_list.size() < 7) {
                draw_judge_list.push_back(Judgment(Judgments::GOOD, big));
            }
            lane_hit_effect = LaneHitEffect(drum_type, Judgments::GOOD);
            good_count++;
            // ROUND 45b (r45-gogo-score): gogo-time x1.2, exactly the decoded
            // CHN05 CalcBaseAddScore shape+constants (0x140133FF0,
            // note_judgement.md section 8 -- constants user-authorized this
            // round): applied AFTER the ladder (and after the 可-halving +
            // floor-to-10 in the OK branch) as `v = 10 * (12 * v / 100)`
            // integer math, and ONLY in ladder mode -- the binary gates the
            // bonus on `!EachPlayer[144]`, i.e. the fixed-per-note (shinuchi
            // analogue) mode gets NO gogo bonus, and section 8 shows retail
            // CHN05 always runs fixed mode. SHINUCHI here therefore stays
            // flat, matching the decoded binary rather than community lore;
            // whether 39.06's shinuchi differs is an open 39.06-RE question.
            {
                int add = base_score;
                if (!r45_disabled() && score_method == ScoreMethod::GEN3 && is_gogo_time)
                    add = 10 * (12 * add / 100);
                score += add;
                if (base_score_list.size() < 5) {
                    base_score_list.push_back(ScoreCounterAnimation(player_num, add, is_2p));
                }
            }
            // r45: a look-ahead-judged note is deque[1]; note_correct() only
            // pops a matching FRONT, so remove it here and leave the stale
            // head in place for the miss scan.
            if (stale_lookahead) colour_deque.erase(colour_deque.begin() + 1);
            if (std::getenv("YATAIDON_R33_GLSTATE")) {
                spdlog::info("[r45judge] GOOD press={:.1f} note={:.1f} lookahead={}",
                             ms_from_start, curr_note.hit_ms, stale_lookahead);
            }
            note_correct(curr_note, current_ms);
            if (dan_gauge) dan_gauge->add_good();
            else if (gauge.has_value()) gauge->add_good();
            branch_p_count++;
            branch_note_count++;
            if (background.has_value()) background->handle_good(PlayerNum(1 + is_2p));

        } else if ((curr_note.hit_ms - ok_window_ms) <= ms_from_start && ms_from_start <= (curr_note.hit_ms + ok_window_ms)) {
            draw_judge_list.push_back(Judgment(Judgments::OK, big));
            lane_hit_effect = LaneHitEffect(drum_type, Judgments::OK);
            ok_count++;
            // ROUND 45b (r45-gogo-score): decoded order of operations
            // (CalcBaseAddScore 0x140133FF0): ladder -> 可 halves -> floor to
            // 10 -> gogo x1.2 as `10 * (12 * v / 100)`, ladder mode only.
            // `10 * (base_score / 2 / 10)` is the same integer math the old
            // `10 * std::floor(base_score / 2 / 10)` computed (base_score is
            // an int and never negative here).
            {
                int add = 10 * (base_score / 2 / 10);
                if (!r45_disabled() && score_method == ScoreMethod::GEN3 && is_gogo_time)
                    add = 10 * (12 * add / 100);
                score += add;
                if (base_score_list.size() < 5) {
                    base_score_list.push_back(ScoreCounterAnimation(player_num, add, is_2p));
                }
            }
            // r45: same deque[1] removal as the GOOD branch above.
            if (stale_lookahead) colour_deque.erase(colour_deque.begin() + 1);
            if (std::getenv("YATAIDON_R33_GLSTATE")) {
                spdlog::info("[r45judge] OK press={:.1f} note={:.1f} lookahead={}",
                             ms_from_start, curr_note.hit_ms, stale_lookahead);
            }
            note_correct(curr_note, current_ms);
            if (dan_gauge) dan_gauge->add_ok();
            else if (gauge.has_value()) gauge->add_ok();
            branch_p_count += 0.5;
            branch_note_count++;
            if (background.has_value()) background->handle_ok(PlayerNum(1 + is_2p));

        } else if ((curr_note.hit_ms - bad_window_ms) <= ms_from_start && ms_from_start <= (curr_note.hit_ms + bad_window_ms)) {
            draw_judge_list.push_back(Judgment(Judgments::BAD, big));
            bad_count++;
            combo = 0;
            branch_note_count++;
            // r45: with the look-ahead active this branch can only be reached
            // when the SECOND note is itself already past its own +win_ok
            // (both notes stale at once) -- the press then judges 不可 on the
            // judged note wherever it sits in the deque, and any remaining
            // overdue head is retired by play_note_manager()'s cascade scan.
            Note note = curr_note;
            colour_deque.erase(colour_deque.begin() + (stale_lookahead ? 1 : 0));
            if (std::getenv("YATAIDON_R33_GLSTATE")) {
                spdlog::info("[r45judge] BAD press={:.1f} note={:.1f} lookahead={}",
                             ms_from_start, note.hit_ms, stale_lookahead);
            }
            auto it = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                                       note.index, [](const Note& n, int idx) { return n.index < idx; });
            if (it != draw_note_buffer.end() && *it == note) draw_note_buffer.erase(it);
            if (dan_gauge) dan_gauge->add_bad();
            else if (gauge.has_value()) gauge->add_bad();
            if (background.has_value()) background->handle_bad(PlayerNum(1 + is_2p));
        }
    }
}

void Player::drumroll_counter_manager(double current_ms) {
    if (is_drumroll && curr_drumroll_count > 0 && drumroll_counter == std::nullopt) {
        drumroll_counter = DrumrollCounter();
    }

    if (drumroll_counter.has_value()) {
        if (drumroll_counter->is_finished() && !is_drumroll) {
            drumroll_counter.reset();
        } else {
            drumroll_counter->update(current_ms, curr_drumroll_count);
        }
    }
}

void Player::balloon_counter_manager(double current_ms) {
    if (!is_balloon && balloon_counter.has_value()) {
        bool popped = balloon_counter->is_finished();
        balloon_counter.reset();
        chara->set_anim(AnimIndex::DON_NORMAL);
        chara->set_anim(popped ? AnimIndex::DON_BALLOON_SUCCESS
                               : AnimIndex::DON_BALLOON_FAILURE);
    }
    if (balloon_counter.has_value()) {
        balloon_counter->update(current_ms, curr_balloon_count);
        if (balloon_counter->is_finished()) {
            if (score_method == ScoreMethod::GEN3) {
                score += 5000;
                base_score_list.push_back(ScoreCounterAnimation(player_num, 5000, is_2p));
            }
            balloon_counter.reset();
            chara->set_anim(AnimIndex::DON_NORMAL);
            chara->set_anim(AnimIndex::DON_BALLOON_SUCCESS);
        }
    }
}

void Player::kusudama_counter_manager(double current_ms) {
    // ROUND 25 (r25-kusudama2p): kusudama_counter now only ever gets set on the
    // pool owner (see kusudama_owner()/check_kusudama), so this is a no-op on a
    // non-owning partner -- exactly one shared widget updates/draws per shared
    // kusudama, not one per seated player. Uses kusudama_shared_hits (not
    // curr_balloon_count, which is reserved for the unrelated single-player
    // BALLOON_HEAD path) so the visible digit reflects the combined pool.
    if (!is_balloon && kusudama_counter.has_value()) {
        kusudama_counter.reset();
    }
    if (kusudama_counter.has_value()) {
        kusudama_counter->update(current_ms, kusudama_shared_hits);
        if (kusudama_counter->is_finished()) {
            kusudama_counter.reset();
        }
    }
}

void Player::cut_to_end(double now, int prev_good, int prev_ok, int prev_bad) {
    draw_note_list.clear();
    draw_note_buffer.clear();
    don_notes.clear();
    kat_notes.clear();
    other_notes.clear();
    barlines.clear();
    branch_m.clear();
    branch_e.clear();
    branch_n.clear();
    timeline.clear();
    is_drumroll = false;
    is_balloon = false;
    drumroll_counter.reset();
    balloon_counter.reset();
    kusudama_counter.reset();
    // end_song() gates on end_time + 1000 for the score save and + 8533 for the
    // result transition; putting end_time a second behind `now` makes the save
    // and the ending animation happen on this frame, as the arcade does when it
    // pre-loads its ending wait counter.
    end_time = now - 1000.0;

    // --- the arcade's record policy for a skipped song -----------------------
    // EnsoGameManager.obj.c:1332-1352 (CHN05 build of the 39.06 engine), the
    // state(+112) == 4 branch that runs while the per-player result record is
    // being filled. It does FOUR things and does NOT discard the record:
    //     v17[28]            = 0;                  // all-notes-reached flag
    //     *((_DWORD*)v17+6)  = 0;                  // gauge -> 0
    //     *(_DWORD*)v17      = 0;                  // crown -> NONE
    //     *((_DWORD*)v17+24) = v30 - v27 - v26;    // BAD = total - GOOD - OK
    // The last line is the important one: every note the player never reached
    // is recorded as a BAD. That is what makes a skipped run structurally
    // unable to be a full combo (the crown ladder at :1293-1308 needs BAD == 0
    // AND TaikoCorePlayer::IsAllOnpuEndPlayer), and it is why the cabinet can
    // keep the play on record honestly instead of throwing it away.
    skipped_run = true;
    // ROUND 20 (r19-danskip): `judgeable_note_count` is set per song at chart
    // load (reset_chart), but in a 段位道場 run `good_count`/`ok_count` are
    // CUMULATIVE across the whole course (DanGameScreen never resets them
    // between songs -- see game_dan.cpp's song_stats/prev_good tracking).
    // Subtracting this song's own baseline first isolates THIS song's good/ok
    // before comparing against THIS song's note total; prev_bad re-adds
    // whatever was already banked by earlier songs. With the defaults (0,0,0)
    // this is exactly the original single-song formula.
    int song_good = good_count - prev_good;
    int song_ok   = ok_count   - prev_ok;
    bad_count = prev_bad + std::max(0, judgeable_note_count - song_good - song_ok);
}

void Player::spawn_hit_effects(DrumType drum_type, Side side) {
    lane_hit_effect = LaneHitEffect(drum_type, Judgments::BAD); //judgment parameter workaround
    if (draw_drum_hit_list.size() < 4) {
        draw_drum_hit_list.push_back(std::make_unique<DrumHitEffect>(drum_type, side));
    }
}

void Player::handle_input(double ms_from_start, double current_ms, std::optional<Background>& background) {
    if (modifiers.auto_play) return;

    struct InputCheck {
        bool (*check_func)(PlayerNum);
        DrumType drum_type;
        Side side;
        const std::string* sound;
    };

    const InputCheck input_checks[] = {
        InputCheck{is_l_don_pressed, DrumType::DON, Side::LEFT, &don_hitsound},
        InputCheck{is_r_don_pressed, DrumType::DON, Side::RIGHT, &don_hitsound},
        InputCheck{is_l_kat_pressed, DrumType::KAT, Side::LEFT, &kat_hitsound},
        InputCheck{is_r_kat_pressed, DrumType::KAT, Side::RIGHT, &kat_hitsound}
    };

    for (const auto& input : input_checks) {

        while (input.check_func(player_num)) {
            spawn_hit_effects(input.drum_type, input.side);
            audio.play_sound(*input.sound, VolumePreset::HITSOUND);
            InputLogType log_type;
            if (input.drum_type == DrumType::DON) {
                log_type = input.side == Side::LEFT ? InputLogType::DON_L : InputLogType::DON_R;
            } else {
                log_type = input.side == Side::LEFT ? InputLogType::KAT_L : InputLogType::KAT_R;
            }
            input_log.insert({ms_from_start, log_type});
            check_note(ms_from_start, input.drum_type, current_ms, background);
        }
    }
}

void Player::draw_bar(double current_ms, float y, const Note& bar) {
    if (!bar.display) return;
    float x_position = get_position_x(bar, current_ms) + judge_x;
    float y_position = get_position_y(bar, current_ms) + judge_y + y;
    float angle;
    if (y_position != 0) {
        angle = std::atan2(bar.scroll_y, bar.scroll_x) * 180.0 / PI;
    } else {
        angle = 0;
    }
    tex.draw_texture(NOTES::_0, {.frame=bar.is_branch_start, .x=x_position+tex.skin_config[SC::MOJI_DRUMROLL].x - (tex.textures[NOTES::_9]->width/2.0f), .y=y_position+tex.skin_config[SC::MOJI_DRUMROLL].y, .rotation=angle});
}

void Player::draw_drumroll(double current_ms, float y, const Note& head, int note_frame, bool moji_pass) {
    if (head.sudden_appear_ms.has_value() && head.sudden_moving_ms.has_value()) {
        double appear_ms = head.hit_ms - head.sudden_appear_ms.value();
        double moving_start_ms = head.hit_ms - head.sudden_moving_ms.value();
        if (current_ms < appear_ms) return;
        if (current_ms < moving_start_ms) {
            current_ms = moving_start_ms;
        }
    }
    float start_position = get_position_x(head, current_ms);
    auto it = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                               head.index + 1, [](const Note& n, int idx) { return n.index < idx; });
    while (it != draw_note_buffer.end() && it->type != NoteType::TAIL) ++it;

    auto& tail = (it != draw_note_buffer.end()) ? *it : draw_note_buffer[1];
    bool is_big = head.type == NoteType::ROLL_HEAD_L;
    float end_position = get_position_x(tail, current_ms);
    float length = end_position - start_position;
    ray::Color color = ray::Color{255, (unsigned char)head.color.value(), (unsigned char)head.color.value(), 255};
    float y_pos = y + tex.skin_config[SC::NOTES].y + get_position_y(head, current_ms) + judge_y;
    start_position += judge_x;
    end_position += judge_x;
    float moji_y = y + tex.skin_config[SC::MOJI].y;
    if (moji_pass) {
        tex.draw_texture(NOTES::MOJI_DRUMROLL_MID, {.x=start_position, .y=moji_y+judge_y, .x2=length});
        tex.draw_texture(NOTES::MOJI, {.frame=head.moji, .x=start_position - (tex.textures[NOTES::MOJI]->width/2.0f), .y=moji_y+judge_y});
        tex.draw_texture(NOTES::MOJI, {.frame=tail.moji, .x=end_position - (tex.textures[NOTES::MOJI]->width/2.0f), .y=moji_y+judge_y});
        return;
    }

    if (head.display) {
        tex.draw_texture(NOTES::_8, {.color=color, .frame=is_big, .x=start_position, .y=y_pos, .x2=length+tex.skin_config[SC::DRUMROLL_WIDTH_OFFSET].width});
        if (is_big) {
            tex.draw_texture(NOTES::DRUMROLL_BIG_TAIL, {.color=color, .x=end_position, .y=y_pos});
        } else {
            tex.draw_texture(NOTES::DRUMROLL_TAIL, {.color=color, .x=end_position, .y=y_pos});
        }
        // `y_pos` already carries judge_y (line above); adding it a second time
        // pushed the drumroll head off the body under #JPOSSCROLL.
        // ROUND 55: roll heads share the note-face state machine (CHN05 kind
        // 6/9 frame tables are identical to don's). Clamp to the loaded art.
        int rh_frames = tex.textures[note_tex_ids[(int)head.type]]->frame_count();
        int rh_frame = (note_frame < rh_frames) ? note_frame : (rh_frames > 1 ? 1 : 0);
        tex.draw_texture(note_tex_ids[(int)head.type], {.color=color, .frame=rh_frame, .x=start_position - tex.textures[NOTES::_9]->width/2.0f, .y=y_pos});
    }
}

void Player::draw_balloon(double current_ms, float y, const Note& head, int note_frame, float pulse, bool moji_pass) {
    float offset = tex.skin_config[SC::BALLOON_OFFSET].x;
    if (head.sudden_appear_ms.has_value() && head.sudden_moving_ms.has_value()) {
        double appear_ms = head.hit_ms - head.sudden_appear_ms.value();
        double moving_start_ms = head.hit_ms - head.sudden_moving_ms.value();
        if (current_ms < appear_ms) return;
        if (current_ms < moving_start_ms) {
            current_ms = moving_start_ms;
        }
    }
    float start_position = get_position_x(head, current_ms);
    auto it = std::lower_bound(draw_note_buffer.begin(), draw_note_buffer.end(),
                               head.index + 1, [](const Note& n, int idx) { return n.index < idx; });
    while (it != draw_note_buffer.end() && it->type != NoteType::TAIL) ++it;

    auto& tail = (it != draw_note_buffer.end()) ? *it : draw_note_buffer[1];
    float end_position = get_position_x(tail, current_ms);
    float pause_position = JudgePos::X + judge_x;
    float y_pos = y + tex.skin_config[SC::NOTES].y + get_position_y(head, current_ms) + judge_y;
    float moji_y = y + tex.skin_config[SC::MOJI].y + get_position_y(head, current_ms) + judge_y;
    start_position += judge_x;
    end_position += judge_x;
    float position;
    if (current_ms >= tail.hit_ms) {
        position = end_position;
    } else if (current_ms >= head.hit_ms) {
        position = pause_position;
    } else {
        position = start_position;
    }
    if (moji_pass) {
        tex.draw_texture(NOTES::MOJI, {.frame=head.moji, .x=position - (tex.textures[NOTES::MOJI]->width/2.0f), .y=moji_y});
        return;
    }
    if (head.display) {
        // ROUND 55: face frame from the shared state machine; the balloon
        // BODY (_10) additionally squashes horizontally with the CHN05 pulse
        // (OnpuDraw.obj.c:5286-5294: DrawOnpParts(x = scale*halfW + off + X,
        // sx = scale, sy = 1.0) -- centre at scale*halfW past the anchor
        // means the LEFT edge stays fixed while the width breathes 1.0..0.7).
        int bh_frames = tex.textures[note_tex_ids[(int)head.type]]->frame_count();
        int bh_frame = (note_frame < bh_frames) ? note_frame : (bh_frames > 1 ? 1 : 0);
        int bb_frames = tex.textures[NOTES::_10]->frame_count();
        int bb_frame = (note_frame < bb_frames) ? note_frame : (bb_frames > 1 ? 1 : 0);
        tex.draw_texture(note_tex_ids[(int)head.type], {.frame=bh_frame, .x=position-offset - tex.textures[NOTES::_9]->width/2.0f, .y=y_pos});
        tex.draw_texture(NOTES::_10, {.frame=bb_frame, .x=position-offset+tex.textures[NOTES::_10]->width - tex.textures[NOTES::_9]->width/2.0f, .y=y_pos, .x2=tex.textures[NOTES::_10]->width * (pulse - 1.0f)});
    }
}

void Player::draw_notes(double current_ms, float y) {
    for (auto it = barlines.rbegin(); it != barlines.rend(); ++it) {
        draw_bar(current_ms, y, *it);
    }

    if (draw_note_buffer.empty()) return;

    double eighth_in_ms = (bpm == 0) ? 0 : (60000.0 * 4.0 / bpm) / 8.0;  // one half-beat
    // ROUND 55: CHN05 note-face animation state machine (OnpuDraw::Process,
    // decompiled/src/OnpuDraw.obj.c:5003-5052). Per frame the arcade picks an
    // animation state from the player's COMBO (per-player field +327260, the
    // same word EnsoGraphicCombo displays) against per-DIFFICULTY thresholds,
    // read from the CHN05 exe (dword_1409447D0 / dword_14093F988 /
    // dword_14093F970, verified by direct binary read; constants
    // user-authorized):
    //     state 0: combo <  T_LOW[d]              -> static face frame 0
    //     state 1: combo >= T_LOW[d]  (period 40) -> frames 0/1, 1 beat each
    //     state 2: combo >= T_MID[d]  (period 20) -> frames 0/1, half-beat each
    //     state 3: combo >= T_HIGH[d] (period 20) -> frames 0/2, half-beat each
    // Frame ids come from the per-kind layout tables (0x14093F9A0 + 556*kind,
    // +508: {0,0,0, 0,1,0, 0,1,0, 0,2,0} for don/ka/big/fusen) -- state 3
    // switches to a THIRD face (the high-combo expression). The phase counter
    // advances 60/(10798.38/BPM) per 60fps frame, so period 40 = exactly 2
    // beats and period 20 = 1 beat: the blink is beat-locked, which the
    // half-beat counter below reproduces (CHN05 free-runs the phase from the
    // last state change instead of anchoring it to song time -- the one
    // deliberate simplification here).
    // Pre-r55 behaviour: blink only at combo >= 50, alternating every
    // half-beat between frames 0/1 regardless of difficulty.
    int note_frame = 0;
    float balloon_pulse = 1.0f;
    if (r55_disabled()) {
        int current_eighth = 0;
        if (combo >= 50 && eighth_in_ms != 0) {
            current_eighth = static_cast<int>(current_ms / eighth_in_ms);
        }
        note_frame = current_eighth % 2;
    } else if (eighth_in_ms != 0) {
        static const int T_LOW[5]  = {5, 5, 10, 10, 10};
        static const int T_MID[5]  = {10, 10, 30, 50, 50};
        static const int T_HIGH[5] = {30, 30, 50, 100, 100};
        const int d = std::min(std::max(difficulty, 0), 4);
        int anim_state = 0;
        if      (combo >= T_HIGH[d]) anim_state = 3;
        else if (combo >= T_MID[d])  anim_state = 2;
        else if (combo >= T_LOW[d])  anim_state = 1;

        if (anim_state > 0) {
            const int half_beats = static_cast<int>(current_ms / eighth_in_ms);
            const double step_len_ms = (anim_state == 1) ? eighth_in_ms * 2.0 : eighth_in_ms;
            const int step = (anim_state == 1) ? (half_beats / 2) % 2 : half_beats % 2;
            if (step == 1) {
                note_frame = (anim_state == 3) ? 2 : 1;
                // Balloon squash pulse (OnpuDraw.obj.c:5273-5294): during the
                // step-1 phase the balloon body's x-scale runs 1.0 -> 0.7 over
                // phase units 0..5, recovers 0.7 -> 1.0 over 5..9, then rests.
                // Units tick at (period/steps) per step: 20 for state 1, 10
                // for states 2/3.
                const double frac = std::fmod(current_ms, step_len_ms) / step_len_ms;
                const double p = frac * ((anim_state == 1) ? 20.0 : 10.0);
                if (p <= 5.0)
                    balloon_pulse = 1.0f - 0.3f * (float)(p / 5.0);
                else if (p <= 9.0)
                    balloon_pulse = 0.7f + 0.3f * 0.25f * (float)(p - 5.0);
            }
        }
    }
    // Clamp a face frame against what the loaded 39.06 note art actually has
    // (don/ka carry all 3 frames; some big-note extractions carry only 2 --
    // fall back to the state-2 blink frame rather than throwing).
    auto face_frame = [&](uint32_t id) {
        int frames = tex.textures[id]->frame_count();
        return (note_frame < frames) ? note_frame : (frames > 1 ? 1 : 0);
    };

    auto skip_note = [&](const Note& note) {
        if (balloon_counter.has_value() && note.type == NoteType::BALLOON_HEAD && !other_notes.empty() && note.index == other_notes[0].index) {
            return true;
        }
        // ROUND 25 (r25-kusudama2p): kusudama_counter now only lives on the
        // pool owner (kusudama_owner()), so a non-owning 2P partner must check
        // the owner's counter, not its own (which is always empty for a
        // partner) -- otherwise the partner would keep drawing the plain note
        // texture underneath the shared counter widget instead of hiding it.
        if (kusudama_owner()->kusudama_counter.has_value() && note.type == NoteType::KUSUDAMA && !other_notes.empty() && note.index == other_notes[0].index) {
            return true;
        }
        return note.type == NoteType::TAIL;
    };

    // nullopt = note not visible yet (sudden command); otherwise screen position
    auto note_position = [&](const Note& note) -> std::optional<std::pair<float, float>> {
        float x_position, y_position;
        if (note.sudden_appear_ms.has_value() && note.sudden_moving_ms.has_value()) {
            double appear_ms = note.hit_ms - note.sudden_appear_ms.value();
            double moving_start_ms = note.hit_ms - note.sudden_moving_ms.value();

            if (current_ms < appear_ms) {
                return std::nullopt;
            }

            double effective_ms = (current_ms < moving_start_ms) ? moving_start_ms : current_ms;

            x_position = get_position_x(note, effective_ms);
            y_position = get_position_y(note, current_ms);
        } else {
            x_position = get_position_x(note, current_ms);
            y_position = get_position_y(note, current_ms);
        }
        return std::make_pair(x_position + judge_x, y_position + judge_y + y);
    };

    // Two passes: all note bodies, then all moji. Alternating between note
    // and moji textures per note flushes the sprite batch on every switch;
    // grouped by texture the whole lane draws in a few batches
    for (auto it = draw_note_buffer.rbegin(); it != draw_note_buffer.rend(); ++it) {
        auto& note = *it;
        if (skip_note(note)) continue;
        auto pos = note_position(note);
        if (!pos) continue;
        // ROUND 55: CHN05 culls the kusudama the moment it reaches the hit
        // point (OnpuDraw.obj.c:5138-5141, kind 12: drawn only while
        // offset >= 0) -- the big-ball widget owns the judge circle from
        // there. Pre-r55 an unhit kusudama kept scrolling left past the
        // judge circle, which the cabinet never shows.
        if (!r55_disabled() && note.type == NoteType::KUSUDAMA &&
            pos->first <= JudgePos::X + judge_x) continue;

        if (note.color.has_value()) {
            draw_drumroll(current_ms, y, note, note_frame, false);
        } else if (note.type == NoteType::BALLOON_HEAD) {
            draw_balloon(current_ms, y, note, note_frame, balloon_pulse, false);
        } else if (note.display) {
            tex.draw_texture(note_tex_ids[(int)note.type], {.frame=face_frame(note_tex_ids[(int)note.type]), .center=true, .x=pos->first - (tex.textures[NOTES::_9]->width/2.0f), .y=pos->second+tex.skin_config[SC::NOTES].y});
        }
    }

    for (auto it = draw_note_buffer.rbegin(); it != draw_note_buffer.rend(); ++it) {
        auto& note = *it;
        if (skip_note(note)) continue;
        auto pos = note_position(note);
        if (!pos) continue;
        if (!r55_disabled() && note.type == NoteType::KUSUDAMA &&
            pos->first <= JudgePos::X + judge_x) continue;  // r55: see pass 1

        if (note.color.has_value()) {
            draw_drumroll(current_ms, y, note, note_frame, true);
        } else if (note.type == NoteType::BALLOON_HEAD) {
            draw_balloon(current_ms, y, note, note_frame, balloon_pulse, true);
        } else {
            tex.draw_texture(NOTES::MOJI, {.frame=note.moji, .x=pos->first - (tex.textures[NOTES::MOJI]->width/2.0f), .y=tex.skin_config[SC::MOJI].y + pos->second});
        }
    }
}

void Player::draw_song_timer(double current_ms, float y) {
    float progress = current_ms / end_time;
    float width = tex.skin_config[SC::SONG_TIMER].width * std::max(std::min(progress, 1.0f), 0.0f);
    ray::DrawRectangle(tex.skin_config[SC::SONG_TIMER].x, y + tex.skin_config[SC::SONG_TIMER].y, width, tex.skin_config[SC::SONG_TIMER].height, ray::Color(0, 255, 158, 255));
    tex.draw_texture(LANE::TIMER, {.y=y});
}

void Player::draw_modifiers(float y) {
    // These icons hang below the score cover for 1P. The 2P score cover is
    // shifted down and mirrored vertically, so mirror each icon's offset
    // within the cover as well: the stack pokes above the bar instead of
    // below (otherwise the icons draw at the 1P-relative spot, hidden under
    // the 2P nameplate / panel edge).
    auto icon_y = [&](uint32_t id) {
        if (!is_2p) return y;
        float cover_h = (float)tex.textures[LANE::LANE_SCORE_COVER]->y2[0];
        float json_y  = (float)tex.textures[id]->y[0];
        float icon_h  = (float)tex.textures[id]->y2[0];
        return y + tex.skin_config[SC::SCORE_COUNTER_2P_Y_OFFSET].y
                 + cover_h - icon_h - 2.0f * json_y;
    };

    if (score_method == ScoreMethod::SHINUCHI) {
        tex.draw_texture(LANE::MOD_SHINUCHI, {.y=icon_y(LANE::MOD_SHINUCHI)});
    }

    if (modifiers.speed >= 40) {
        tex.draw_texture(LANE::MOD_YONBAI, {.y=icon_y(LANE::MOD_YONBAI)});
    } else if (modifiers.speed >= 30) {
        tex.draw_texture(LANE::MOD_SANBAI, {.y=icon_y(LANE::MOD_SANBAI)});
    } else if (modifiers.speed > 10) {
        tex.draw_texture(LANE::MOD_BAISAKU, {.y=icon_y(LANE::MOD_BAISAKU)});
    }

    if (modifiers.display) {
        tex.draw_texture(LANE::MOD_DORON, {.y=icon_y(LANE::MOD_DORON)});
    }
    if (modifiers.inverse) {
        tex.draw_texture(LANE::MOD_ABEKOBE, {.y=icon_y(LANE::MOD_ABEKOBE)});
    }
    if (modifiers.random == 2) {
        tex.draw_texture(LANE::MOD_DETARAME, {.y=icon_y(LANE::MOD_DETARAME)});
    } else if (modifiers.random == 1) {
        tex.draw_texture(LANE::MOD_KIMAGURE, {.y=icon_y(LANE::MOD_KIMAGURE)});
    }
}

void Player::draw_lane_cover(float y) {
    tex.draw_texture(lane_cover_tex_id, {.y=y});
    if (is_dan) tex.draw_texture(LANE::DAN_LANE_COVER, {.y=y});
}


void Player::draw_overlays(float y, const ray::Shader& mask_shader) {
    tex.draw_texture(LANE::DRUM, {.y=y});
    if (ending_anim.has_value()) {
        std::visit([](auto& anim) { anim.draw(); }, ending_anim.value());
    }

    for (auto& anim : draw_drum_hit_list) {
        anim->draw(y);
    }
    for (NoteArc& anim : draw_arc_list) {
        anim.draw(y, mask_shader);
    }
    for (GaugeHitEffect& anim : gauge_hit_effect) {
        anim.draw(y);
    }
    draw_modifiers(y);

    combo_display.draw(y);
    if (combo_announce.has_value()) {
        combo_announce->draw(y + (tex.skin_config[SC::COMBO_ANNOUNCE_P2_Y_OFFSET].y * is_2p));
    }
    tex.draw_texture(lane_icon_tex_id, {.y=y, .index=is_2p});
    if (is_dan) {
        tex.draw_texture(LANE::LANE_DIFFICULTY, {.frame=6, .y=y});
    } else {
        tex.draw_texture(LANE::LANE_DIFFICULTY, {.frame=difficulty, .y=y, .index=is_2p});
    }
    if (judge_counter.has_value()) {
        judge_counter->draw();
    }

    if (modifiers.auto_play) {
        tex.draw_texture(tex.get_enum("lane/auto_icon_" + global_data.config->general.language), {.y=y, .index=is_2p});
    } else {
        if (is_2p) {
            nameplate.draw(tex.skin_config[SC::GAME_NAMEPLATE_2P].x, y + tex.skin_config[SC::GAME_NAMEPLATE_2P].y);
        } else {
            nameplate.draw(tex.skin_config[SC::GAME_NAMEPLATE_1P].x, y + tex.skin_config[SC::GAME_NAMEPLATE_1P].y);
        }
    }
    if (is_balloon) {
        // ROUND 108 (r108-2p-balloon-don): CLOSES ROUND 98's "GAME 2P NOT
        // AUDITED" gap for the balloon-time draw site.
        //
        // In the cabinet the balloon-time Don is NOT a lane-relative HUD
        // element -- it is a CHILD OF THE BALLOON RIG. `action_fusen.nulm`
        // sprite 55 (`main`, at stage centre 960,540) holds four siblings at
        // constant local transforms across all 85 frames:
        //     don       tx -364 ty -256  -> stage (596, 284)   <-- the Don
        //     fusen     tx -122 ty -156  -> stage (838, 384)   (balloon body)
        //     fukidashi tx -230 ty -380  -> stage (730, 160)   (count bubble)
        // (re-dumped this round with `lumen_anim_dump --sprite 55 --all
        // --leaves --auto-stop`; agrees with ROUND 104/106/107 to the pixel).
        // The whole document is placed once per seat by `enso_post.bin` part 27
        // `onpu/action_fusen`, playerNo 0 -> y 0, playerNo 1 -> y 444. So the
        // Don takes the SAME +444 the bubble and the body take -- one rig, one
        // offset -- and the engine's share of that is `444 - 264 = 180`,
        // i.e. exactly `balloon_counter_2p_offset`.
        //
        // We were giving it only the 264 lane pitch (`y` alone), so our 2P
        // balloon Don sat 180 px ABOVE its own bubble and its own balloon.
        // Measured on ROUND 107's `after2p_02.png`: balloon-body ink minus
        // Don draw y is -85 for 1P but +96 for 2P, a 181 px discrepancy that
        // is 0 in the cabinet.  That single error is what the user reported
        // twice over -- 「3d don要再低一點」 and 「878的浮出要再高一點」 are the
        // same 180 px seen from the Don's side and from the bubble's side.
        // The bubble itself is NOT moved: measured against the user's own
        // 878 frame it is already within -7 px of the cabinet in BOTH seats
        // (the known lane-top family offset).
        //
        // Gated on `balloon_counter` because `is_balloon` is ALSO true for a
        // kusudama (see check_kusudama / line ~1163), and `action_kusudama`
        // has no playerNo-1 row in any target -- the cabinet draws ONE shared
        // kusudama, so its Don must not take a per-seat offset.
        float rig_2p_y = 0.0f;
        if (is_2p && balloon_counter.has_value()) {
            if (const SkinInfo* p2 = tex.skin_entry("balloon_counter_2p_offset"))
                rig_2p_y = p2->y;
        }
        chara->draw(tex.skin_config[SC::GAME_CHARA_BALLOON].x, y + tex.skin_config[SC::GAME_CHARA_BALLOON].y + rig_2p_y, 1.0f, "GAME_BALLOON");
    } else {
        if (is_2p) {
            chara->draw(tex.skin_config[SC::GAME_CHARA_P2].x, y + tex.skin_config[SC::GAME_CHARA_P2].y, 1.0f, "GAME_P2");
        } else {
            chara->draw(tex.skin_config[SC::GAME_CHARA_P1].x, y + tex.skin_config[SC::GAME_CHARA_P1].y, 1.0f, "GAME_P1");
        }
    }

    if (drumroll_counter.has_value()) {
        drumroll_counter->draw(y + (tex.skin_config[SC::COMBO_ANNOUNCE_P2_Y_OFFSET].y * is_2p));
    }
    if (balloon_counter.has_value()) {
        balloon_counter->draw(y);
    }
    if (kusudama_counter.has_value()) {
        kusudama_counter->draw();
    }
    score_counter.draw(y);
    for (ScoreCounterAnimation& anim : base_score_list) {
        anim.draw(y);
    }
    if (current_lyric.has_value()) {
        current_lyric->draw({.x=(int)(tex.screen_width/2) - current_lyric->width/2, .y=static_cast<float>(tex.screen_height - (int)(current_lyric->height*1.5))});
    }
}

void Player::seek_to(double resume_time) {
    don_notes.clear();
    kat_notes.clear();
    other_notes.clear();
    draw_note_list.clear();
    draw_note_buffer.clear();
    barlines.clear();
    timeline_buffer.clear();
    draw_judge_list.clear();
    draw_drum_hit_list.clear();
    draw_arc_list.clear();
    gauge_hit_effect.clear();
    lane_hit_effect.reset();
    gogo_time.reset();
    fireworks.reset();
    drumroll_counter.reset();
    balloon_counter.reset();
    kusudama_counter.reset();
    combo_announce.reset();
    is_drumroll = false;
    is_balloon = false;
    curr_drumroll_count = 0;
    curr_balloon_count = 0;
    kusudama_shared_hits = 0;

    reset_chart();

    // Keep notes sitting exactly on the resume boundary (a bar's downbeat
    // lands there): the resume time is derived with floating arithmetic and
    // can come out a hair above the note's hit_ms, occasionally dropping
    // the first note of the target bar from the hit queues.
    const double boundary_eps = 1.0;
    auto filter = [resume_time, boundary_eps](std::deque<Note>& q) {
        while (!q.empty() && q.front().hit_ms < resume_time - boundary_eps) q.pop_front();
    };
    filter(don_notes);
    filter(kat_notes);
    filter(other_notes);

    draw_note_list.erase(
        std::remove_if(draw_note_list.begin(), draw_note_list.end(),
            [resume_time, boundary_eps](const Note& n) { return n.hit_ms < resume_time - boundary_eps; }),
        draw_note_list.end());
}
