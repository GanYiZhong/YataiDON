#include "game_practice.h"
#include "../libs/input.h"
#include <cmath>

void PracticeGameScreen::on_screen_start() {
    GameScreen::on_screen_start();
    if (!movie.has_value()) {
        background.emplace(global_data.player_num, bpm, "PRACTICE");
    }
    pause_don_anim   = (TextureResizeAnimation*)tex.get_animation(67, true);
    pause_kat_anim   = (TextureResizeAnimation*)tex.get_animation(67, true);
    resume_don_anim  = (TextureResizeAnimation*)tex.get_animation(67, true);
    skip_l_kat_anim  = (TextureResizeAnimation*)tex.get_animation(67, true);
    skip_r_kat_anim  = (TextureResizeAnimation*)tex.get_animation(67, true);
    menu_don_anim    = (TextureResizeAnimation*)tex.get_animation(67, true);
    speed_l_kat_anim = (TextureResizeAnimation*)tex.get_animation(67, true);
    speed_r_kat_anim = (TextureResizeAnimation*)tex.get_animation(67, true);
    init_tja_practice(global_data.session_data[(int)global_data.player_num].selected_song);
}

Screens PracticeGameScreen::on_screen_end(Screens next_screen) {
    scrobble_index = 0;
    scrobble_time = 0;
    menu_open  = false;
    menu_index = 0;
    jump_bar   = -1;
    menu_text.clear();
    scrobble_move = std::make_unique<MoveAnimation>(200.0, 0);
    bars.clear();
    scrobble_note_list.clear();
    markers.clear();
    return GameScreen::on_screen_end(next_screen);
}

void PracticeGameScreen::init_tja(fs::path song) {
    GameScreen::init_tja(song);
    if (!players.empty()) {
        players.back() = std::make_unique<PracticePlayer>(
            parser,
            global_data.player_num,
            global_data.session_data[(int)global_data.player_num].selected_difficulty,
            false,
            get_player_modifiers(global_data.player_num));
        practice_player = static_cast<PracticePlayer*>(players.back().get());
    }
}

void PracticeGameScreen::init_tja_practice(const fs::path& song) {
    // Extract bars and note list for scrobbling from the already-parsed TJA
    int difficulty = global_data.session_data[(int)global_data.player_num].selected_difficulty;
    auto [notes, bm, be, bn] = parser->notes_to_position(difficulty);
    // Only a TJA has scroll commands to disable; asking the variant for one
    // when an arcade chart is loaded threw and kept practice mode out of
    // those songs entirely.
    if (auto* tja = std::get_if<TJAParser>(&parser->impl))
        tja->scroll_disabled = true;
    apply_modifiers(notes, get_player_modifiers(global_data.player_num));

    bars.clear();
    scrobble_note_list.clear();
    markers.clear();

    for (const auto& note : notes.notes) {
        scrobble_note_list.push_back(note);
        if (note.type == NoteType::BARLINE) {
            bars.push_back(note);
        }
    }

    for (const auto& tl : notes.timeline) {
        if (tl.gogo_time.has_value() && tl.gogo_time.value()) {
            markers.push_back(tl.start_time);
        }
    }

    if (!bars.empty()) {
        scrobble_index = 0;
        scrobble_time = bars[0].hit_ms;
    }
    scrobble_move = std::make_unique<MoveAnimation>(200.0, 0);
}

void PracticeGameScreen::pause_song_practice() {
    paused = !paused;
    if (practice_player) practice_player->paused = paused;

    if (paused) {
        if (song_music.has_value()) {
            audio.stop_sound(song_music.value());
        }
        pause_time = (int)(get_current_ms() - start_ms);

        if (bars.empty()) return;
        double first_bar_time = bars[0].hit_ms;
        int nearest_bar_index = 0;
        double min_distance = std::numeric_limits<double>::infinity();
        for (int i = 0; i < (int)bars.size(); ++i) {
            double bar_relative_time = bars[i].hit_ms - first_bar_time;
            double distance = std::abs(bar_relative_time - ms_from_start);
            if (distance < min_distance) {
                min_distance = distance;
                nearest_bar_index = i;
            }
        }
        scrobble_index = std::max(0, nearest_bar_index - 1);
        scrobble_time = bars[scrobble_index].hit_ms;
    } else {
        if (bars.empty()) return;

        int resume_bar_index = std::max(0, scrobble_index);
        int previous_bar_index = std::max(0, scrobble_index - global_data.config->general.practice_mode_bar_delay);

        double first_bar_time = bars[0].hit_ms;
        double resume_time = bars[resume_bar_index].hit_ms - first_bar_time + start_delay;
        double start_time  = bars[previous_bar_index].hit_ms - first_bar_time + start_delay;

        if (practice_player) {
            // The lead-in bars before the scrobbled bar stay an empty
            // runway: seeking to the scrobbled bar drops their notes from
            // both the hit queues and the draw path.
            practice_player->seek_to(resume_time);
            // A fresh attempt: score, combo and the judgement tallies all
            // start over rather than keeping the last run's numbers.
            practice_player->reset_performance();
        }

        pause_time = (int)start_time;

        if (song_music.has_value()) {
            audio.play_sound(song_music.value(), VolumePreset::MUSIC);
            double seek_sec = (start_time - start_delay) / 1000.0 - parser->metadata.offset;
            audio.seek_sound(song_music.value(), std::max(0.0, seek_sec));
            audio.set_sound_pitch(song_music.value(), song_speed / 10.0f);
        }
        song_started = true;
        start_ms = get_current_ms() - pause_time;
        // update() computed ms_from_start at the top of this frame while we
        // were still paused, so it holds the stale pause position. The player
        // update later this frame would sweep the freshly seeked queues with
        // that stale clock - instantly missing the notes right after the
        // resume point and pair-popping drumrolls between the scrobble target
        // and wherever the pause happened.
        ms_from_start = start_time;
    }
}

void PracticeGameScreen::restart_practice() {
    if (song_music.has_value()) {
        audio.stop_sound(song_music.value());
    }
    players.clear();
    init_tja(global_data.session_data[(int)global_data.player_num].selected_song);
    init_tja_practice(global_data.session_data[(int)global_data.player_num].selected_song);
    audio.play_sound("restart", VolumePreset::SOUND);
    song_started = false;
    paused       = false;
    menu_open    = false;
    // Reset the chart clock the same way on_screen_start does; with the
    // stale start_ms the opening notes were already past the sweep and
    // could never be hit (same clock as the base game's restart, #83).
    last_resync_ms = 0;
    start_ms = get_current_ms() - parser->metadata.offset * 1000
             - (double)global_data.config->general.audio_offset;
    ms_from_start = get_current_ms() - start_ms;
}

void PracticeGameScreen::build_menu_text() {
    menu_text.clear();
    bool auto_on = practice_player && practice_player->is_auto_play();

    // Follows the language the config picked, the same way the rest of the
    // interface does: Japanese, Chinese (either script setting) or English.
    const std::string& lang = global_data.config->general.language;
    int col = lang == "ja" ? 0 : (lang.rfind("zh", 0) == 0 ? 1 : 2);

    // Visual order left to right, as the arcade lays its bars out; the
    // rightmost one, closing the menu, is where the cursor starts.
    struct Row { const char* ja; const char* zh; const char* en; };
    static const Row ROWS[] = {
        { "\u30b2\u30fc\u30e0\u7d42\u4e86",
          "\u7d50\u675f\u904a\u6232",
          "End Game" },
        { "\u307b\u304b\u306e\u66f2\u3092\u6f14\u594f",
          "\u9078\u5176\u4ed6\u6b4c\u66f2",
          "Another Song" },
        { "\u6700\u521d\u304b\u3089\u6f14\u594f",
          "\u5f9e\u982d\u6f14\u594f",
          "From the Top" },
        { "\u30aa\u30fc\u30c8\u6f14\u594f\u8a2d\u5b9a",
          "\u81ea\u52d5\u6f14\u594f\u8a2d\u5b9a",
          "Auto Play" },
        { "\u30b8\u30e3\u30f3\u30d7\u30dd\u30a4\u30f3\u30c8\u3078",
          "\u8df3\u81f3\u8df3\u8f49\u9ede",
          "To Jump Point" },
        { "\u30b8\u30e3\u30f3\u30d7\u30dd\u30a4\u30f3\u30c8\u8a2d\u5b9a",
          "\u8a2d\u5b9a\u8df3\u8f49\u9ede",
          "Set Jump Point" },
        { "\u30e1\u30cb\u30e5\u30fc\u3092\u3068\u3058\u308b",
          "\u95dc\u9589\u9078\u55ae",
          "Close Menu" },
    };

    for (const Row& r : ROWS) {
        std::string label = col == 0 ? r.ja : col == 1 ? r.zh : r.en;
        menu_text.push_back(std::make_unique<OutlinedText>(label,
            (int)(tex.skin_config[SC::SONG_BOX_NAME].font_size),
            ray::WHITE, ray::BLACK, true));
    }
    (void)auto_on;
}

std::optional<Screens> PracticeGameScreen::menu_action(int index) {
    switch (index) {
        case 0:   // leave practice altogether
            if (song_music.has_value()) audio.stop_sound(song_music.value());
            return on_screen_end(Screens::ENTRY);
        case 1:   // pick another song
            if (song_music.has_value()) audio.stop_sound(song_music.value());
            return on_screen_end(Screens::PRACTICE_SELECT);
        case 2:   // restart from the top
            restart_practice();
            return std::nullopt;
        case 3:   // toggle auto
            if (practice_player)
                practice_player->set_auto_play(!practice_player->is_auto_play());
            return std::nullopt;
        case 4:   // move the scrobble cursor to the jump point
            if (jump_bar >= 0 && jump_bar < (int)bars.size()) {
                scrobble_index = jump_bar;
                scrobble_time  = bars[scrobble_index].hit_ms;
                scrobble_move  = std::make_unique<MoveAnimation>(200.0, 0);
                menu_open = false;
            }
            return std::nullopt;
        case 5:   // register the current bar as the jump point
            jump_bar = scrobble_index;
            return std::nullopt;
        case 6:   // close the menu, stay paused
            menu_open = false;
            return std::nullopt;
    }
    return std::nullopt;
}

void PracticeGameScreen::draw_practice_menu() const {
    int n = (int)menu_text.size();
    if (n == 0) return;

    float scale   = tex.screen_scale;
    float bar_w   = 86 * scale;
    float bar_h   = 380 * scale;
    float gap     = 42 * scale;
    float pad     = 48 * scale;
    float panel_w = n * bar_w + (n - 1) * gap + pad * 2;
    float panel_h = bar_h + pad * 2;
    float panel_x = (tex.screen_width - panel_w) / 2.0f;
    float panel_y = 48 * scale;

    // The arcade's pale blue panel with its white frame.
    ray::Rectangle panel = { panel_x, panel_y, panel_w, panel_h };
    ray::DrawRectangleRounded(panel, 0.06f, 8, ray::Color(186, 192, 232, 245));
    ray::DrawRectangleRoundedLinesEx(panel, 0.06f, 8, 4 * scale, ray::WHITE);

    for (int i = 0; i < n; i++) {
        float x = panel_x + pad + i * (bar_w + gap);
        float y = panel_y + pad;
        bool selected = (i == menu_index);

        // Wooden bars, the chosen one gold - the same language as the wheel.
        ray::Color face   = selected ? ray::Color(255, 214,  74, 255)
                                     : ray::Color(166, 106,  44, 255);
        ray::Color border = selected ? ray::WHITE
                                     : ray::Color( 74,  46,  16, 255);
        ray::DrawRectangle((int)x, (int)y, (int)bar_w, (int)bar_h, face);
        ray::DrawRectangleLinesEx({x, y, bar_w, bar_h}, 4 * scale, border);

        // A darker inner edge gives the bar its bevel.
        ray::DrawRectangleLinesEx({x + 4 * scale, y + 4 * scale,
                                   bar_w - 8 * scale, bar_h - 8 * scale},
                                  2 * scale, ray::Color(0, 0, 0, 40));

        OutlinedText* text = menu_text[i].get();
        float tx = x + (bar_w - text->width) / 2.0f;
        float ty = y + std::max(10.0f * scale, (bar_h - text->height) / 2.0f);
        text->draw({.x = tx, .y = ty});
    }
}

std::optional<Screens> PracticeGameScreen::global_keys_practice() {
    if (ray::IsKeyPressed(global_data.config->keys.restart_key)) {
        restart_practice();
        return std::nullopt;
    }

    if (check_key_pressed(global_data.config->keys.back_key)) {
        if (song_music.has_value()) {
            audio.stop_sound(song_music.value());
        }
        return on_screen_end(Screens::PRACTICE_SELECT);
    }

    PlayerNum other_player = (global_data.player_num == PlayerNum::P1) ? PlayerNum::P2 : PlayerNum::P1;
    int other_idx  = (int)other_player - 1;
    int player_idx = (int)global_data.player_num - 1;

    if (!paused) {
        bool other_don   = is_l_don_pressed(other_player) || is_r_don_pressed(other_player);
        bool other_l_kat = is_l_kat_pressed(other_player);
        bool other_r_kat = is_r_kat_pressed(other_player);
        bool other_kat   = other_l_kat || other_r_kat;
        if (other_don || other_kat) {
            pause_song_practice();
            if (other_don) pause_don_anim->start();
            if (other_kat) pause_kat_anim->start();
            practice_player->spawn_scrobble_effect(DrumType::DON, Side::LEFT, other_idx);
        }
    } else {
        if (menu_open) {
            // The menu swallows the drums: kat steps, don confirms.
            bool step_l = is_l_kat_pressed(global_data.player_num) || is_l_kat_pressed(other_player);
            bool step_r = is_r_kat_pressed(global_data.player_num) || is_r_kat_pressed(other_player);
            if (step_l || step_r) {
                audio.play_sound("kat", VolumePreset::SOUND);
                int n = (int)menu_text.size();
                menu_index = ((menu_index + (step_r ? 1 : -1)) % n + n) % n;
            }
            if (is_l_don_pressed(global_data.player_num) || is_r_don_pressed(global_data.player_num) ||
                is_l_don_pressed(other_player) || is_r_don_pressed(other_player)) {
                audio.play_sound("don", VolumePreset::SOUND);
                auto next = menu_action(menu_index);
                if (next.has_value()) return next;
            }
            return std::nullopt;
        }

        if (is_l_don_pressed(other_player) || is_r_don_pressed(other_player)) {
            audio.play_sound("don", VolumePreset::SOUND);
            menu_don_anim->start();
            menu_open  = true;
            build_menu_text();
            menu_index = (int)menu_text.size() - 1;
            return std::nullopt;
        }

        if (is_l_don_pressed(global_data.player_num) || is_r_don_pressed(global_data.player_num)) {
            pause_song_practice();
            resume_don_anim->start();
            practice_player->spawn_scrobble_effect(DrumType::DON, Side::LEFT, player_idx);
        }

        bool speed_down = is_l_kat_pressed(other_player);
        bool speed_up   = is_r_kat_pressed(other_player);
        if (paused && (speed_down || speed_up)) {
            audio.play_sound("kat", VolumePreset::SOUND);
            if (speed_down) { song_speed = std::max(1, song_speed - 1); speed_l_kat_anim->start(); }
            if (speed_up)   { song_speed++;                              speed_r_kat_anim->start(); }
            if (song_music.has_value())
                audio.set_sound_pitch(song_music.value(), song_speed / 10.0f);
        }

        bool scrobble_left  = is_l_kat_pressed(global_data.player_num);
        bool scrobble_right = is_r_kat_pressed(global_data.player_num);

        if (!bars.empty() && (scrobble_left || scrobble_right)) {
            audio.play_sound("kat", VolumePreset::SOUND);
            if (scrobble_left)  skip_l_kat_anim->start();
            if (scrobble_right) skip_r_kat_anim->start();

            if (practice_player) {
                if (scrobble_left)  practice_player->spawn_scrobble_effect(DrumType::KAT, Side::LEFT,  player_idx);
                if (scrobble_right) practice_player->spawn_scrobble_effect(DrumType::KAT, Side::RIGHT, player_idx);
            }

            // Snap any in-progress animation
            if (scrobble_move->is_started && !scrobble_move->is_finished) {
                scrobble_time = bars[scrobble_index].hit_ms;
            }

            int old_index = scrobble_index;
            if (scrobble_left) {
                scrobble_index = (scrobble_index > 0) ? scrobble_index - 1 : (int)bars.size() - 1;
            } else {
                scrobble_index = (scrobble_index + 1) % (int)bars.size();
            }

            double time_difference = bars[scrobble_index].hit_ms - bars[old_index].hit_ms;
            scrobble_move = std::make_unique<MoveAnimation>(400.0, (int)time_difference, false, false, 0, 0.0,
                                                            std::nullopt, std::nullopt, std::string("quadratic"));
            scrobble_move->start();
        }
    }

    return std::nullopt;
}

std::optional<Screens> PracticeGameScreen::update() {
    Screen::update();

    double current_ms = get_frame_ms();
    transition->update(current_ms);
    if (!paused) {
        if (song_started && song_music.has_value() && audio.is_sound_playing(song_music.value())) {
            double audio_ms = audio.get_sound_time_played(song_music.value()) * 1000.0;
            ms_from_start = audio_ms + parser->metadata.offset * 1000.0
                          + start_delay - global_data.config->general.audio_offset;
            start_ms = current_ms - ms_from_start;
        } else {
            ms_from_start = current_ms - start_ms;
        }
    }
    poll_pending_song();
    if (transition->is_finished()) {
        start_song(current_ms);
        global_data.input_locked = 0;
    }

    resync_song(current_ms);

    update_background(current_ms);

    // Process practice input before player update so events aren't consumed by handle_input
    auto next_screen = global_keys_practice();
    if (next_screen.has_value()) return next_screen;

    for (auto& player : players)
        player->update(ms_from_start, current_ms, background);
    song_info.update(current_ms);

    scrobble_move->update(current_ms);
    if (scrobble_move->is_started && scrobble_move->is_finished) {
        scrobble_time = bars[scrobble_index].hit_ms;
        scrobble_move = std::make_unique<MoveAnimation>(200.0, 0);
    }

    if (pause_don_anim)   pause_don_anim->update(current_ms);
    if (pause_kat_anim)   pause_kat_anim->update(current_ms);
    if (resume_don_anim)  resume_don_anim->update(current_ms);
    if (skip_l_kat_anim)  skip_l_kat_anim->update(current_ms);
    if (skip_r_kat_anim)  skip_r_kat_anim->update(current_ms);
    if (menu_don_anim)    menu_don_anim->update(current_ms);
    if (speed_l_kat_anim) speed_l_kat_anim->update(current_ms);
    if (speed_r_kat_anim) speed_r_kat_anim->update(current_ms);

    return std::nullopt;
}

float PracticeGameScreen::get_scrobble_position_x(const Note& note, double current_ms) const {
    float speedx = note.bpm / 240000.0f * note.scroll_x * (tex.screen_width - JudgePos::X);
    float offset_px = 0.0f;
    if (!bars.empty() && scrobble_index < (int)bars.size()) {
        float bar_speedx = bars[scrobble_index].bpm / 240000.0f * bars[scrobble_index].scroll_x * (tex.screen_width - JudgePos::X);
        offset_px = (float)(scrobble_move->attribute * bar_speedx);
    }
    return JudgePos::X + (float)((note.hit_ms - current_ms) * speedx) - offset_px;
}

void PracticeGameScreen::draw_bar_scrobble(const Note& bar, double current_ms) const {
    if (!bar.display) return;
    float x = get_scrobble_position_x(bar, current_ms);
    float y_off = (float)((bar.hit_ms - current_ms) * (bar.bpm / 240000.0f * bar.scroll_y * ((tex.screen_width - JudgePos::X) / tex.screen_width) * tex.screen_width));
    float angle = (y_off != 0) ? std::atan2(bar.scroll_y, bar.scroll_x) * 180.0f / PI : 0.0f;
    tex.draw_texture(NOTES::_0, {.frame = bar.is_branch_start,
                                  .x = x + tex.skin_config[SC::MOJI_DRUMROLL].x - tex.textures[NOTES::_9]->width / 2.0f,
                                  .y = y_off + tex.skin_config[SC::MOJI_DRUMROLL].y + (184 * tex.screen_scale),
                                  .rotation = angle});
}

void PracticeGameScreen::draw_drumroll_scrobble(const Note& head, double current_ms) const {
    float start_pos = get_scrobble_position_x(head, current_ms);
    // Find corresponding TAIL
    const Note* tail = nullptr;
    for (const auto& n : scrobble_note_list) {
        if (n.type == NoteType::TAIL && n.index > head.index) { tail = &n; break; }
    }
    if (!tail) return;
    float end_pos = get_scrobble_position_x(*tail, current_ms);
    float length = end_pos - start_pos;
    bool is_big = (head.type == NoteType::ROLL_HEAD_L);
    int color_val = head.color.value_or(255);
    ray::Color color = {255, (unsigned char)color_val, (unsigned char)color_val, 255};
    float y_pos = tex.skin_config[SC::NOTES].y + (184 * tex.screen_scale);
    float moji_y = tex.skin_config[SC::MOJI].y + (184 * tex.screen_scale);
    if (head.display) {
        tex.draw_texture(NOTES::_8, {.color = color, .frame = is_big, .x = start_pos, .y = y_pos, .x2 = length + tex.skin_config[SC::DRUMROLL_WIDTH_OFFSET].width});
        if (is_big) tex.draw_texture(NOTES::DRUMROLL_BIG_TAIL, {.color = color, .x = end_pos, .y = y_pos});
        else        tex.draw_texture(NOTES::DRUMROLL_TAIL,     {.color = color, .x = end_pos, .y = y_pos});
        TexID head_tex = tex_id_map.count("notes/" + std::to_string((int)head.type))
                         ? tex.get_enum("notes/" + std::to_string((int)head.type)) : TexID(0);
        tex.draw_texture(head_tex, {.color = color, .x = start_pos - tex.textures[NOTES::_9]->width / 2.0f, .y = y_pos});
    }
    tex.draw_texture(NOTES::MOJI_DRUMROLL_MID, {.x = start_pos, .y = moji_y, .x2 = length});
    tex.draw_texture(NOTES::MOJI, {.frame = head.moji, .x = start_pos - tex.textures[NOTES::MOJI]->width / 2.0f, .y = moji_y});
    tex.draw_texture(NOTES::MOJI, {.frame = tail->moji, .x = end_pos  - tex.textures[NOTES::MOJI]->width / 2.0f, .y = moji_y});
}

void PracticeGameScreen::draw_balloon_scrobble(const Note& head, double current_ms) const {
    float offset = tex.skin_config[SC::BALLOON_OFFSET].x;
    float start_pos = get_scrobble_position_x(head, current_ms);
    const Note* tail = nullptr;
    for (const auto& n : scrobble_note_list) {
        if (n.type == NoteType::TAIL && n.index > head.index) { tail = &n; break; }
    }
    if (!tail) return;
    float end_pos   = get_scrobble_position_x(*tail, current_ms);
    float pause_pos = JudgePos::X;
    float y_pos = tex.skin_config[SC::NOTES].y + (184 * tex.screen_scale);
    float position;
    if (current_ms >= tail->hit_ms)   position = end_pos;
    else if (current_ms >= head.hit_ms) position = pause_pos;
    else                                position = start_pos;
    if (head.display) {
        TexID head_tex = tex_id_map.count("notes/" + std::to_string((int)head.type))
                         ? tex.get_enum("notes/" + std::to_string((int)head.type)) : TexID(0);
        tex.draw_texture(head_tex, {.x = position - offset - tex.textures[NOTES::_9]->width / 2.0f, .y = y_pos});
        tex.draw_texture(NOTES::_10, {.x = position - offset + tex.textures[NOTES::_10]->width - tex.textures[NOTES::_9]->width / 2.0f, .y = y_pos});
    }
    float moji_y = tex.skin_config[SC::MOJI].y + (184 * tex.screen_scale);
    tex.draw_texture(NOTES::MOJI, {.frame = head.moji, .x = position - tex.textures[NOTES::MOJI]->width / 2.0f, .y = moji_y});
}

void PracticeGameScreen::draw_notes_scrobble(double current_ms) const {
    int scissor_x = players[0]->get_scissor_x();
    int win_w = ray::GetScreenWidth();
    ray::BeginScissorMode(scissor_x, 0, win_w - scissor_x, ray::GetScreenHeight());

    // Draw bars
    for (auto it = bars.rbegin(); it != bars.rend(); ++it) {
        draw_bar_scrobble(*it, current_ms);
    }
    // Draw notes in reverse order
    float y = 184 * tex.screen_scale;
    for (auto it = scrobble_note_list.rbegin(); it != scrobble_note_list.rend(); ++it) {
        const Note& note = *it;
        if (note.type == NoteType::TAIL || note.type == NoteType::BARLINE) continue;

        if (note.color.has_value()) {
            draw_drumroll_scrobble(note, current_ms);
        } else if (note.type == NoteType::BALLOON_HEAD || note.type == NoteType::KUSUDAMA) {
            draw_balloon_scrobble(note, current_ms);
        } else {
            if (note.display) {
                float x = get_scrobble_position_x(note, current_ms);
                TexID note_tex = tex_id_map.count("notes/" + std::to_string((int)note.type))
                                 ? tex.get_enum("notes/" + std::to_string((int)note.type)) : TexID(0);
                tex.draw_texture(note_tex, {.center = true, .x = x - tex.textures[NOTES::_9]->width / 2.0f, .y = tex.skin_config[SC::NOTES].y + y});
            }
            float moji_x = get_scrobble_position_x(note, current_ms) - tex.textures[NOTES::MOJI]->width / 2.0f;
            tex.draw_texture(NOTES::MOJI, {.frame = note.moji, .x = moji_x, .y = tex.skin_config[SC::MOJI].y + y});
        }
    }

    ray::EndScissorMode();

    if (!bars.empty() && scrobble_index < (int)bars.size()) {
        std::string bar_str = std::to_string(scrobble_index + 1);
        float dw = tex.textures[PRACTICE::BAR_COUNT_BAR]->x2[0];
        float x  = get_scrobble_position_x(bars[scrobble_index], current_ms) + 4 * tex.screen_scale;
        float y  = tex.skin_config[SC::NOTES].y + 184 * tex.screen_scale
                   + tex.textures[PRACTICE::BAR_COUNT_BAR]->y2[0] + 2 * tex.screen_scale;
        for (int i = 0; i < (int)bar_str.size(); i++)
            tex.draw_texture(PRACTICE::BAR_COUNT_BAR, {.frame = bar_str[i] - '0', .x = x + i * dw, .y = y});
    }
}

void PracticeGameScreen::draw() {
    ray::ClearBackground(ray::BLACK);
    if (movie.has_value()) {
        movie->draw();
    } else if (background.has_value()) {
        background->draw_back();
    }

    players[0]->draw_practice(ms_from_start, 0, 184 * tex.screen_scale, mask_shader, !paused);

    if (background.has_value()) background->draw_fore();

    // Scrobble note overlay when paused
    if (paused) {
        draw_notes_scrobble(scrobble_time);
    }

    // Player overlays after practice graphics (hit effects, combos, etc.)
    if (players.size() == 1) {
        players[0]->draw_overlays(184 * tex.screen_scale, mask_shader);
    }

    tex.draw_texture(PRACTICE::LARGE_DRUM, {.index = 0});
    tex.draw_texture(PRACTICE::LARGE_DRUM, {.index = 1});

    int other_idx = (global_data.player_num == PlayerNum::P1) ? 1 : 0;
    int player_idx = (global_data.player_num == PlayerNum::P1) ? 0 : 1;
    if (!paused) {
        tex.draw_texture(PRACTICE::PAUSE_DON, {.scale = pause_don_anim  ? (float)pause_don_anim->attribute  : 1.0f, .center = true, .index = other_idx});
        tex.draw_texture(PRACTICE::PAUSE_KAT, {.scale = pause_kat_anim  ? (float)pause_kat_anim->attribute  : 1.0f, .center = true, .index = other_idx * 2});
        tex.draw_texture(PRACTICE::PAUSE_KAT, {.scale = pause_kat_anim  ? (float)pause_kat_anim->attribute  : 1.0f, .center = true, .index = other_idx * 2 + 1});
    } else {
        tex.draw_texture(PRACTICE::RESUME_DON,  {.scale = resume_don_anim  ? (float)resume_don_anim->attribute  : 1.0f, .center = true, .index = player_idx});
        tex.draw_texture(PRACTICE::SKIP_L_KAT,  {.scale = skip_l_kat_anim  ? (float)skip_l_kat_anim->attribute  : 1.0f, .center = true, .index = player_idx * 2});
        tex.draw_texture(PRACTICE::SKIP_R_KAT,  {.scale = skip_r_kat_anim  ? (float)skip_r_kat_anim->attribute  : 1.0f, .center = true, .index = player_idx * 2 + 1});
        tex.draw_texture(PRACTICE::MENU_DON,    {.scale = menu_don_anim    ? (float)menu_don_anim->attribute    : 1.0f, .center = true, .index = other_idx});
        // The skin's speed_l_kat sprite reads 速 (faster) and speed_r_kat reads
        // 遅 (slower), but left kat lowers the tempo and right kat raises it
        // (matching measure skip: left = back, right = forward). Draw the
        // sprites swapped so the icons match what the keys actually do (#89).
        tex.draw_texture(PRACTICE::SPEED_R_KAT, {.scale = speed_l_kat_anim ? (float)speed_l_kat_anim->attribute : 1.0f, .center = true, .index = other_idx * 2});
        tex.draw_texture(PRACTICE::SPEED_L_KAT, {.scale = speed_r_kat_anim ? (float)speed_r_kat_anim->attribute : 1.0f, .center = true, .index = other_idx * 2 + 1});
    }

    if (!paused) {
        tex.draw_texture(PRACTICE::PLAYING, {.fade = 0.5, .index = (int)global_data.player_num - 1});
    }

    // Progress bar
    tex.draw_texture(PRACTICE::PROGRESS_BAR_BG, {});
    float progress;
    if (paused && !bars.empty()) {
        double raw = scrobble_time + scrobble_move->attribute - bars[0].hit_ms;
        progress = (players.empty() || players[0]->end_time <= 0)
                   ? 0.0f
                   : (float)std::min(raw / players[0]->end_time, 1.0);
    } else {
        progress = players.empty() ? 0.0f : (float)std::min(ms_from_start / players[0]->end_time, 1.0);
    }
    float bar_width = tex.skin_config[SC::PRACTICE_PROGRESS_BAR_WIDTH].width;
    tex.draw_texture(PRACTICE::PROGRESS_BAR, {.x2 = progress * bar_width});

    if (!bars.empty()) {
        double first_bar_time = bars[0].hit_ms;
        double total_time = players.empty() ? 1.0 : players[0]->end_time;
        for (double marker : markers) {
            float mx = (float)((marker - first_bar_time) / total_time) * bar_width;
            tex.draw_texture(PRACTICE::GOGO_MARKER, {.x = mx});
        }
    }

    if (!bars.empty()) {
        int current_bar;
        if (paused) {
            current_bar = scrobble_index + 1;
        } else {
            current_bar = 0;
            double first_bar_time = bars[0].hit_ms;
            for (int i = 0; i < (int)bars.size(); i++) {
                if (bars[i].hit_ms - first_bar_time <= ms_from_start) current_bar = i + 1;
                else break;
            }
        }
        int total_bars = (int)bars.size();

        float dw   = tex.textures[PRACTICE::BAR_COUNT]->x2[0];
        float divw = tex.textures[PRACTICE::BAR_DIVIDER]->x2[0];
        float dh   = tex.textures[PRACTICE::BAR_COUNT]->y2[0];
        float pb_y = tex.textures[PRACTICE::PROGRESS_BAR_BG]->y[0];
        float pb_h = tex.textures[PRACTICE::PROGRESS_BAR_BG]->y2[0];
        float digit_y = pb_y + (pb_h - dh) * 0.5f;

        std::string cur_str = std::to_string(current_bar);
        std::string tot_str = std::to_string(total_bars);
        int d1 = song_speed / 10;
        int d2 = song_speed % 10;

        tex.draw_texture(PRACTICE::SONG_TEMPO, {});
        tex.draw_texture(PRACTICE::DOT, {});
        tex.draw_texture(PRACTICE::MULTIPLIER, {});
        tex.draw_texture(PRACTICE::BAR_LABEL, {});

        float st_right  = tex.textures[PRACTICE::SONG_TEMPO]->x[0] + tex.textures[PRACTICE::SONG_TEMPO]->x2[0];
        float dot_right = tex.textures[PRACTICE::DOT]->x[0] + tex.textures[PRACTICE::DOT]->x2[0];
        float lbl_right = tex.textures[PRACTICE::BAR_LABEL]->x[0] + tex.textures[PRACTICE::BAR_LABEL]->x2[0];

        tex.draw_texture(PRACTICE::BAR_COUNT, {.frame = d1, .x = st_right - 4 * tex.screen_scale, .y = digit_y});
        tex.draw_texture(PRACTICE::BAR_COUNT, {.frame = d2, .x = dot_right - 4 * tex.screen_scale, .y = digit_y});

        for (int i = 0; i < (int)cur_str.size(); i++)
            tex.draw_texture(PRACTICE::BAR_COUNT, {.frame = cur_str[i] - '0', .x = lbl_right + i * dw, .y = digit_y});
        float div_x = lbl_right + (float)cur_str.size() * dw;
        tex.draw_texture(PRACTICE::BAR_DIVIDER, {.x = div_x, .y = digit_y});
        for (int i = 0; i < (int)tot_str.size(); i++)
            tex.draw_texture(PRACTICE::BAR_COUNT, {.frame = tot_str[i] - '0', .x = div_x + divw + i * dw, .y = digit_y});
    }

    if (paused) {
        tex.draw_texture(PRACTICE::PAUSED, {.fade = 0.5});
        if (menu_open) draw_practice_menu();
    }

    draw_overlay();
}
