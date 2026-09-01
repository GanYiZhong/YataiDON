#include "box_manager.h"
#include "../../libs/global_data.h"
#include "../enums.h"          // GENRE_MAP / GenreIndex — see dan_library_available()

#include <fstream>

// ─── ROUND 83 (r83-dandojo-as-mode) — `Cabinet.DaniDojoFolderAvailable()` ──────
//
// On the cabinet 段位道場 is a MODE BOARD, appended by
// `script_lua/entry/mode_select.lua` `CreateBoardList()` (l.289-296) directly after
// `GameMode.kEnso`, and gated by `dani_enable`:
//
//     dani_available (= Cabinet.DaniDojoAvailable())  AND  exactly one seat, that
//     seat being a CARD login spending a credit (l.240-242)
//
// The card half is unportable — this engine has no card system, every seat enters as
// the cabinet's `kCoin` (`entry.cpp::join_player`, ROUND 12) — so it is deliberately
// not reproduced; it is also the reason the 段位道場 chip in `entry_overlay.lua` can
// only ever read NG (ROUND 49's open item, settled in MAPPING.md ROUND 83 §D).
//
// What IS portable is the content half. CHN05 registers **two** Lua bindings side by
// side, `Cabinet.DaniDojoAvailable` (0x1401A2A60) and `Cabinet.DaniDojoFolderAvailable`
// (0x1401A2AC0) — `decompiled/src/LuaFuncSetupCabinet.obj.c` — i.e. the cabinet itself
// splits "the operator enabled it" from "the courses are installed". This is the second
// one: no dan library on disk, no board.
//
// Cost matters (this runs in EntryScreen::on_screen_start): the dojo is a genre folder
// at depth 1 of a library root, exactly where SONG_SELECT finds it, so only the
// immediate children of each root are looked at — ~15 small reads, no recursive walk.
// `Navigator::parse_box_def` is a Navigator member and there is no Navigator here, so
// the one line that matters (`#GENRE:`) is read directly against the shared `GENRE_MAP`.
static bool dan_library_available() {
    if (!global_data.config) return false;
    const auto dan_names = GENRE_MAP.find(GenreIndex::DAN);
    if (dan_names == GENRE_MAP.end()) return false;   // fail soft
    std::error_code ec;
    for (const fs::path& root : global_data.config->paths.tja_path) {
        if (!fs::is_directory(root, ec)) continue;
        fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec) continue;
        for (const auto& entry : it) {
            if (!entry.is_directory(ec)) continue;
            std::ifstream box_def(entry.path() / "box.def");
            if (!box_def) continue;
            std::string line;
            while (std::getline(box_def, line)) {
                if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
                    (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                    line.erase(0, 3);
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                const size_t end = line.find_last_not_of(" \t\r\n");
                if (end != std::string::npos) line.erase(end + 1);
                if (!line.starts_with("#GENRE:")) continue;
                if (dan_names->second.contains(line.substr(7))) return true;
                break;
            }
        }
    }
    return false;
}

BoxManager::BoxManager(bool two_player)
    : selected_box_index(0), is_2p(two_player), costume_menu_open(false) {
    const std::string lang = global_data.config->general.language;

    // ROUND 83 — the dojo board is opt-in per skin AND gated on the library, so
    // PyTaikoGreen (which declares no `entry_dan` text) keeps its four boards and no
    // skin can end up with a board that leads to an empty ribbon. `skin_text` reads the
    // key by name, so this needs no new member in the generated SC enum (which is built
    // from the PARENT skin's skin_config.json and must not be touched from a child).
    // ROUND 86 — cached: this walks the library roots, and the list is now rebuilt live.
    dan_text      = tex.skin_text("entry_dan", lang);
    dan_available = !dan_text.empty() && dan_library_available();

    fade_out = (FadeAnimation*)tex.get_animation(9);

    build_board_list();
}

// ─── ROUND 86 — `ModeSelect:CreateBoardList()` (mode_select.lua:230-299) ───────
//
// The cabinet builds the whole board list from the two seats' playdata EVERY time, and
// `kDani` is appended only when `dani_enable` — which requires exactly one seat entered,
// the other still `UserType.kNone` (l.240-242). Two seats in and the board is simply not
// in the list: it is never drawn, never cursored onto, never decided. ROUND 83 rebuilt
// only the last of those three (`selection_allowed()`), which is the reported defect
// 「兩個人進入遊戲，不要顯示段位道場」 — the board was on screen and merely refused.
//
// The card half of `dani_enable` stays unportable (this engine has no card system; every
// seat is the cabinet's `kCoin`), so `dan_available` (content) AND `!is_2p` (seats) is the
// whole gate here.
void BoxManager::build_board_list() {
    const std::string lang = global_data.config->general.language;
    auto& skin = tex.skin_config;
    int font_size = skin[SC::ENTRY_BOX_TEXT].font_size;

    // The skin's mode-select board list is SHARED Lua state keyed by construction order
    // (`__hss_entry_boxes`, Scripts/entry/box.lua:16/279-285): `EntryBox.new` appends to it
    // and only starts a fresh list when `__hss_entry_reset` is set. `Scripts/entry/
    // entry.lua:10` sets it from Lua at `Entry.new()`, which covers a fresh screen but not
    // a rebuild inside one — and the missing engine-side hook is the reason ROUND 83 gave
    // for not doing this properly (see selection_allowed() below). There is nothing to
    // build: `ScriptManager::lua` is a live sol::state, so the engine sets the same global
    // the skin already reads, and `box.lua` clears it itself on the next EntryBox.new.
    // A skin without that convention (PyTaikoGreen) just carries an unread global.
    if (script_manager.lua) (*script_manager.lua)["__hss_entry_reset"] = true;

    boxes.clear();
    box_locations.clear();

    dan_box_index = (dan_available && !is_2p) ? 1 : -1;

    // Cabinet board order (mode_select.lua CreateBoardList): 演奏ゲーム then 段位道場.
    // 特訓モード / きせかえ / ゲーム設定 have no cabinet counterpart and follow.
    box_locations = {Screens::SONG_SELECT};
    boxes.push_back(std::make_unique<Box>(skin[SC::ENTRY_GAME].text[lang], font_size, Screens::SONG_SELECT));
    if (dan_box_index >= 0) {
        box_locations.push_back(Screens::DAN_SELECT);
        boxes.push_back(std::make_unique<Box>(dan_text, font_size, Screens::DAN_SELECT));
    }
    for (auto [screen, sc] : {std::pair{Screens::PRACTICE_SELECT, SC::ENTRY_PRACTICE},
                              std::pair{Screens::ENTRY,           SC::ENTRY_COSTUME},
                              std::pair{Screens::SETTINGS,        SC::ENTRY_SETTINGS}}) {
        box_locations.push_back(screen);
        boxes.push_back(std::make_unique<Box>(skin[sc].text[lang], font_size, screen));
    }
    //boxes.push_back(std::make_unique<Box>(skin[SC::ENTRY_AI_BATTLE].text[lang], font_size, Screens::AI_SELECT));

    num_boxes = boxes.size();

    // ROUND 83's 2P remap, applied here too so a list rebuilt while 2P is already correct
    // on its first drawn frame rather than one update() later.
    if (is_2p) {
        for (int i = 0; i < num_boxes; i++) {
            if (box_locations[i] == Screens::SONG_SELECT)
                boxes[i]->location = Screens::SONG_SELECT_2P;
        }
    }

    float spacing_x = skin[SC::ENTRY_BOX_SPACING].x;
    float spacing_y = skin[SC::ENTRY_BOX_SPACING].y;
    is_vertical = spacing_y > 0;

    if (is_vertical) {
        float step = spacing_y;
        float total_height = (num_boxes - 1) * step;
        float start_y = tex.screen_height / 2.0f - total_height / 2.0f;
        float center_x = tex.screen_width / 2.0f - tex.textures[MODE_SELECT::BOX_HIGHLIGHT_CENTER]->width / 2.0f;

        for (int i = 0; i < num_boxes; i++) {
            boxes[i]->set_positions(center_x, start_y + i * step);
            if (i > 0) {
                boxes[i]->move_down();
            }
        }
    } else {
        float box_width = boxes[0]->width;
        float total_width = num_boxes * box_width + (num_boxes - 1) * spacing_x;
        float start_x = tex.screen_width / 2.0f - total_width / 2.0f;

        for (int i = 0; i < num_boxes; i++) {
            boxes[i]->set_positions(start_x + i * (box_width + spacing_x));
            if (i > 0) {
                boxes[i]->move_right();
            }
        }
    }
}

// ─── ROUND 86 — `CheckBoardListChange` / `ChangeBoardList` ────────────────────
//
// `entry_main.lua:1142` polls `ModeSelect:CheckBoardListChange(playdata_p1, playdata_p2)`
// on every mode-select frame and calls `ChangeBoardList` on a difference — that is how the
// cabinet drops the dojo board the instant the second seat joins mid-mode-select, without
// leaving the screen. `CheckBoardListChange` (l.213-229) rebuilds a throwaway list and
// compares it length-first then element-wise; ours has exactly one variable term, so the
// comparison collapses to "is the dojo board wanted, and is it there".
bool BoxManager::check_board_list_change() const {
    return (dan_available && !is_2p) != (dan_box_index >= 0);
}

// `ChangeBoardList` (mode_select.lua:198-212). Note line 200: the cabinet sets
// `select_idx_ = select_idx_org_` — the DEFAULT index CreateBoardList returned (演奏ゲーム
// with no collabo modes installed), NOT the index the player had cursored onto. The user's
// cursor is deliberately discarded, and that is also what keeps ROUND 83's warning at
// update() below from biting: indexing this list by position broke once when the length
// became dynamic, and no stale index survives a reset to the default board.
void BoxManager::change_board_list() {
    build_board_list();
    selected_box_index = 0;   // = select_idx_org_
}

// ROUND 83 — `ModeSelectBorad:IsOnePlayerOnly(mode)` (ModeSelectBorad.lua:96-118)
// returns true for `GameMode.kDani`: the dojo is a solo mode on the cabinet too, which
// is why its board carries the `only_1` icon (MC 110 label `only_1`). The cabinet
// enforces it one step earlier than we can — `CreateBoardList` simply never emits the
// board while both seats are in, and `ChangeBoardList` rebuilds the list live when the
// second seat joins mid-mode-select.
//
// ROUND 86 does exactly that (build_board_list / change_board_list above), so this is no
// longer the enforcement — it is a BACKSTOP: if any future path ever puts the cursor on a
// dojo board in a 2P session, the decide is still refused the way `ModeSelect:Decide()`
// refuses one (mode_select.lua:131-135: play a beep and return without deciding).
bool BoxManager::selection_allowed() const {
    return !(is_2p && boxes[selected_box_index]->location == Screens::DAN_SELECT);
}

void BoxManager::select_box() {
    if (!selection_allowed()) {
        audio.play_sound("kat", VolumePreset::SOUND);
        return;
    }
    fade_out->start();
}

bool BoxManager::is_box_selected() {
    return fade_out->is_started;
}

bool BoxManager::is_finished() {
    return fade_out->is_finished;
}

bool BoxManager::is_costume_box() {
    return boxes[selected_box_index]->location == Screens::ENTRY;
}

void BoxManager::open_costume_menu(PlayerNum player_num) {
    costume_menu_open = true;
    opening_player = player_num;
}

Screens BoxManager::selected_box() {
    return boxes[selected_box_index]->location;
}

void BoxManager::move_left() {
    int prev_selection = selected_box_index;
    if (boxes[prev_selection]->move->is_started && !boxes[prev_selection]->move->is_finished) {
        return;
    }
    selected_box_index = std::max(0, selected_box_index - 1);
    if (prev_selection == selected_box_index) return;
    if (is_vertical) {
        boxes[selected_box_index + 1]->move_down();
        boxes[selected_box_index]->move_down();
    } else {
        if (selected_box_index != selected_box_index - 1) {
            boxes[selected_box_index + 1]->move_right();
        }
        boxes[selected_box_index]->move_right();
    }
}

void BoxManager::move_right() {
    int prev_selection = selected_box_index;
    if (boxes[prev_selection]->move->is_started && !boxes[prev_selection]->move->is_finished) {
        return;
    }
    selected_box_index = std::min(num_boxes - 1, selected_box_index + 1);
    if (prev_selection == selected_box_index) return;
    if (is_vertical) {
        boxes[selected_box_index - 1]->move_up();
        boxes[selected_box_index]->move_up();
    } else {
        if (selected_box_index != 0) {
            boxes[selected_box_index - 1]->move_left();
        }
        boxes[selected_box_index]->move_left();
    }
}

void BoxManager::update(double current_time_ms, bool is_2p) {
    this->is_2p = is_2p;
    // ROUND 86 — `entry_main.lua:1142`'s per-frame CheckBoardListChange poll. Not while the
    // decide fade is running: the cabinet only polls this from its mode-select states, and
    // the screen is already on its way out.
    if (!fade_out->is_started && check_board_list_change()) change_board_list();
    if (this->is_2p) {
        // ROUND 83 — only the 演奏ゲーム board changes screen in 2P. Rewriting the whole
        // list by index broke as soon as the list length became dynamic (the dojo board),
        // so the one entry that differs is remapped by NAME instead.
        for (int i = 0; i < num_boxes; i++) {
            if (box_locations[i] == Screens::SONG_SELECT)
                boxes[i]->location = Screens::SONG_SELECT_2P;
        }
    }
    fade_out->update(current_time_ms);
    for (int i = 0; i < num_boxes; i++) {
        boxes[i]->update(current_time_ms, i == selected_box_index);
    }
}

void BoxManager::draw() {
    for (auto& box : boxes) {
        box->draw(fade_out->attribute);
    }
}
