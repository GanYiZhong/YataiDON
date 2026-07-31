#include "modifier.h"
#include "../../libs/audio.h"

const std::map<std::string, std::string> ModifierSelector::TEX_MAP = {
    {"auto",    "mod_auto"},
    {"speed",   "mod_baisaku"},
    {"display", "mod_doron"},
    {"inverse", "mod_abekobe"},
    {"random",  "mod_kimagure"}
};
const std::array<std::string, 5> ModifierSelector::MOD_NAMES = {
    "auto", "speed", "display", "inverse", "random"
};

// Maps mod_index to the bool fields in Modifiers (auto, display, inverse)
bool ModifierSelector::get_bool(int mod_index) {
    switch (mod_index) {
        case 0: return player->modifier_auto;
        case 2: return player->modifier_display;
        case 3: return player->modifier_inverse;
        default: return false;
    }
}

void ModifierSelector::set_bool(int mod_index, bool value) {
    switch (mod_index) {
        case 0: player->modifier_auto    = value; break;
        case 2: player->modifier_display = value; break;
        case 3: player->modifier_inverse = value; break;
        default: break;
    }
}

std::unique_ptr<OutlinedText> ModifierSelector::make_text(const std::string& str) {
    return std::make_unique<OutlinedText>(str, tex.skin_config[SC::MODIFIER_TEXT].font_size, ray::WHITE, ray::BLACK, false, 3.5f);
}

ModifierSelector::ModifierSelector(PlayerNum player_num, PlayerData* player) : player_num(player_num), player(player) {
    current_mod_index = 0;
    is_confirmed = false;
    is_finished = false;
    direction = -1;
    language = global_data.config->general.language;

    blue_arrow_fade = (FadeAnimation*)tex.get_animation(29, true);
    blue_arrow_move = (MoveAnimation*)tex.get_animation(30, true);
    move            = (MoveAnimation*)tex.get_animation(28, true);
    move->start();
    move_sideways   = (MoveAnimation*)tex.get_animation(31, true);
    fade_sideways   = (FadeAnimation*)tex.get_animation(32, true);

    audio.play_sound("voice_options_" + std::to_string((int)player_num) + "p", VolumePreset::SOUND);

    static const std::array<SC, 5> MOD_NAME_KEYS = {
        SC::MODIFIER_NAME_AUTO, SC::MODIFIER_NAME_SPEED, SC::MODIFIER_NAME_DISPLAY,
        SC::MODIFIER_NAME_INVERSE, SC::MODIFIER_NAME_RANDOM
    };
    for (const auto& key : MOD_NAME_KEYS)
        text_name.push_back(make_text(tex.skin_config[key].text.at(language)));

    text_true      = make_text(tex.skin_config[SC::MODIFIER_TEXT_TRUE].text.at(language));
    text_false     = make_text(tex.skin_config[SC::MODIFIER_TEXT_FALSE].text.at(language));
    text_speed     = make_text(std::format("{:.1f}", player->modifier_speed / 10.0f));
    text_kimagure  = make_text(tex.skin_config[SC::MODIFIER_TEXT_KIMAGURE].text.at(language));
    text_detarame  = make_text(tex.skin_config[SC::MODIFIER_TEXT_DETARAME].text.at(language));

    text_true_2     = make_text(tex.skin_config[SC::MODIFIER_TEXT_TRUE].text.at(language));
    text_false_2    = make_text(tex.skin_config[SC::MODIFIER_TEXT_FALSE].text.at(language));
    text_speed_2    = make_text(std::format("{:.1f}", player->modifier_speed / 10.0f));
    text_kimagure_2 = make_text(tex.skin_config[SC::MODIFIER_TEXT_KIMAGURE].text.at(language));
    text_detarame_2 = make_text(tex.skin_config[SC::MODIFIER_TEXT_DETARAME].text.at(language));
}

void ModifierSelector::update(double current_ms) {
    move->update(current_ms);
    blue_arrow_fade->update(current_ms);
    blue_arrow_move->update(current_ms);
    move_sideways->update(current_ms);
    fade_sideways->update(current_ms);
    is_finished = is_confirmed && move->is_finished;
}

void ModifierSelector::confirm() {
    if (is_confirmed) return;
    current_mod_index++;
    if (current_mod_index == (int)MOD_NAMES.size()) {
        is_confirmed = true;
        move->restart();
    }
}

void ModifierSelector::start_text_animation(int dir) {
    move_sideways->start();
    fade_sideways->start();
    direction = dir;

    const std::string& mod_name = MOD_NAMES[current_mod_index];
    if (mod_name == "speed") {
        text_speed_2 = std::move(text_speed);
        text_speed = make_text(std::format("{:.1f}", player->modifier_speed / 10.0f));
    } else if (mod_name == "random") {
        if (player->modifier_random == 1) {
            text_kimagure = std::move(text_kimagure_2);
            text_kimagure_2 = make_text(tex.skin_config[SC::MODIFIER_TEXT_KIMAGURE].text.at(language));
        } else if (player->modifier_random == 2) {
            text_detarame = std::move(text_detarame_2);
            text_detarame_2 = make_text(tex.skin_config[SC::MODIFIER_TEXT_DETARAME].text.at(language));
        }
    } else {
        // bool mod
        if (get_bool(current_mod_index)) {
            text_true = std::move(text_true_2);
            text_true_2 = make_text(tex.skin_config[SC::MODIFIER_TEXT_TRUE].text.at(language));
        } else {
            text_false = std::move(text_false_2);
            text_false_2 = make_text(tex.skin_config[SC::MODIFIER_TEXT_FALSE].text.at(language));
        }
    }
}

void ModifierSelector::left() {
    if (is_confirmed) return;
    const std::string& mod_name = MOD_NAMES[current_mod_index];

    if (mod_name == "speed") {
        player->modifier_speed = std::max(1, player->modifier_speed - 1);
        start_text_animation(-1);
    } else if (mod_name == "random") {
        player->modifier_random = std::max(0, player->modifier_random - 1);
        start_text_animation(-1);
    } else {
        set_bool(current_mod_index, !get_bool(current_mod_index));
        start_text_animation(-1);
    }
}

void ModifierSelector::right() {
    if (is_confirmed) return;
    const std::string& mod_name = MOD_NAMES[current_mod_index];

    if (mod_name == "speed") {
        player->modifier_speed += 1;
        start_text_animation(1);
    } else if (mod_name == "random") {
        player->modifier_random = (player->modifier_random + 1) % 3;
        start_text_animation(1);
    } else {
        set_bool(current_mod_index, !get_bool(current_mod_index));
        start_text_animation(1);
    }
}

void ModifierSelector::draw_animated_text(const std::unique_ptr<OutlinedText>& primary, const std::unique_ptr<OutlinedText>& secondary, float x, float y, bool should_animate) {}

void ModifierSelector::draw() {}
