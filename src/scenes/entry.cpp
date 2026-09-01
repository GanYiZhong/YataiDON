#include "entry.h"
#include "../libs/input.h"
#include "../libs/scores.h"

void EntryScreen::on_screen_start() {
    Screen::on_screen_start();
    side = 1;
    is_2p = false;
    // ROUND 86 — `CreateBoardList` is handed BOTH seats' playdata, so a screen that boots
    // already 2P must never build the 段位道場 board in the first place. The only route
    // into ENTRY with a seat already entered is the mid-song-select 2P join below
    // (`entry_join_pending` -> start_second_player_join), which the cabinet reaches through
    // `StartBGIn`'s `StartModeSelect` branch with both seats at kCoin.
    box_manager = std::make_unique<BoxManager>(global_data.entry_join_pending);
    state = EntryState::SELECT_SIDE;

    {
        auto pd = scores_manager.get_player_data(global_data.config->general.player_1_id);
        nameplate = Nameplate(
            pd ? pd->username : "", pd ? pd->title : "",
            PlayerNum::ALL,
            pd ? pd->dan : -1, pd ? pd->gold : false, pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
    }

    timer = std::make_unique<Timer>(60, get_current_ms(), [this]() {
        if (box_manager->is_costume_box()) {
            box_manager->open_costume_menu(global_data.player_num);
        } else {
            // ROUND 83 — `ModeSelect:TimeUp()` (mode_select.lua:155-163) always
            // decides something. `select_box()` can now refuse (段位道場 is
            // 1P-only, `IsOnePlayerOnly(kDani)`), and a refused time-up would
            // leave the screen with no way out, so step the cursor off the
            // board this session may not take before deciding — the cabinet
            // would never have offered it in the first place.
            if (!box_manager->selection_allowed()) box_manager->move_left();
            box_manager->select_box();
        }
    });

    lua_entry = std::make_unique<EntryScript>();
    lua_entry->start_side_select();
    reload_preview_chara(global_data.config->general.player_1_id);
    announce_played = false;
    players.clear();
    players.resize(2);
    audio.play_sound("bgm", VolumePreset::MUSIC);

    if (global_data.entry_join_pending) {
        global_data.entry_join_pending = false;
        start_second_player_join();
    }
}

// ROUND 15 - the ENTRY half of the cabinet's mid-song-select 2P join.
//
// `entry/entry_main.lua` `StartBGIn` (l.314): when the scene boots with the engine
// global `StartModeSelect == true` it does NOT go to `CreditWaitStart`.  It runs
//
//     for loop_idx = 1, 2 do
//         if PlayDataManager.GetUserType(loop_idx - 1) == UserType.kNone then
//             PlayDataManager.Entry(loop_idx - 1, UserType.kCoin)
//         end
//     end
//     ... SetupEntryData(true) / ChangeDonForPlayData / LoadChangeStandChara ...
//     -> StartLoadWait -> StartLoadWait2 (DispDonAndNamePlate) -> ModeSelectStart
//
// i.e. every seat still at kNone is entered on the spot and the scene lands
// directly in MODE SELECT with both Dons up.  `nextMode = SceneType.kEntry` in
// `song_select_all.lua:1619` is the only route to ENTRY that can leave a seat
// already at kCoin, so that branch exists for exactly this case.  The consequence
// worth stating out loud: **the newcomer does not press anything a second time** -
// the drum hit he made on song select IS his entry - and there is therefore no
// second credit-wait screen and nothing for him to time out of.
//
// What is deliberately NOT preserved, because the cabinet does not preserve it
// either: the first player's MODE choice.  The pair re-run mode select together.
void EntryScreen::start_second_player_join() {
    const PlayerNum first  = global_data.entry_joined_seat;
    const PlayerNum second = (first == PlayerNum::P1) ? PlayerNum::P2 : PlayerNum::P1;

    // Rebuild the seat that was already in, WITHOUT touching
    // `global_data.first_login_player`: it still names `first`, so `get_player_id`
    // keeps mapping him to player_1_id and the late joiner to player_2_id. That is
    // the whole identity guarantee - nameplate, costume, modifiers and 音色 all
    // hang off the player_id, so none of them can be swapped by this round trip.
    side = (first == PlayerNum::P1) ? 0 : 2;
    global_data.player_num = first;
    players[0] = std::make_unique<EntryPlayer>(first, side, box_manager.get());
    players[0]->start_animations();

    // ...and enter the newcomer, exactly like `PlayDataManager.Entry(idx, kCoin)`.
    const int second_side = (second == PlayerNum::P1) ? 0 : 2;
    global_data.player_num = second;
    players[1] = std::make_unique<EntryPlayer>(second, second_side, box_manager.get());
    players[1]->start_animations();
    audio.play_sound("cloud", VolumePreset::SOUND);
    audio.play_sound("entry_start_" + std::to_string((int)second) + "p", VolumePreset::VOICE);

    // join_player() parks player_num back on P1 once a session is 2P; the 2P scenes
    // build their two SongSelectPlayers from explicit PlayerNums, so this only has
    // to stay consistent with the existing path.
    global_data.player_num = PlayerNum::P1;
    is_2p = true;
    side = 1;
    state = EntryState::SELECT_MODE;
    spdlog::info("[2P join] ENTRY resumed: {}P kept (first_login {}P), {}P entered, mode select",
                 (int)first, (int)global_data.first_login_player, (int)second);
}

void EntryScreen::reload_preview_chara(int player_id) {
    auto pd = scores_manager.get_player_data(player_id);
    chara = make_chara_from_player_data(pd ? &*pd : nullptr);
    if (pd) {
        chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
        chara->apply_face(pd->chara_face_index);
    } else {
        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
    }
}

Screens EntryScreen::on_screen_end(Screens next_screen) {
    audio.stop_sound("bgm");
    return Screen::on_screen_end(next_screen);
}

// ROUND 12 — the arcade credit-wait interaction model, opt-in per skin.
//
// `entry_main.tlb`'s `CheckEntry` (lines 2515-2543) polls
// `Input.GetTrigger(PLAYERn_OK / PLAYERn_CANCEL)` — which are that seat's RIGHT DON and
// LEFT DON (`research/bnusio.md` §8) — and calls `PlayDataManager.Entry(n-1, kCoin)` for
// the seat that hit.  There is no cursor, no cancel widget and no kat handling: the two
// yellow rows are prompts, both lit, and the seat you hit decides who enters.
bool EntryScreen::arcade_credit() const {
    return tex.skin_flag("entry_credit_arcade");
}

bool EntryScreen::seat_joined(PlayerNum player_num) const {
    for (auto& player : players) {
        if (player && player->player_num == player_num) return true;
    }
    return false;
}

// The body of the old SELECT_SIDE don branch, keyed by seat instead of by cursor.
void EntryScreen::join_player(PlayerNum player_num) {
    side = (player_num == PlayerNum::P1) ? 0 : 2;
    global_data.player_num = player_num;

    if (players[0]) {
        players[1] = std::make_unique<EntryPlayer>(global_data.player_num, side, box_manager.get());
        players[1]->start_animations();
        global_data.player_num = PlayerNum::P1;
        is_2p = true;
    } else {
        global_data.first_login_player = global_data.player_num;
        players[0] = std::make_unique<EntryPlayer>(global_data.player_num, side, box_manager.get());
        players[0]->start_animations();
        is_2p = false;
    }
    audio.play_sound("cloud", VolumePreset::SOUND);
    audio.play_sound("entry_start_" + std::to_string((int)global_data.player_num) + "p", VolumePreset::VOICE);
    // The engine has no CreditDecideAction state, so tell the skin which row to flash
    // before EntryState leaves SELECT_SIDE (a no-op for skins without the hook).
    if (state == EntryState::SELECT_SIDE) lua_entry->decide_side_select(side);
    state = EntryState::SELECT_MODE;
    audio.play_sound("don", VolumePreset::SOUND);
}

std::optional<Screens> EntryScreen::handle_input() {
    if (arcade_credit() && state == EntryState::SELECT_SIDE) {
        // Any DON on a seat that has not entered yet enters that seat, immediately.
        if (!seat_joined(PlayerNum::P1) &&
            (is_l_don_pressed(PlayerNum::P1) || is_r_don_pressed(PlayerNum::P1))) {
            join_player(PlayerNum::P1);
        } else if (!seat_joined(PlayerNum::P2) &&
                   (is_l_don_pressed(PlayerNum::P2) || is_r_don_pressed(PlayerNum::P2))) {
            join_player(PlayerNum::P2);
        }
        return std::nullopt;
    }
    if (state == EntryState::SELECT_SIDE) {
        if (is_l_don_pressed() || is_r_don_pressed()) {
            if (side == 1) {
                if (players[0]) {
                    // The side select was opened to add a second player -
                    // cancelling it goes back to the first player's mode
                    // select instead of dropping the whole entry to title.
                    audio.play_sound("don", VolumePreset::SOUND);
                    state = EntryState::SELECT_MODE;
                    return std::nullopt;
                }
                return on_screen_end(Screens::TITLE);
            }
            global_data.player_num = (side == 0) ? PlayerNum::P1 : PlayerNum::P2;

            if (players[0]) {
                players[1] = std::make_unique<EntryPlayer>(global_data.player_num, side, box_manager.get());
                players[1]->start_animations();
                global_data.player_num = PlayerNum::P1;
                is_2p = true;
            } else {
                global_data.first_login_player = global_data.player_num;
                players[0] = std::make_unique<EntryPlayer>(global_data.player_num, side, box_manager.get());
                players[0]->start_animations();
                is_2p = false;
            }
            audio.play_sound("cloud", VolumePreset::SOUND);
            audio.play_sound("entry_start_" + std::to_string((int)global_data.player_num) + "p", VolumePreset::VOICE);
            state = EntryState::SELECT_MODE;
            audio.play_sound("don", VolumePreset::SOUND);
        }
        if (is_l_kat_pressed()) {
            audio.play_sound("kat", VolumePreset::SOUND);
            if (players[0] && players[0]->player_num == PlayerNum::P1)
                side = 1;
            else if (players[0] && players[0]->player_num == PlayerNum::P2)
                side = 0;
            else
                side = std::max(0, side - 1);
        }
        if (is_r_kat_pressed()) {
            audio.play_sound("kat", VolumePreset::SOUND);
            if (players[0] && players[0]->player_num == PlayerNum::P1)
                side = 2;
            else if (players[0] && players[0]->player_num == PlayerNum::P2)
                side = 1;
            else
                side = std::min(2, side + 1);
        }
    } else if (state == EntryState::SELECT_COSTUME) {
        for (auto& player : players) {
            if (player) player->handle_input();
        }
    } else if (state == EntryState::SELECT_MODE) {
        if (arcade_credit()) {
            // The cabinet never re-enters the credit screen for the 2nd player:
            // `CheckEntry` keeps being polled through ModeSelectStart /
            // ModeSelectStartWait / ModeSelect / ModeSelectChangeList, so the seat that
            // hits joins IN PLACE (its `credit_side_` invite cloud just disappears).
            // Polled before the mode-boxes-ready gate, exactly like the arcade.
            if (!seat_joined(PlayerNum::P1) &&
                (is_l_don_pressed(PlayerNum::P1) || is_r_don_pressed(PlayerNum::P1))) {
                join_player(PlayerNum::P1);
                return std::nullopt;
            }
            if (!seat_joined(PlayerNum::P2) &&
                (is_l_don_pressed(PlayerNum::P2) || is_r_don_pressed(PlayerNum::P2))) {
                join_player(PlayerNum::P2);
                return std::nullopt;
            }
            if (!mode_select_ready()) return std::nullopt;
            for (auto& player : players) {
                if (player) player->handle_input();
            }
            return std::nullopt;
        }
        // The mode boxes are only drawn once every player's entry animation
        // has played out; don't let them be picked while they are invisible.
        if (!mode_select_ready()) return std::nullopt;
        for (auto& player : players) {
            if (player) player->handle_input();
        }
        if (players[0] && players[0]->player_num == PlayerNum::P1 && (is_l_don_pressed(PlayerNum::P2) || is_r_don_pressed(PlayerNum::P2))) {
            audio.play_sound("don", VolumePreset::SOUND);
            state = EntryState::SELECT_SIDE;
            {
                auto pd = scores_manager.get_player_data(global_data.config->general.player_2_id);
                nameplate = Nameplate(
                    pd ? pd->username : "", pd ? pd->title : "",
                    PlayerNum::ALL,
                    pd ? pd->dan : -1, pd ? pd->gold : false, pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
            }
            lua_entry->restart_side_select();
            side = 1;
            reload_preview_chara(global_data.config->general.player_2_id);
        } else if (players[0] && players[0]->player_num == PlayerNum::P2 && (is_l_don_pressed(PlayerNum::P1) || is_r_don_pressed(PlayerNum::P1))) {
            audio.play_sound("don", VolumePreset::SOUND);
            state = EntryState::SELECT_SIDE;
            lua_entry->restart_side_select();
            side = 1;
            reload_preview_chara(global_data.config->general.player_2_id);
        }
    }
    return std::nullopt;
}

std::optional<Screens> EntryScreen::update() {
    Screen::update();
    double current_time = get_current_ms();
    allnet_indicator.update(current_time);
    entry_overlay.update(current_time);
    lua_entry->update(current_time);
    box_manager->update(current_time, is_2p);
    // ROUND 20: `entry_main.lua` `CreditWaitStart` leaves the 60 s dial STOPPED
    // (`SetShadow(true)` / no `StartCount()`) and only counts it at all when
    // `IsTimerCountInCreditWait()` is true — i.e. a card or QR session is
    // present.  This engine has no card/QR entry path, so that predicate is
    // always false: under the arcade credit model the dial must never run
    // during `SELECT_SIDE` (it starts only once `ModeSelectStartWait` is
    // reached, i.e. our `SELECT_MODE`).  Skipping `update()` here freezes the
    // Lua-side `last_time` cleanly (it is re-stamped to `current_ms` on every
    // tick it *does* see, so no catch-up burst on resume) — no new engine
    // state needed.  Off by default: skins without `entry_credit_arcade` keep
    // the old unconditional countdown (legacy 3-way cursor has no card/QR
    // concept to key this off either).
    if (!(arcade_credit() && state == EntryState::SELECT_SIDE)) {
        timer->update(current_time);
    }
    nameplate.update(current_time);
    chara->update(current_time);
    for (auto& player : players) {
        if (player) player->update(current_time);
    }
    if (box_manager->costume_menu_open) {
        box_manager->costume_menu_open = false;
        state = EntryState::SELECT_COSTUME;
        for (auto& player : players) {
            if (player && player->player_num == box_manager->opening_player)
                player->open_costume_menu();
        }
    }
    if (state == EntryState::SELECT_COSTUME) {
        bool any_open = false;
        for (auto& player : players) {
            if (player && player->costume_menu.has_value()) { any_open = true; break; }
        }
        if (!any_open) state = EntryState::SELECT_MODE;
    }
    if (box_manager->is_finished()) {
        return on_screen_end(box_manager->selected_box());
    }
    for (auto& player : players) {
        if (player && player->is_cloud_animation_finished() &&
            !audio.is_sound_playing("entry_start_" + std::to_string((int)global_data.player_num) + "p") &&
            !announce_played) {
            audio.play_sound("select_mode", VolumePreset::VOICE);
            announce_played = true;
        }
    }
    return handle_input();
}

void EntryScreen::draw_background() {
    lua_entry->draw_background();
}

void EntryScreen::draw_side_select(float fade) {
    auto& skin = tex.skin_config;
    lua_entry->draw_side_select();

    // ROUND 15 - `DispDonAndNamePlate()` (`script_lua/entry/entry_main.lua`): the cabinet's
    // preview Don and its nameplate exist PER SEAT and are visible only while that seat's
    // `play_data_[n].user_type ~= kNone`, i.e. only after that player has actually entered.
    // On the credit-wait screen in free play with nobody entered the cabinet therefore shows
    // no Don and no plate at all - the two 太鼓をたたいてスタート! rows stand on the bare
    // street.  We drew both unconditionally from the first frame of the screen
    // (`on_screen_start` -> `reload_preview_chara`), which is the user's
    // 「現在 1p 還沒進入遊戲，但 3d don 就會顯示」.
    // Once a seat joins, its Don comes from that seat's own `EntryPlayer` (`draw_player_drum`
    // / `draw_nameplate_and_indicator`), so the shared preview pair is never the right thing
    // to show here; gating it is the whole fix.  Only under the arcade credit model - the
    // legacy 3-way side cursor is a YataiDON screen with no cabinet counterpart and keeps its
    // preview so the player can see who he is about to be.
    const bool show_preview = !arcade_credit() || seat_joined(PlayerNum::P1) || seat_joined(PlayerNum::P2);
    if (show_preview) {
        chara->draw(tex.skin_config[SC::CHARA_ENTRY].x, tex.skin_config[SC::CHARA_ENTRY].y);
    }
    lua_entry->draw_side_select_buttons(side);
    if (show_preview) {
        nameplate.draw(skin[SC::NAMEPLATE_ENTRY].x, skin[SC::NAMEPLATE_ENTRY].y, fade);
    }
}

void EntryScreen::draw_player_drum() {
    for (auto& player : players) {
        if (player) player->draw_drum();
    }
}

bool EntryScreen::mode_select_ready() {
    for (auto& player : players) {
        if (player && !player->is_cloud_animation_finished()) return false;
    }
    return true;
}

void EntryScreen::draw_mode_select() {
    if (!mode_select_ready()) return;
    box_manager->draw();
}

void EntryScreen::draw() {
    draw_background();
    draw_player_drum();

    if (state == EntryState::SELECT_SIDE) {
        draw_side_select(lua_entry->get_side_select_fade());
    } else if (state == EntryState::SELECT_MODE) {
        draw_mode_select();
    } else if (state == EntryState::SELECT_COSTUME) {
        for (auto& player : players) {
            if (player) player->draw_costume_menu();
        }
    }

    bool p1_joined = (players[0] && players[0]->player_num == PlayerNum::P1) ||
                     (players[1] && players[1]->player_num == PlayerNum::P1);
    bool p2_joined = (players[0] && players[0]->player_num == PlayerNum::P2) ||
                     (players[1] && players[1]->player_num == PlayerNum::P2);
    lua_entry->draw_footer(p1_joined, p2_joined);

    for (auto& player : players) {
        if (player) {
            player->draw_nameplate_and_indicator(player->get_nameplate_fadein());
        }
    }

    lua_entry->draw_player_entry();

    if (box_manager->is_finished()) {
        ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::BLACK);
    }

    timer->draw();
    entry_overlay.draw(0, tex.skin_config[SC::ENTRY_OVERLAY_ENTRY].y);
    coin_overlay.draw();
    allnet_indicator.draw();
}
