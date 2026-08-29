#include "song_select.h"
#include "../libs/input.h"
#include "../libs/network.h"
#include "../libs/automation.h"
#include <filesystem>

void SongSelectScreen::on_screen_start() {
    Screen::on_screen_start();
    audio.set_sound_volume("ura_switch", 0.25f);
    audio.set_sound_volume("add_favorite", 3.0f);
    SongBox::reset_bgm_slot();
    audio.play_sound("bgm", VolumePreset::MUSIC);
    audio.play_sound("voice_enter", VolumePreset::VOICE);

    diff_fade_out = (FadeAnimation*)tex.get_animation(2);
    script = std::make_unique<SongSelectScript>();
    navigator.script = script.get();

    shader = load_shader("shader/dummy.vs", "shader/colortransform.fs");

    state = SongSelectState::BROWSING;

    game_transition.reset();
    dan_transition.reset();

    navigator.hide_dan = hides_dan();
    navigator.is_2p = is_2p_screen();
    navigator.init(global_data.config->paths.tja_path);
#ifndef __EMSCRIPTEN__
    stats_future = std::async(std::launch::async, [this]() {
        return navigator.get_statistics(global_data.config->paths.tja_path[0]);
    });
#endif
    navigator.refresh_scores();

    player = std::make_unique<SongSelectPlayer>(global_data.player_num);
    player->script = script.get();

    indicator = std::make_unique<Indicator>(Indicator::State::SELECT);
    song_num = std::make_unique<SongNum>(global_data.songs_played + 1);
    select_timer = std::make_unique<Timer>(100, get_current_ms(), [this]() { player->select_song(); });
    diff_select_timer = nullptr;
    join_request_ms = -1.0;
}

void SongSelectScreen::select_song(SongBox* song) {
    navigator.add_to_recent(song);
    SessionData& session_data = global_data.session_data[(int)global_data.player_num];
    session_data.selected_song = song->path;
    session_data.selected_difficulty = (int)player->selected_difficulty;
    session_data.song_hash = song->hash_for(session_data.selected_difficulty);
    session_data.genre_index = (int)song->song_genre_index - 1;
    global_data.last_difficulty[(int)global_data.player_num] = session_data.selected_difficulty;
    game_transition.emplace(song->text_name, song->text_subtitle, false);
    if (exists(session_data.selected_song.parent_path() / "Loading.png")) {
        game_transition->add_loading_graphic((session_data.selected_song.parent_path() / "Loading.png").string());
    }
    game_transition->start();
}

void SongSelectScreen::handle_input_browsing(double current_ms) {
    state = player->handle_input_browsing(current_ms);
}

void SongSelectScreen::handle_input_selecting() {
    player->handle_input_selecting();
}

void SongSelectScreen::handle_input_diff_sorting() {
    if (!diff_sort_selector) return;
    auto result = player->handle_input_diff_sort(&diff_sort_selector.value());
    if (result) {
        diff_sort_selector.reset();
        state = SongSelectState::BROWSING;
        if (result->first == -1) {
            navigator.cancel_diff_sort();
        } else {
            last_diff_sort = *result;
            navigator.apply_diff_sort(result->first, result->second);
        }
    }
}

// ROUND 15: the arcade window has no "chosen" input event - the third row's don
// starts a 2 s hold and the result appears when the close fade has run out.
void SongSelectScreen::apply_sort_window_result() {
    if (!diff_sort_selector || !diff_sort_selector->is_arcade()) return;
    auto r = diff_sort_selector->take_result();
    if (!r) return;
    diff_sort_selector.reset();
    state = SongSelectState::BROWSING;
    last_diff_sort  = {(*r)[0], (*r)[1]};
    last_diff_order = (*r)[2];
    navigator.apply_diff_sort((*r)[0], (*r)[1], (*r)[2]);
}

void SongSelectScreen::handle_input_search() {
    if (!search_box) return;
    auto result = player->handle_input_search();
    search_box->current_search = player->search_string;
    if (result) {
        navigator.current_search = *result;
        search_box.reset();
        state = SongSelectState::BROWSING;
        navigator.load_current_directory(navigator.get_current_item()->path);
    }
}

void SongSelectScreen::poll_song_jump(double current_ms) {
    static constexpr double SONG_JUMP_POLL_INTERVAL_MS = 3000.0;
    const std::string& access_code = global_data.config->network.access_code;

    if (!access_code.empty() && state == SongSelectState::BROWSING &&
        current_ms - last_song_jump_poll_ms >= SONG_JUMP_POLL_INTERVAL_MS) {
        last_song_jump_poll_ms = current_ms;
        network.poll_song_jump(access_code);
    }

    if (auto hash = network.take_song_jump_result()) {
        navigator.jump_to_song(*hash);
    }
}

// --- mid-song-select 2P join -------------------------------------------------
//
// The cabinet, decoded from 39.06 `Data/x64/script_lua/`:
//
//  * `song_select/song_select_all.lua` `SongSelectAllMain.Update` (l.1848) calls
//    `SecondPlayerJoinToEntry()` every frame while `nextMode ~= SceneType.kEntry`.
//  * `SecondPlayerJoinToEntry` (l.1609) runs `CheckEntry()` only when
//    `songselectsong_:Get2PEntryEnableFlag()`; on a hit it plays
//    `se_common_v12a / entry_2p_add`, sets `nextMode = SceneType.kEntry` and enters
//    `EntryEnd`, which counts `wait_entry_end_cnt = 1.5 * Common.FPS` = 90 frames
//    and then `Common.LuaFinish()`es the whole scene.  It is NOT an overlay and
//    NOT a paused wheel - song select is destroyed and the game goes back to ENTRY.
//  * `CheckEntry` (l.1580) is the input+credit gate: `need_cost <= now_coin or
//    is_freePlay`, with `need_cost = GameCost2 - GameCost1` while `player_num == 1`
//    and `now_coin` forced to 0 while `player_num == 2` (so a full 2P session can
//    never re-trigger).  The input it reads is the UN-JOINED seat's own drum,
//    either face (`Input.PLAYERn_OK or PLAYERn_CANCEL`, guarded by
//    `is_joinPlayer[n] == false`).  We are always free play, so only the input
//    half of that gate exists here.
//  * The window is `playSongCount < 2 and player_num == 1`, set identically by
//    `Set2PEntryEnable` (l.1806) and `song_select_song_main.lua` `StartWait`
//    (l.271): the offer is live on the FIRST AND SECOND song select and gone from
//    the third on.  `CreditSideVisible` (l.1622) shows the invite cloud on exactly
//    the same condition, which is why `coin_overlay.lua` now checks it too.
//  * There is no timeout and no cancel on the join: once it fires the scene is
//    committed and the pair face ENTRY's own timer instead.
//
// The seat that is already in is `global_data.player_num` - `on_screen_start`
// builds this screen's only `SongSelectPlayer` from it - so the seat we watch is
// simply the other one, which makes 1P->2P and 2P->1P exactly symmetric.
std::optional<Screens> SongSelectScreen::poll_second_player_join(double current_ms) {
    // Arcade `EntryEnd`: 90 frames of nothing, then the scene ends.
    static constexpr double JOIN_WAIT_MS = 1.5 * 1000.0;
    if (join_request_ms >= 0.0) {
        if (current_ms - join_request_ms < JOIN_WAIT_MS) return std::nullopt;
        join_request_ms = -1.0;
        global_data.entry_join_pending = true;
        global_data.entry_joined_seat  = join_existing_seat;
        spdlog::info("[2P join] returning to ENTRY, {}P stays in", (int)join_existing_seat);
        return on_screen_end(Screens::ENTRY);
    }

    if (!allows_second_player_join()) return std::nullopt;
    if (!tex.skin_flag("songselect_2p_join")) return std::nullopt;
    // `playSongCount < 2`.
    if (global_data.songs_played >= 2) return std::nullopt;
    // Already committed to a song / a dan course: the arcade's flag is cleared the
    // moment the scene starts tearing down, and re-entering ENTRY from under a
    // running transition would strand the loaded chart.
    if (game_transition.has_value() || dan_transition.has_value()) return std::nullopt;
    if (navigator.is_processing || navigator.inline_streaming) return std::nullopt;
    // The arcade clears the flag for its own sub-screens (the shop at
    // `song_select_all.lua:396`, the QR flow at `song_select_qr.lua:68`).  Ours are
    // the sort window and the search box, both of which own the whole input plane.
    if (state != SongSelectState::BROWSING && state != SongSelectState::SONG_SELECTED)
        return std::nullopt;

    const PlayerNum in  = (global_data.player_num == PlayerNum::P2) ? PlayerNum::P2 : PlayerNum::P1;
    const PlayerNum out = (in == PlayerNum::P1) ? PlayerNum::P2 : PlayerNum::P1;
    if (!is_l_don_pressed(out) && !is_r_don_pressed(out)) return std::nullopt;
    // Drain the rest of this frame's dons on that seat so none survive into ENTRY.
    while (is_l_don_pressed(out) || is_r_don_pressed(out)) {}

    join_request_ms   = current_ms;
    join_existing_seat = in;
    audio.play_sound("don", VolumePreset::SOUND);          // CheckEntry: don_l / don_r
    audio.play_sound("entry_2p_add", VolumePreset::SOUND); // SecondPlayerJoinToEntry
    spdlog::info("[2P join] {}P asked to join from song select; {}P holds", (int)out, (int)in);
    return std::nullopt;
}

void SongSelectScreen::handle_input(double current_ms) {
    if (navigator.is_processing || navigator.inline_streaming) {
        clear_input_buffers();
        return;
    }
    // Ignore input while the difficulty panel is still expanding, matching
    // the cursor which only appears once the animation is done.
    if (state == SongSelectState::SONG_SELECTED && navigator.get_diff_fade_in() < 1.0f) {
        clear_input_buffers();
        return;
    }
    if (state == SongSelectState::BROWSING) {
        handle_input_browsing(current_ms);
    } else if (state == SongSelectState::SONG_SELECTED) {
        handle_input_selecting();
    } else if (state == SongSelectState::DIFF_SORTING) {
        handle_input_diff_sorting();
    } else if (state == SongSelectState::SEARCHING) {
        handle_input_search();
    }
}

std::optional<Screens> SongSelectScreen::update() {
    Screen::update();
    SongSelectState prev_state = state;
    double current_time = get_current_ms();
    allnet_indicator.update(current_time);
    diff_fade_out->update(current_time);
    script->update(current_time);
    // The two auto-pick timers are frozen while the 2P-join hold runs: the arcade
    // is in `EntryEnd`, where `Timer` is no longer ticked, and a timer firing
    // inside those 90 frames would start a song the scene is already leaving.
    if (join_request_ms < 0.0) {
        select_timer->update(current_time);
        if (diff_select_timer != nullptr) diff_select_timer->update(current_time);
    }
    indicator->update(current_time);
    if (search_box) search_box->update(current_time);

    if (navigator.diff_sort_ready() && !diff_sort_selector) {
        if (stats_future.valid()) {
            stats_future.wait();
            cached_stats = stats_future.get();
        }
        diff_sort_selector.emplace(cached_stats, last_diff_sort.first, last_diff_sort.second,
                                   script.get(), last_diff_order);
    }
    if (diff_sort_selector) {
        state = SongSelectState::DIFF_SORTING;
        diff_sort_selector->update(current_time);
        apply_sort_window_result();
    }

    poll_song_jump(current_time);
    // ROUND 26 (r26-gaugesliver): automation `gotosong <path>` -- see
    // automation.h/.cpp and Navigator::jump_to_song_path. Testing-only, same
    // shape as the network poll_song_jump() call directly above.
    {
        std::string jump_path;
        if (automation_take_song_jump(jump_path)) {
            navigator.jump_to_song_path(std::filesystem::path(jump_path));
        }
    }
    // Polled before the player's own input, like the arcade's `Update`. While the
    // 90-frame hold runs the scene is already committed to ENTRY, so nothing else
    // may consume input - the arcade is in `EntryEnd`, not in `Main`, for those
    // 1.5 s and cannot start a song either.
    if (auto join = poll_second_player_join(current_time)) return join;
    if (join_request_ms >= 0.0) {
        clear_input_buffers();
    } else {
        handle_input(current_time);
    }

    player->update(current_time);
    if (player->is_ready && !game_transition.has_value() && join_request_ms < 0.0) {
        if (player->selected_difficulty >= Difficulty::EASY) {
            BaseBox* item = navigator.get_current_item();
            select_song((SongBox*)item);
        } else if (player->selected_difficulty == Difficulty::BACK) {
            navigator.exit_diff_select();
            state = SongSelectState::BROWSING;
            player->reset_selection();
        }
    }

    if (screen_init) navigator.update(current_time);

    // ...and neither transition may complete out from under the join hold.
    if (game_transition.has_value() && join_request_ms < 0.0) {
        game_transition->update(current_time);
        if (game_transition->is_finished()) {
            return on_screen_end(get_game_screen_target());
        }
    }

    if (dan_transition.has_value() && join_request_ms < 0.0) {
        dan_transition->update(current_time);
        if (dan_transition->is_finished()) {
            return on_screen_end(Screens::DAN_SELECT);
        }
    }

    if (check_key_pressed(global_data.config->keys.back_key) && global_data.config->general.song_limit <= 0) {
        return on_screen_end(Screens::ENTRY);
    }

    if (state != prev_state) {
        script->restart_text_fade();
        if (prev_state == SongSelectState::SEARCHING)
            android_set_keyboard_visible(false);
        if (state == SongSelectState::SONG_SELECTED) {
            diff_select_timer = std::make_unique<Timer>(60, current_time, [this]() { select_song((SongBox*)navigator.get_current_item()); });
        } else if (state == SongSelectState::SEARCHING) {
            search_box.emplace();
            android_set_keyboard_visible(true);
        } else if (state == SongSelectState::DAN_SELECTED) {
            dan_transition.emplace();
            dan_transition->start();
        }
    }

    return std::nullopt;
}

Screens SongSelectScreen::on_screen_end(Screens next_screen) {
    navigator.join_loader();
    ray::UnloadShader(shader);
    return Screen::on_screen_end(next_screen);
}

void SongSelectScreen::draw_overlays() {
    script->draw_overlays(state);

    tex.draw_texture(GLOBAL::SONG_NUM_BG, {.x=-(song_num->width-127), .x2=(song_num->width-127), .fade=0.75});
    song_num->draw(tex.skin_config[SC::SONG_NUM].x-song_num->width, tex.skin_config[SC::SONG_NUM].y, 1.0);
    if (state == SongSelectState::SONG_SELECTED) {
        if (diff_select_timer) diff_select_timer->draw();
    } else {
        select_timer->draw();
    }
    allnet_indicator.draw();
    coin_overlay.draw();
    indicator->draw(tex.skin_config[SC::SONG_SELECT_INDICATOR].x, tex.skin_config[SC::SONG_SELECT_INDICATOR].y);
}

void SongSelectScreen::draw() {
    navigator.draw_background();
    player->draw_background_diffs(state);
    bool in_diff_select = player->selected_song && state == SongSelectState::SONG_SELECTED;
    // ROUND 15: pass 0 of the skin's selector - the yellow course frame / button
    // glow, which the cabinet keeps UNDER the difficulty cards. Pass 1 (the 1P/2P
    // bubble) runs from SongSelectPlayer::draw, after the wheel.
    if (in_diff_select) {
        navigator.draw_diff_select_bg();
        player->try_lua_selector(false, navigator.get_diff_fade_in(), 0);
    }
    navigator.draw();
    script->draw_footer();

    player->draw(state, false, navigator.get_diff_fade_in());

    draw_overlays();

    if (screen_init) navigator.draw_score_history();

    if (diff_sort_selector) diff_sort_selector->draw();
    if (search_box) search_box->draw();
    if (game_transition.has_value()) {
        game_transition->draw();
        // The arcade keeps the credit/free-play line visible over the song-loading
        // screen; re-draw the coin overlay on top with in_transition set so the skin
        // can drop the 2P invite / QR chip (see coin_overlay.lua).
        global_data.in_transition = true;
        coin_overlay.draw();
        global_data.in_transition = false;
    }
    // The skin's top-most slot, above the whole song-select HUD. The dan
    // shutter rig lives here; the engine's own DAN_TRANSITION quad (one
    // fixed-x quad whose width animates, i.e. a wipe) still draws after it and
    // a skin that wants to own the transition neutralises it with "y2": 0.
    script->draw_top(dan_transition.has_value() ? (float)dan_transition->progress() : -1.0f);

    if (dan_transition.has_value()) dan_transition->draw();
}
