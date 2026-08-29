#include "game_2p.h"
#include "../libs/input.h"

// ROUND 58 (r58-2p-background-polish): the on_screen_start() override that
// re-emplaced background as TWO_PLAYER (and rebuilt result_transition) after
// the base had already constructed the P1 versions is gone -- the base now
// asks scene_player_num() (game_2p.h) and builds the TWO_PLAYER objects
// directly, so a SCENEPRESET chart no longer builds+destroys a full collab
// rig during 2P screen init (ROUND 52 found-in-passing).

void Game2PScreen::init_tja(fs::path song) {
    int delay = (song.extension() == ".osu") ? 0 : start_delay;
    parser = SongParser(song, delay, PlayerNum::P1);
    parser_2p = SongParser(song, delay, PlayerNum::P2);

    if (fs::exists(parser->metadata.bgmovie)) {
        movie.emplace(parser->metadata.bgmovie);
    }

    auto& titles = parser->metadata.title;
    auto& subtitles = parser->metadata.subtitle;
    const std::string& lang = global_data.config->general.language;
    std::string title = titles.count(lang) ? titles.at(lang) : titles.count("en") ? titles.at("en") : titles.empty() ? "" : titles.begin()->second;
    std::string subtitle = subtitles.count(lang) ? subtitles.at(lang) : "";

    global_data.session_data[(int)PlayerNum::P1].song_title = title;
    global_data.session_data[(int)PlayerNum::P1].song_subtitle = subtitle;
    global_data.session_data[(int)PlayerNum::P1].song_subtitle_full_display = parser->metadata.subtitle_full_display;
    global_data.session_data[(int)PlayerNum::P2].song_title = title;
    global_data.session_data[(int)PlayerNum::P2].song_subtitle = subtitle;
    global_data.session_data[(int)PlayerNum::P2].song_subtitle_full_display = parser->metadata.subtitle_full_display;

    if (fs::exists(parser->metadata.wave) && !song_music.has_value()) {
        song_music = audio.load_sound(parser->metadata.wave, "song");
    }

    players.push_back(std::make_unique<Player>(
        parser, PlayerNum::P1,
        global_data.session_data[(int)PlayerNum::P1].selected_difficulty, false,
        get_player_modifiers(PlayerNum::P1)));
    players.push_back(std::make_unique<Player>(
        parser_2p, PlayerNum::P2,
        global_data.session_data[(int)PlayerNum::P2].selected_difficulty, true,
        get_player_modifiers(PlayerNum::P2)));

    // ROUND 25 (r25-kusudama2p): wire the two seated players together so their
    // kusudama (段位鼓) hits sum into one shared pool/pop instead of two
    // independent per-player counters -- see ENGINE_BINDINGS.md ROUND 25 and
    // player.h's kusudama_partner/kusudama_owner(). 1P (game.cpp) and DAN
    // (game_dan.cpp) never call this, so kusudama_partner stays nullptr there
    // and their kusudama behaviour is completely unchanged.
    players[0]->kusudama_partner = players[1].get();
    players[1]->kusudama_partner = players[0].get();

    start_ms = get_current_ms() - parser->metadata.offset * 1000;
}

std::optional<Screens> Game2PScreen::update() {
    Screen::update();

    double current_time = get_current_ms();
    transition->update(current_time);
    if (!paused) {
        ms_from_start = current_time - start_ms;
    }
    poll_pending_song();
    if (transition->is_finished()) {
        start_song(ms_from_start);
        global_data.input_locked = 0;
    }
    // ROUND 20: this branch used to re-pin `start_ms = current_time -
    // offset*1000` every frame the transition curtain was still up, which --
    // because ms_from_start for THIS frame is computed above from the PREVIOUS
    // frame's start_ms -- froze ms_from_start near a constant +offset*1000 for
    // the whole loading transition instead of running continuously the way
    // GameScreen (1P) does from on_screen_start onward (game.cpp never resets
    // start_ms in update()). Any note/branch/gogo event with hit_ms below that
    // offset was evaluated as already-past the instant the curtain lifted,
    // which is exactly the class of "wrong drive source" bug this round is
    // auditing for. start_ms is already set once in on_screen_start() (and in
    // the restart handler below), so removing the per-frame override is enough
    // to match 1P's single-continuous-clock mechanism.

    // ROUND 19-sp: was `resync_song(ms_from_start)` — same wrong-clock bug that
    // r17-dan fixed in game_dan.cpp. `GameScreen::resync_song` expects the
    // absolute wall-clock value so it can compute `frame_delta = current_ms -
    // last_resync_ms` and `start_ms = current_ms - ms_from_start`. Fed the
    // relative `ms_from_start` (~a few thousand ms), it stored that as
    // `last_resync_ms` and set `start_ms = 0`, so the very next frame computed
    // `ms_from_start = current_time - 0 = epoch_time (~1.75e12 ms)`, triggering
    // a hard-resync every frame and making the correction_rate calculation bogus.
    resync_song(current_time);

    update_background(current_time);

    for (auto& p : players) {
        p->update(ms_from_start, current_time, background);
    }
    song_info.update(current_time);
    result_transition.update(current_time);

    if (result_transition.is_finished && !audio.is_sound_playing("result_transition")) {
        return on_screen_end(Screens::RESULT_2P);
    }
    else if (ms_from_start >= players[0]->end_time) {
        if (ms_from_start >= players[0]->end_time + 1000 && !score_saved) {
            global_data.session_data[global_data.config->general.player_1_id].result_data = players[0]->get_result_score();
            global_data.session_data[global_data.config->general.player_2_id].result_data = players[1]->get_result_score();
            save_score(global_data.config->general.player_1_id, PlayerNum::P1);
            save_score(global_data.config->general.player_2_id, PlayerNum::P2);
            for (int i = 0; i < 2; i++) {
                players[i]->spawn_ending_anim();
            }
            global_data.songs_played += 1;
            score_saved = true;
        }
        if (ms_from_start >= players[0]->end_time + 8533.34) {
            if (!result_transition.is_started) {
                result_transition.start();
                audio.play_sound("result_transition", VolumePreset::VOICE);
            }
        }
    }

    if (ray::IsKeyPressed(global_data.config->keys.restart_key)) {
        if (song_music.has_value()) audio.stop_sound(song_music.value());
        players.clear();
        parser_2p.reset();
        init_tja(global_data.session_data[(int)PlayerNum::P1].selected_song);
        audio.play_sound("restart", VolumePreset::SOUND);
        song_started = false;
        score_saved = false;
        paused = false;
        pause_time = 0;
        last_resync_ms = 0;
        // See GameScreen::restart_song - reset the chart clock or the song
        // starts immediately, skipping the lead-in (#83).
        start_ms = get_current_ms() - parser->metadata.offset*1000 - (double)global_data.config->general.audio_offset;
        ms_from_start = get_current_ms() - start_ms;
    }
    if (check_key_pressed(global_data.config->keys.back_key)) {
        if (song_music.has_value()) audio.stop_sound(song_music.value());
        return on_screen_end(Screens::SONG_SELECT_2P);
    }
    if (ray::IsKeyPressed(global_data.config->keys.pause_key)) {
        pause_song();
    }

    return std::nullopt;
}
