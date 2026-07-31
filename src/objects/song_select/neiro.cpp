#include "neiro.h"
#include "../../libs/audio.h"

NeiroSelector::NeiroSelector(PlayerNum player_num, PlayerData* player) : player_num(player_num), player(player) {
    selected_sound = player->neiro_index;
    is_finished = false;
    is_confirmed = false;
    direction = -1;

    std::filesystem::path neiro_list_path = std::filesystem::path("Skins")
        / global_data.config->paths.skin
        / "Sounds" / "hit_sounds" / "neiro_list.txt";

    std::ifstream neiro_list(neiro_list_path);

    if (!neiro_list.is_open()) {
        spdlog::error("Failed to open neiro_list.txt");
    } else {
        std::string line;
        while (std::getline(neiro_list, line)) {
            if (!line.empty() && line.back() == '\n') line.pop_back();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            sounds.push_back(line);
        }
    }
    sounds.push_back("無音");
    if (selected_sound == -1) selected_sound = (int)sounds.size() - 1;
    selected_sound = std::clamp(selected_sound, 0, (int)sounds.size() - 1);
    load_sound();
    audio.play_sound("voice_hitsound_select_" + std::to_string((int)player_num) + "p", VolumePreset::VOICE);

    move = (MoveAnimation*)tex.get_animation(28, true);
    move->start();
    blue_arrow_fade = (FadeAnimation*)tex.get_animation(29, true);
    blue_arrow_move = (MoveAnimation*)tex.get_animation(30, true);
    move_sideways = (MoveAnimation*)tex.get_animation(31, true);
    fade_sideways = (FadeAnimation*)tex.get_animation(32, true);

    text = std::make_unique<OutlinedText>(sounds[selected_sound], tex.skin_config[SC::NEIRO_TEXT].font_size, ray::WHITE, ray::BLACK, false);
    text_2 = std::make_unique<OutlinedText>(sounds[selected_sound], tex.skin_config[SC::NEIRO_TEXT].font_size, ray::WHITE, ray::BLACK, false);
}

void NeiroSelector::load_sound() {
    if (selected_sound == (int)sounds.size() - 1) return;
    std::filesystem::path base = std::filesystem::path("Skins")
        / global_data.config->paths.skin
        / "Sounds" / "hit_sounds" / std::to_string(selected_sound);
    if (selected_sound == 0) {
        curr_sound = audio.load_sound(base / "don.wav", "hit_sound");
    } else {
        curr_sound = audio.load_sound(base / "don.ogg", "hit_sound");
    }
}

void NeiroSelector::left() {
    if (move->is_started && !move->is_finished) return;
    selected_sound = ((selected_sound - 1) % (int)sounds.size() + (int)sounds.size()) % (int)sounds.size();
    audio.unload_sound(curr_sound);
    load_sound();
    move_sideways->start();
    fade_sideways->start();

    text = std::move(text_2);
    text_2 = std::make_unique<OutlinedText>(sounds[selected_sound], tex.skin_config[SC::NEIRO_TEXT].font_size, ray::WHITE, ray::BLACK, false);

    direction = -1;
    if (selected_sound == (int)sounds.size() - 1) return;
    audio.play_sound(curr_sound, VolumePreset::HITSOUND);
}

void NeiroSelector::right() {
    if (move->is_started && !move->is_finished) return;
    selected_sound = (selected_sound + 1) % (int)sounds.size();
    audio.unload_sound(curr_sound);
    load_sound();
    move_sideways->start();
    fade_sideways->start();

    text = std::move(text_2);
    text_2 = std::make_unique<OutlinedText>(sounds[selected_sound], tex.skin_config[SC::NEIRO_TEXT].font_size, ray::WHITE, ray::BLACK, false);

    direction = 1;
    if (selected_sound == (int)sounds.size() - 1) return;
    audio.play_sound(curr_sound, VolumePreset::HITSOUND);
}

void NeiroSelector::confirm() {
    if (move->is_started && !move->is_finished) return;
    player->neiro_index = selected_sound == (int)sounds.size() - 1 ? -1 : selected_sound;
    is_confirmed = true;
    move->restart();
}

void NeiroSelector::update(double current_ms) {
    move->update(current_ms);
    blue_arrow_fade->update(current_ms);
    blue_arrow_move->update(current_ms);
    move_sideways->update(current_ms);
    fade_sideways->update(current_ms);
    is_finished = move->is_finished && is_confirmed;
}

void NeiroSelector::draw() {}
