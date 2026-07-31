#include "player.h"
#include "../../libs/input.h"
#include "../../libs/scores.h"

EntryPlayer::EntryPlayer(PlayerNum player_num, int side, BoxManager* box_manager)
    : player_num(player_num), side(side), box_manager(box_manager) {
    indicator = std::make_unique<Indicator>(Indicator::State::SELECT);

    int player_id = get_player_id(player_num);
    auto pd = scores_manager.get_player_data(player_id);
    nameplate = std::make_unique<Nameplate>(
        pd ? pd->username : "", pd ? pd->title : "",
        player_num,
        pd ? pd->dan : -1, pd ? pd->gold : false, pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
    std::string costume_name = pd ? std::to_string(pd->chara_cos_index) : "0";
    chara = std::make_unique<Chara3D>(costume_name, player_num == PlayerNum::P2);
    if (pd) {
        chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
        chara->apply_face(pd->chara_face_index);
    } else {
        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
    }
}

void EntryPlayer::start_animations() {}

void EntryPlayer::update(double current_time) {
    nameplate->update(current_time);
    indicator->update(current_time);
    chara->update(current_time);
    if (costume_menu) costume_menu->update(current_time);
}

void EntryPlayer::open_costume_menu() {
    costume_menu.emplace(player_num);
}

void EntryPlayer::draw_drum() {}

void EntryPlayer::draw_costume_menu() {}

void EntryPlayer::draw_nameplate_and_indicator(float fade) {}

bool EntryPlayer::is_cloud_animation_finished() {
    return true;
}

float EntryPlayer::get_nameplate_fadein() {
    return 1.0f;
}

void EntryPlayer::handle_input() {
    if (costume_menu) {
        costume_menu->handle_input();
        if (costume_menu->get_index().has_value()) {
            int selected_index = costume_menu->get_index().value();
            if (selected_index != chara_index) {
                chara_index = selected_index;
                std::string model_name = costume_menu->get_costume_name();
                chara = std::make_unique<Chara3D>(model_name, player_num == PlayerNum::P2);
                {
                    int player_id = get_player_id(player_num);
                    if (auto pd = scores_manager.get_player_data(player_id)) {
                        chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
                        chara->apply_face(pd->chara_face_index);
                    } else {
                        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
                    }
                }
            }
        }
        if (costume_menu->confirmed) {
            int player_id = get_player_id(player_num);
            if (auto pd = scores_manager.get_player_data(player_id)) {
                pd->chara_cos_index = std::stoi(costume_menu->get_costume_name());
                scores_manager.save_player_data(*pd);
            }
            costume_menu.reset();
            audio.play_sound("costume_select_" + std::to_string((int)player_num) + "p", VolumePreset::SOUND);
            chara->set_anim(AnimIndex::DON_BALLOON_SUCCESS);
        }
        return;
    }
    if (box_manager->is_box_selected()) return;

    if (is_l_don_pressed(player_num) || is_r_don_pressed(player_num)) {
        audio.play_sound("don", VolumePreset::SOUND);
        if (box_manager->is_costume_box()) {
            box_manager->open_costume_menu(player_num);
        } else {
            box_manager->select_box();
        }
    }
    if (is_l_kat_pressed(player_num)) {
        audio.play_sound("kat", VolumePreset::SOUND);
        box_manager->move_left();
    }
    if (is_r_kat_pressed(player_num)) {
        audio.play_sound("kat", VolumePreset::SOUND);
        box_manager->move_right();
    }
}
