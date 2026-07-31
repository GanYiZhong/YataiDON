#include "player.h"
#include "../../libs/audio.h"
#include "../../libs/scores.h"

ResultPlayer::ResultPlayer(PlayerNum player_num, bool has_2p, bool is_2p)
    : player_num(player_num), has_2p(has_2p), is_2p(is_2p)
{
    int player_id = get_player_id(player_num);
    auto pd = scores_manager.get_player_data(player_id);

    std::string costume_name = pd ? std::to_string(pd->chara_cos_index) : "0";
    chara = std::make_unique<Chara3D>(costume_name);
    if (pd) {
        chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
        chara->apply_face(pd->chara_face_index);
    } else {
        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
    }
    chara->set_anim(AnimIndex::DON_NORMAL);

    SessionData& sd = global_data.session_data[(int)player_num];
    score_animator = ScoreAnimator(sd.result_data.score);
    nameplate = Nameplate(
        pd ? pd->username : "", pd ? pd->title : "",
        player_num,
        pd ? pd->dan : -1, pd ? pd->gold : false, pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
    update_list = {
        {"score",          sd.result_data.score},
        {"good",           sd.result_data.good},
        {"ok",             sd.result_data.ok},
        {"bad",            sd.result_data.bad},
        {"max_combo",      sd.result_data.max_combo},
        {"total_drumroll", sd.result_data.total_drumroll}
    };

    CrownType crown_type;
    if (sd.result_data.ok == 0 && sd.result_data.bad == 0)
        crown_type = CrownType::CROWN_DFC;
    else if (sd.result_data.bad == 0)
        crown_type = CrownType::CROWN_FC;
    else
        crown_type = CrownType::CROWN_CLEAR;

    int score_diff = std::max(0, sd.result_data.score - sd.result_data.prev_score);

    Modifiers mods = player_data_to_modifiers(pd.value_or(PlayerData{}));
    bool is_shinuchi = global_data.config->general.score_method == ScoreMethod::SHINUCHI;
    (void)crown_type;
    (void)score_diff;
    (void)is_shinuchi;
    (void)mods;
}

void ResultPlayer::update_score_animation(double current_ms, bool is_skipped) {
    if (is_skipped) {
        while (update_index < (int)update_list.size()) {
            auto& [field_name, value] = update_list[update_index];
            std::string value_str = std::to_string(value);
            if      (field_name == "score")          score          = value_str;
            else if (field_name == "good")           good           = value_str;
            else if (field_name == "ok")             ok             = value_str;
            else if (field_name == "bad")            bad            = value_str;
            else if (field_name == "max_combo")      max_combo      = value_str;
            else if (field_name == "total_drumroll") total_drumroll = value_str;
            update_index++;
        }
    } else if (score_delay.has_value() && update_index < (int)update_list.size()) {
        if (current_ms > score_delay.value()) {
            if (score_animator.has_value() && !score_animator->is_finished) {
                auto& [field_name, curr_num] = update_list[update_index];
                std::string next_score_str = score_animator->next_score();
                int new_num = std::stoi(next_score_str);
                if      (field_name == "score")          score          = next_score_str;
                else if (field_name == "good")           good           = next_score_str;
                else if (field_name == "ok")             ok             = next_score_str;
                else if (field_name == "bad")            bad            = next_score_str;
                else if (field_name == "max_combo")      max_combo      = next_score_str;
                else if (field_name == "total_drumroll") total_drumroll = next_score_str;
                if (new_num != curr_num) audio.play_sound("num_up", VolumePreset::SOUND);
                if (score_animator->is_finished) {
                    audio.play_sound("don", VolumePreset::SOUND);
                    score_delay.value() += 750;
                    if (update_index == (int)update_list.size() - 1) return;
                    update_index++;
                    if (update_index < (int)update_list.size()) {
                        score_animator = ScoreAnimator(std::get<1>(update_list[update_index]));
                    }
                }
                score_delay.value() += 16.67 * 3;
            }
        }
    }
    if (update_index > 0 && !high_score_sound_played) {
        SessionData& sd = global_data.session_data[(int)player_num];
        if (sd.result_data.score > sd.result_data.prev_score) {
            audio.play_sound("high_score_voice_" + std::to_string((int)player_num) + "p", VolumePreset::VOICE);
            high_score_sound_played = true;
        }
    }
}

void ResultPlayer::update(double current_ms, bool fade_in_finished, bool is_skipped) {
    (void)fade_in_finished;
    update_score_animation(current_ms, is_skipped);
    nameplate.update(current_ms);
    chara->update(current_ms);
}

void ResultPlayer::draw() {}
