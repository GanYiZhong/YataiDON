#include "modifier.h"
#include "../../libs/audio.h"

const std::map<std::string, std::string> ModifierSelector::TEX_MAP = {
    {"auto",    "mod_auto"},
    {"speed",   "mod_baisaku"},
    {"display", "mod_doron"},
    {"inverse", "mod_abekobe"},
    {"random",  "mod_kimagure"}
};
const std::array<std::string, 5> ModifierSelector::BASE_MOD_NAMES = {
    "auto", "speed", "display", "inverse", "random"
};

// Maps a row to the bool fields in PlayerData (auto, display, inverse, skip).
// Keyed by row *name*, not by index: the opt-in rows are inserted between the
// base ones, so an index switch would silently move with them.
bool ModifierSelector::get_bool(int mod_index) {
    const std::string& n = mod_names[mod_index];
    if (n == "auto")    return player->modifier_auto;
    if (n == "display") return player->modifier_display;
    if (n == "inverse") return player->modifier_inverse;
    if (n == "skip")    return player->modifier_skip;
    return false;
}

void ModifierSelector::set_bool(int mod_index, bool value) {
    const std::string& n = mod_names[mod_index];
    if      (n == "auto")    player->modifier_auto    = value;
    else if (n == "display") player->modifier_display = value;
    else if (n == "inverse") player->modifier_inverse = value;
    else if (n == "skip")    player->modifier_skip    = value;
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

    mod_names.assign(BASE_MOD_NAMES.begin(), BASE_MOD_NAMES.end());
    // 演奏スキップ sits between ランダム and 音色 on the arcade board
    // (song_select_option_selector.lua InitOptionName, sessionIndex_ 4 -> 5 -> 8).
    if (tex.skin_flag("option_skip_row")) mod_names.push_back("skip");
    if (tex.skin_flag("option_neiro_row")) {
        load_neiro_names();
        mod_names.push_back("neiro");
    }

    blue_arrow_fade = (FadeAnimation*)tex.get_animation(29, true);
    blue_arrow_move = (MoveAnimation*)tex.get_animation(30, true);
    move            = (MoveAnimation*)tex.get_animation(28, true);
    // Optional: the arcade slide-OUT is a different two-segment curve from the
    // slide-in (board_left "out": 0 -> -75 in 4 frames, then -> 650 in 15).
    // A skin that does not define animation 39 keeps the old behaviour of
    // replaying animation 28 for the way out.
    move_out        = tex.has_animation(39) ? (MoveAnimation*)tex.get_animation(39, true) : nullptr;
    move->start();
    move_sideways   = (MoveAnimation*)tex.get_animation(31, true);
    fade_sideways   = (FadeAnimation*)tex.get_animation(32, true);

    // A voice callout (voice_senkuoku_v12a/select_option_c), so it belongs on the
    // voice bus like every other 選曲 voice (neiro.cpp, diff_sort.cpp,
    // song_select/player.cpp). On SOUND it ignored the 音声 slider and played
    // 1.9 dB under the others at the shipped 0.81/1.00 defaults. (r14-sound)
    audio.play_sound("voice_options_" + std::to_string((int)player_num) + "p", VolumePreset::VOICE);

    static const std::array<SC, 5> MOD_NAME_KEYS = {
        SC::MODIFIER_NAME_AUTO, SC::MODIFIER_NAME_SPEED, SC::MODIFIER_NAME_DISPLAY,
        SC::MODIFIER_NAME_INVERSE, SC::MODIFIER_NAME_RANDOM
    };
    for (const auto& key : MOD_NAME_KEYS)
        text_name.push_back(make_text(tex.skin_config[key].text.at(language)));

    if (has_row("skip"))
        text_name.push_back(make_text(tex.skin_text("modifier_name_skip", language, "演奏スキップ")));
    if (has_neiro_row())
        text_name.push_back(make_text(tex.skin_text("modifier_name_neiro", language, "音色")));

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

    if (has_neiro_row()) {
        text_neiro   = make_text(neiro_names[neiro_index]);
        text_neiro_2 = make_text(neiro_names[neiro_index]);
    }
}

// Reads the same list NeiroSelector does: one hit-sound set per line plus a
// trailing "no sound" entry, and keeps the same player->neiro_index encoding
// (-1 = no sound) so both entry points stay interchangeable.
void ModifierSelector::load_neiro_names() {
    std::filesystem::path neiro_list_path = std::filesystem::path("Skins")
        / global_data.config->paths.skin
        / "Sounds" / "hit_sounds" / "neiro_list.txt";

    std::ifstream neiro_list(neiro_list_path);
    if (!neiro_list.is_open()) {
        spdlog::error("Failed to open neiro_list.txt");
    } else {
        std::string line;
        while (std::getline(neiro_list, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) neiro_names.push_back(line);
        }
    }
    neiro_names.push_back(tex.skin_text("modifier_text_muon", language, "無音"));

    neiro_index = player->neiro_index;
    if (neiro_index == -1) neiro_index = (int)neiro_names.size() - 1;
    neiro_index = std::clamp(neiro_index, 0, (int)neiro_names.size() - 1);
}

void ModifierSelector::step_neiro(int dir) {
    int count = (int)neiro_names.size();
    neiro_index = ((neiro_index + dir) % count + count) % count;
    player->neiro_index = (neiro_index == count - 1) ? -1 : neiro_index;

    if (!neiro_preview.empty()) audio.unload_sound(neiro_preview);
    neiro_preview.clear();
    if (neiro_index == count - 1) return;

    std::filesystem::path base = std::filesystem::path("Skins")
        / global_data.config->paths.skin
        / "Sounds" / "hit_sounds" / std::to_string(neiro_index);
    neiro_preview = audio.load_sound(base / (neiro_index == 0 ? "don.wav" : "don.ogg"), "hit_sound");
    audio.play_sound(neiro_preview, VolumePreset::HITSOUND);
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
    if (current_mod_index == (int)mod_names.size()) {
        is_confirmed = true;
        // start(), not restart(): restart() leaves is_started alone and
        // MoveAnimation::update early-returns on !is_started, so a move_out that was
        // never started would freeze the board on screen forever.
        if (move_out) { move = move_out; move->start(); }
        else          { move->restart(); }
    }
}

void ModifierSelector::start_text_animation(int dir) {
    move_sideways->start();
    fade_sideways->start();
    direction = dir;

    const std::string& mod_name = mod_names[current_mod_index];
    if (mod_name == "speed") {
        text_speed_2 = std::move(text_speed);
        text_speed = make_text(std::format("{:.1f}", player->modifier_speed / 10.0f));
    } else if (mod_name == "neiro") {
        text_neiro_2 = std::move(text_neiro);
        text_neiro = make_text(neiro_names[neiro_index]);
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

static int speed_step(int value, int dir) {
    if (dir > 0) {
        if (value >= 40) return 1;
        if (value >= 30) return 40;
        if (value >= 20) return 30;
        return value + 1;
    }
    if (value <= 1)  return 40;
    if (value <= 20) return value - 1;
    if (value <= 30) return 20;
    return 30;
}

void ModifierSelector::left() {
    if (is_confirmed) return;
    const std::string& mod_name = mod_names[current_mod_index];
    // A greyed row is inert on the cabinet, not just grey.
    if (row_greyed(mod_name)) return;

    if (mod_name == "speed") {
        player->modifier_speed = speed_step(player->modifier_speed, -1);
        start_text_animation(1);
    } else if (mod_name == "neiro") {
        step_neiro(-1);
        start_text_animation(1);
    } else if (mod_name == "random") {
        player->modifier_random = (player->modifier_random + 2) % 3;
        start_text_animation(1);
    } else {
        set_bool(current_mod_index, !get_bool(current_mod_index));
        start_text_animation(1);
    }
}

void ModifierSelector::right() {
    if (is_confirmed) return;
    const std::string& mod_name = mod_names[current_mod_index];
    // A greyed row is inert on the cabinet, not just grey.
    if (row_greyed(mod_name)) return;

    if (mod_name == "speed") {
        player->modifier_speed = speed_step(player->modifier_speed, +1);
        start_text_animation(-1);
    } else if (mod_name == "neiro") {
        step_neiro(1);
        start_text_animation(-1);
    } else if (mod_name == "random") {
        player->modifier_random = (player->modifier_random + 1) % 3;
        start_text_animation(-1);
    } else {
        set_bool(current_mod_index, !get_bool(current_mod_index));
        start_text_animation(-1);
    }
}

void ModifierSelector::draw_animated_text(const std::unique_ptr<OutlinedText>& primary, const std::unique_ptr<OutlinedText>& secondary, float x, float y, bool should_animate) {
    if (should_animate && !move_sideways->is_finished) {
        primary->draw({
            .x = x + ((float)move_sideways->attribute * direction),
            .y = y,
            .fade = fade_sideways->attribute});
        secondary->draw({
            .x = (direction * -tex.skin_config[SC::OPTION_TEXT_IN].x) + x + ((float)move_sideways->attribute * direction),
            .y = y,
            .fade = 1.0f - fade_sideways->attribute});
    } else {
        primary->draw({.x = x, .y = y});
    }
}

void ModifierSelector::draw() {
    float move_val = is_confirmed
        ? move->attribute + tex.skin_config[SC::SONG_SELECT_OFFSET].x
        : -move->attribute;
    float x = ((int)player_num - 1) * tex.skin_config[SC::OPTION_P2].x;
    float mod_offset_y = tex.skin_config[SC::MODIFIER_OFFSET].y;

    tex.draw_texture(MODIFIER::TOP,    {.x=x, .y=move_val});
    tex.draw_texture(tex.get_enum("modifier/" + (std::to_string((int)player_num) + "p")), {.x=x, .y=move_val});
    tex.draw_texture(MODIFIER::BOTTOM, {.x=x, .y=move_val + ((int)mod_names.size() * mod_offset_y)});

    for (int i = 0; i < (int)mod_names.size(); i++) {
        float row_y = move_val + (i * mod_offset_y);
        const std::string& mod_name = mod_names[i];
        bool is_current = (i == current_mod_index);

        tex.draw_texture(MODIFIER::BACKGROUND,                              {.x=x, .y=row_y});
        tex.draw_texture(tex.get_enum(std::string("modifier/") + (is_current ? "mod_bg_highlight" : "mod_bg")), {.x=x, .y=row_y});
        tex.draw_texture(MODIFIER::MOD_BOX,                                 {.x=x, .y=row_y});

        text_name[i]->draw({
            .x = tex.skin_config[SC::MODIFIER_OFFSET_2].x + x,
            .y = tex.skin_config[SC::MODIFIER_TEXT].y + row_y
        });

        float text_base_x = tex.skin_config[SC::MODIFIER_TEXT].x;
        float text_y = tex.skin_config[SC::MODIFIER_TEXT].y + row_y;

        if (mod_name == "speed") {
            float tx = text_base_x - (text_speed->width / 2.0f);
            draw_animated_text(text_speed, text_speed_2, tx + x, text_y, is_current);

            float spd = player->modifier_speed;
            if      (spd >= 40) tex.draw_texture(MODIFIER::MOD_YONBAI,         {.x=x, .y=row_y});
            else if (spd >= 30) tex.draw_texture(MODIFIER::MOD_SANBAI,         {.x=x, .y=row_y});
            else if (spd >  10) tex.draw_texture(tex.get_enum("modifier/" + (TEX_MAP.at(mod_name))), {.x=x, .y=row_y});

        } else if (mod_name == "neiro") {
            float tx = text_base_x - (text_neiro->width / 2.0f);
            draw_animated_text(text_neiro, text_neiro_2, tx + x, text_y, is_current);

        } else if (mod_name == "random") {
            if (player->modifier_random == 1) {
                float tx = text_base_x - (text_kimagure->width / 2.0f);
                draw_animated_text(text_kimagure, text_kimagure_2, tx + x, text_y, is_current);
                tex.draw_texture(tex.get_enum("modifier/" + (TEX_MAP.at(mod_name))), {.x=x, .y=row_y});
            } else if (player->modifier_random == 2) {
                float tx = text_base_x - (text_detarame->width / 2.0f);
                draw_animated_text(text_detarame, text_detarame_2, tx + x, text_y, is_current);
                tex.draw_texture(MODIFIER::MOD_DETARAME, {.x=x, .y=row_y});
            } else {
                float tx = text_base_x - (text_false->width / 2.0f);
                draw_animated_text(text_false, text_false_2, tx + x, text_y, is_current);
            }

        } else {
            // bool mod
            bool val = get_bool(i);
            auto icon = TEX_MAP.find(mod_name);
            if (val && icon != TEX_MAP.end())
                tex.draw_texture(tex.get_enum("modifier/" + icon->second), {.x=x, .y=row_y});
            const auto& primary   = val ? text_true   : text_false;
            const auto& secondary = val ? text_true_2 : text_false_2;
            float tx = text_base_x - (primary->width / 2.0f);
            draw_animated_text(primary, secondary, tx + x, text_y, is_current);
        }

        if (is_current) {
            tex.draw_texture(MODIFIER::BLUE_ARROW, {.x=x - (float)blue_arrow_move->attribute, .y=row_y, .fade=blue_arrow_fade->attribute});
            tex.draw_texture(MODIFIER::BLUE_ARROW, {.mirror=Mirror::HORIZONTAL, .x=x + tex.skin_config[SC::MODIFIER_OFFSET_2].y + (float)blue_arrow_move->attribute, .y=row_y, .fade=blue_arrow_fade->attribute});
        }
    }
}

// The localised row NAME, from the same skin_config entries the constructor
// feeds into text_name[] (OutlinedText keeps its string private).
std::string ModifierSelector::row_label(const std::string& n) const {
    if (n == "auto")    return tex.skin_config[SC::MODIFIER_NAME_AUTO].text.at(language);
    if (n == "speed")   return tex.skin_config[SC::MODIFIER_NAME_SPEED].text.at(language);
    if (n == "display") return tex.skin_config[SC::MODIFIER_NAME_DISPLAY].text.at(language);
    if (n == "inverse") return tex.skin_config[SC::MODIFIER_NAME_INVERSE].text.at(language);
    if (n == "random")  return tex.skin_config[SC::MODIFIER_NAME_RANDOM].text.at(language);
    if (n == "skip")    return tex.skin_text("modifier_name_skip", language, "演奏スキップ");
    if (n == "neiro")   return tex.skin_text("modifier_name_neiro", language, "音色");
    return std::string();
}

// --- Lua data-out -----------------------------------------------------------
// The panel's row state as plain values, so a skin can draw its own version of
// the 演奏オプション / NEIRO board on top of the engine's without re-deriving
// any of it. Nothing here changes what the engine draws.
// The arcade greys 演奏スキップ out whenever both drums are in the enso, because
// then there is no free drum to hit the skip on: the only GrayOut() call in the
// whole option board is
//     if player_num >= 2 then mc_OptionSelector_*[kSelectSkip]:GrayOut() end
// (39.06 song_select/song_select_option_selector.lua; see the skin's
// Graphics/song_select/MAPPING_option.md). GameScreen::init_skip reaches the
// same conclusion from the other end -- with both drums playing there is no
// unused lane -- so this is not cosmetic: in 2P the row genuinely does nothing.
//
// It is evaluated LIVE, which is also the answer for the mid-session 2P join
// (songselect_2p_join): the moment the session becomes 2P the row greys and
// stops accepting input, and the stored value is simply never honoured again
// because init_skip finds no free lane. The cabinet does not clear the saved
// setting either -- it just becomes inert.
bool ModifierSelector::row_greyed(const std::string& name) const {
    return name == "skip" && global_data.player_num == PlayerNum::TWO_PLAYER;
}

std::vector<ModifierSelector::ModRow> ModifierSelector::lua_rows() {
    // The arcade greys 演奏スキップ out whenever both drums are in the enso,
    // because then there is no free drum to hit the skip on. GameScreen::arm_skip
    // reaches the same conclusion from the other end, so this is not a cosmetic
    // rule: with two players the row genuinely does nothing.
    std::vector<ModRow> rows;
    rows.reserve(mod_names.size());
    for (int i = 0; i < (int)mod_names.size(); i++) {
        const std::string& n = mod_names[i];
        ModRow r;
        r.name = n;
        r.label = row_label(n);
        r.greyed = row_greyed(n);
        if (n == "speed") {
            r.value   = std::format("{:.1f}", player->modifier_speed / 10.0f);
            r.state   = player->modifier_speed;
            r.changed = player->modifier_speed != 10;
        } else if (n == "neiro") {
            r.value   = neiro_names.empty() ? std::string() : neiro_names[neiro_index];
            r.state   = neiro_index;
            r.changed = player->neiro_index != 0;
        } else if (n == "random") {
            if (player->modifier_random == 1)
                r.value = tex.skin_config[SC::MODIFIER_TEXT_KIMAGURE].text.at(language);
            else if (player->modifier_random == 2)
                r.value = tex.skin_config[SC::MODIFIER_TEXT_DETARAME].text.at(language);
            else
                r.value = tex.skin_config[SC::MODIFIER_TEXT_FALSE].text.at(language);
            r.state   = player->modifier_random;
            r.changed = player->modifier_random != 0;
        } else {
            bool v    = get_bool(i);
            r.state   = v ? 1 : 0;
            r.value   = v ? tex.skin_config[SC::MODIFIER_TEXT_TRUE].text.at(language)
                          : tex.skin_config[SC::MODIFIER_TEXT_FALSE].text.at(language);
            r.changed = v;
        }
        r.enabled = !is_confirmed && i >= current_mod_index;
        rows.push_back(std::move(r));
    }
    return rows;
}
