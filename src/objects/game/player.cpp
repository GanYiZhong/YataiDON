#include "player.h"
#include "../../libs/audio.h"
#include "../../libs/input.h"
#include "../../libs/scores.h"

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
    std::string costume_name = pd ? std::to_string(pd->chara_cos_index) : "0";
    chara = std::make_unique<Chara3D>(costume_name);
    if (pd) {
        chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
        chara->apply_face(pd->chara_face_index);
    } else {
        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
    }
    chara->set_anim(AnimIndex::DON_NORMAL);
    if (global_data.config->general.judge_counter) {
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
    return result;
}

void Player::spawn_ending_anim() {
    if (!gauge.has_value() && !dan_gauge) return;
    bool is_clear = dan_gauge ? dan_gauge->get_is_clear() : gauge->get_is_clear();
    if (!is_clear) {
        ending_anim = FailAnimation(is_2p);
    } else if (bad_count == 0) {
        ending_anim = FCAnimation(is_2p);
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
        if (subdivision_in_ms > last_subdivision) {
            last_subdivision = subdivision_in_ms;
            hit_type = DrumType::DON;
            autoplay_hit_side = autoplay_hit_side == Side::LEFT ? Side::RIGHT : Side::LEFT;
            spawn_hit_effects(hit_type, autoplay_hit_side);
            audio.play_sound(don_hitsound, VolumePreset::HITSOUND);
            check_note(ms_from_start, hit_type, current_ms, background);
        }
    } else {
        while (!don_notes.empty() && ms_from_start >= don_notes.front().hit_ms) {
            hit_type = DrumType::DON;
            autoplay_hit_side = autoplay_hit_side == Side::LEFT ? Side::RIGHT : Side::LEFT;
            spawn_hit_effects(hit_type, autoplay_hit_side);
            audio.play_sound(don_hitsound, VolumePreset::HITSOUND);
            check_note(ms_from_start, hit_type, current_ms, background);
            last_note_hit = current_ms;
        }

        while (!kat_notes.empty() && ms_from_start >= kat_notes.front().hit_ms) {
            hit_type = DrumType::KAT;
            autoplay_hit_side = autoplay_hit_side == Side::LEFT ? Side::RIGHT : Side::LEFT;
            spawn_hit_effects(hit_type, autoplay_hit_side);
            audio.play_sound(kat_hitsound, VolumePreset::HITSOUND);
            check_note(ms_from_start, hit_type, current_ms, background);
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
            background->handle_gauge(player_num, gauge->get_progress(), gauge->get_is_clear(), gauge->get_is_rainbow());
        }
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
}

void Player::draw_practice(double ms_from_start, float x, float y, ray::Shader& mask_shader, bool draw_notes_on) {
}

void Player::get_load_time(Note& note) {
    int note_half_w = tex.textures[NOTES::_9]->width / 2;
    float travel_distance = tex.screen_width - JudgePos::X;
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
    float speedx = note.bpm * scroll_multiplier / 240000 * note.scroll_x * (tex.screen_width - JudgePos::X);
    return JudgePos::X + (note.hit_ms - current_ms) * speedx;
}

float Player::get_position_y(const Note& note, double current_ms) {
    if (delay_start.has_value()) {
        current_ms = delay_start.value();
    }
    float speedy = note.bpm * scroll_multiplier / 240000 * note.scroll_y * ((tex.screen_width - JudgePos::X)/tex.screen_width) * tex.screen_width;
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
    if (!don_notes.empty() && don_notes.front().hit_ms + Timing::BAD < current_ms) {
        combo = 0;
        if (background.has_value()) background->handle_bad(PlayerNum(1 + is_2p));
        bad_count++;
        if (dan_gauge) dan_gauge->add_bad();
        else if (gauge.has_value()) gauge->add_bad();

        don_notes.pop_front();
        branch_note_count++;
    }

    if (!kat_notes.empty() && kat_notes.front().hit_ms + Timing::BAD < current_ms) {
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
    if (!draw_note_list.empty() && current_ms >= draw_note_list.front().load_ms) {
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
            drumroll_it->color = std::min(255, drumroll_it->color.value() + 1);
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
            chara->set_anim(AnimIndex::DON_COMBO);
        }
        if (combo % 100 == 0) {
            combo_announce = ComboAnnounce(combo, current_ms, player_num);
        }
        if (combo > max_combo) {
            max_combo = combo;
        }
        if (combo % 100 == 0 && score_method == ScoreMethod::GEN3) {
            score += 10000;
            base_score_list.push_back(ScoreCounterAnimation(player_num, 10000));
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
    score += 100;
    if (base_score_list.size() < 5) {
        base_score_list.push_back(ScoreCounterAnimation(player_num, 100));
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
            drumroll_it->color.value() = std::max(0, 255 - (curr_drumroll_count * 10));
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
    score += 100;
    base_score_list.push_back(ScoreCounterAnimation(player_num, 100));
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
    if (!kusudama_counter.has_value()) {
        kusudama_counter = KusudamaCounter(balloon.count.value());
    }
    if (background.has_value()) background->handle_balloon(PlayerNum(is_2p + 1));
    curr_balloon_count++;
    total_drumroll++;
    score += 100;
    base_score_list.push_back(ScoreCounterAnimation(player_num, 100));
    if (curr_balloon_count == balloon.count.value()) {
        is_balloon = false;
        audio.play_sound("kusudama_pop", VolumePreset::HITSOUND);
        kusudama_counter->update(current_ms, curr_balloon_count);
        note_correct(balloon, current_ms);
        curr_balloon_count = 0;
    }
}

void Player::check_note(double ms_from_start, DrumType drum_type, double current_ms, std::optional<Background>& background) {
    if (don_notes.empty() && kat_notes.empty() && other_notes.empty()) return;

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

    {
        if (ms_from_start > (curr_note.hit_ms + bad_window_ms)) return;

        bool big = curr_note.type == NoteType::DON_L || curr_note.type == NoteType::KAT_L;
        if ((curr_note.hit_ms - good_window_ms <= ms_from_start) && (ms_from_start <= curr_note.hit_ms + good_window_ms)) {
            if (draw_judge_list.size() < 7) {
                draw_judge_list.push_back(Judgment(Judgments::GOOD, big));
            }
            lane_hit_effect = LaneHitEffect(drum_type, Judgments::GOOD);
            good_count++;
            score += base_score;
            if (base_score_list.size() < 5) {
                base_score_list.push_back(ScoreCounterAnimation(player_num, base_score));
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
            score += 10 * std::floor(base_score / 2 / 10);
            if (base_score_list.size() < 5) {
                base_score_list.push_back(ScoreCounterAnimation(player_num, 10 * std::floor(base_score / 2 / 10)));
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
            Note note;
            if (drum_type == DrumType::DON) {
                note = don_notes.front();
                don_notes.pop_front();
            } else {
                note = kat_notes.front();
                kat_notes.pop_front();
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
        balloon_counter.reset();
        chara->set_anim(AnimIndex::DON_NORMAL);
        chara->set_anim(AnimIndex::DON_BALLOON_FAILURE);
    }
    if (balloon_counter.has_value()) {
        balloon_counter->update(current_ms, curr_balloon_count);
        if (balloon_counter->is_finished()) {
            if (score_method == ScoreMethod::GEN3) {
                score += 5000;
                base_score_list.push_back(ScoreCounterAnimation(player_num, 5000));
            }
            balloon_counter.reset();
            chara->set_anim(AnimIndex::DON_NORMAL);
            chara->set_anim(AnimIndex::DON_BALLOON_SUCCESS);
        }
    }
}

void Player::kusudama_counter_manager(double current_ms) {
    if (!is_balloon && kusudama_counter.has_value()) {
        kusudama_counter.reset();
    }
    if (kusudama_counter.has_value()) {
        kusudama_counter->update(current_ms, curr_balloon_count);
        if (kusudama_counter->is_finished()) {
            kusudama_counter.reset();
        }
    }
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
            check_note(ms_from_start, input.drum_type, current_ms, background);
        }
    }
}

void Player::draw_bar(double current_ms, float y, const Note& bar) {
}

void Player::draw_drumroll(double current_ms, float y, const Note& head, int current_eighth, bool moji_pass) {
}

void Player::draw_balloon(double current_ms, float y, const Note& head, int current_eighth, bool moji_pass) {
}

void Player::draw_notes(double current_ms, float y) {
}

void Player::draw_song_timer(double current_ms, float y) {
}

void Player::draw_modifiers(float y) {
}

void Player::draw_lane_cover(float y) {
}


void Player::draw_overlays(float y, const ray::Shader& mask_shader) {
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

    reset_chart();

    auto filter = [resume_time](std::deque<Note>& q) {
        while (!q.empty() && q.front().hit_ms < resume_time) q.pop_front();
    };
    filter(don_notes);
    filter(kat_notes);
    filter(other_notes);

    draw_note_list.erase(
        std::remove_if(draw_note_list.begin(), draw_note_list.end(),
            [resume_time](const Note& n) { return n.hit_ms < resume_time; }),
        draw_note_list.end());
}
