#include "player.h"
#include "song_select_script.h"
#include "../../libs/audio.h"
#include "../../libs/input.h"
#include "../../libs/scores.h"

void SongSelectPlayer::try_lua_selector(bool is_half, float fade_in, int pass) {
    selector_handled_by_lua = script && script->draw_selector(this, is_half, fade_in, pass);
}

SongSelectPlayer::SongSelectPlayer(PlayerNum player_num)
    : player_num(player_num)
{
    int player_id = (player_num == global_data.first_login_player) ? global_data.config->general.player_1_id : global_data.config->general.player_2_id;
    if (auto p = scores_manager.get_player_data(player_id))
        player_data = *p;

    nameplate = Nameplate(player_data.username, player_data.title, player_num, player_data.dan, player_data.gold, player_data.rainbow, player_data.title_bg);

    selected_difficulty = Difficulty::BACK;
    prev_diff = Difficulty::BACK;
    selected_song = false;
    is_ready = false;
    is_ura = false;
    voice_played = false;
    ura_toggle = 0;
    diff_select_move_right = false;
    last_moved = 0;

    chara = make_chara_from_player_data(&player_data, player_num == PlayerNum::P2);
    if (player_data.player_id > 0) {
        chara->set_don_colors(player_data.chara_color_1, player_data.chara_color_2, player_data.chara_color_3);
        chara->apply_face(player_data.chara_face_index);
    } else {
        chara->set_don_colors(chara_default_color_1(player_id), chara_default_color_2(player_id), {249, 240, 225, 255});
    }
    chara->set_anim(AnimIndex::DON_SELECT_LOOP);

    diff_selector_move_1          = (MoveAnimation*)tex.get_animation(26, true);
    diff_selector_move_2          = (MoveAnimation*)tex.get_animation(27, true);
    selected_diff_bounce          = (MoveAnimation*)tex.get_animation(33, true);
    selected_diff_fadein          = (FadeAnimation*)tex.get_animation(34, true);
    selected_diff_highlight_fade  = (FadeAnimation*)tex.get_animation(35, true);
    selected_diff_text_resize     = (TextureResizeAnimation*)tex.get_animation(36, true);
    selected_diff_text_fadein     = (FadeAnimation*)tex.get_animation(37, true);
}

void SongSelectPlayer::update(double current_time) {
    selected_diff_bounce->update(current_time);
    selected_diff_fadein->update(current_time);
    selected_diff_highlight_fade->update(current_time);
    selected_diff_text_resize->update(current_time);
    selected_diff_text_fadein->update(current_time);
    diff_selector_move_1->update(current_time);
    diff_selector_move_2->update(current_time);
    nameplate.update(current_time);
    chara->update(current_time);

    if (neiro_selector.has_value()) {
        neiro_selector->update(current_time);
        if (neiro_selector->is_finished) {
            neiro_selector.reset();
            scores_manager.save_player_data(player_data);
            chara->set_anim(AnimIndex::DON_SELECT_PANELDOWN);
        }
    }

    if (modifier_selector.has_value()) {
        modifier_selector->update(current_time);
        if (modifier_selector->is_finished) {
            modifier_selector.reset();
            scores_manager.save_player_data(player_data);
            chara->set_anim(AnimIndex::DON_SELECT_PANELDOWN);
        }
    }
    if (ura_switch.has_value()) ura_switch->update(current_time);
    if (voice_played && !is_voice_playing()) {
        is_ready = true;
    }
}

bool SongSelectPlayer::is_voice_playing() {
    return audio.is_sound_playing("voice_start_song_" + std::to_string((int)player_num) + "p");
}

void SongSelectPlayer::init_diff_cursor() {
    int last = global_data.last_difficulty[(int)player_num];
    if (last < (int)Difficulty::EASY) return;

    // An ura pick lands the cursor on the oni column; the player flips to
    // ura themselves if this song has one.
    Difficulty desired = (Difficulty)std::min(last, (int)Difficulty::ONI);

    Difficulty pick = Difficulty::BACK;
    for (Difficulty d : curr_diffs) {
        if (d < Difficulty::EASY || d > Difficulty::ONI) continue;
        if (d <= desired && (pick == Difficulty::BACK || d > pick)) pick = d;
    }
    if (pick == Difficulty::BACK) {
        // nothing at or below the remembered difficulty - take the lowest
        for (Difficulty d : curr_diffs) {
            if (d < Difficulty::EASY || d > Difficulty::ONI) continue;
            if (pick == Difficulty::BACK || d < pick) pick = d;
        }
    }
    if (pick != Difficulty::BACK) {
        selected_difficulty = pick;
        // draw_selector positions from prev_diff until the move animation
        // has run; leaving it at BACK draws the cursor over the option
        // column instead of the picked difficulty.
        prev_diff = pick;
    }
}

void SongSelectPlayer::reset_selection() {
    is_ready = false;
    selected_song = false;
    voice_played = false;
}

void SongSelectPlayer::start_background_diffs() {
    selected_diff_text_resize->start();
    selected_diff_text_fadein->start();
    selected_diff_highlight_fade->start();
}

void SongSelectPlayer::sync_ura(bool ura) {
    // A player who already confirmed keeps what the column showed when
    // they pressed; only someone still choosing follows the toggle.
    if (voice_played || is_ready) return;
    if (is_ura == ura) return;
    is_ura = ura;
    ura_toggle = 0;
    if (selected_difficulty == Difficulty::ONI || selected_difficulty == Difficulty::URA)
        selected_difficulty = Difficulty(7 - (int)selected_difficulty);
}

SongSelectState SongSelectPlayer::select_song() {
    audio.play_sound("don", VolumePreset::SOUND);
    BaseBox* item = navigator.get_current_item();
    if (navigator.is_directory(item) && item->genre_index == GenreIndex::DAN) {
        global_data.session_data[(int)player_num].selected_dan_folder = item->path;
        return SongSelectState::DAN_SELECTED;
    } else if (navigator.is_song(item)) {
        navigator.enter_diff_select();
        selected_song = true;
        SongBox* song_item = (SongBox*)item;
        curr_diffs = song_item->get_diffs();
        init_diff_cursor();
        selected_diff_bounce->start();
        selected_diff_fadein->start();
        return SongSelectState::SONG_SELECTED;
    } else if (navigator.is_directory(item)) {
        navigator.load_current_directory(item->path);
    }
    return SongSelectState::BROWSING;
}

SongSelectState SongSelectPlayer::handle_input_browsing(double current_ms) {

    bool l_kat = is_l_kat_pressed(player_num);
    bool r_kat = is_r_kat_pressed(player_num);
    bool l_don = is_l_don_pressed(player_num);
    bool r_don = is_r_don_pressed(player_num);
    float wheel = ray::GetMouseWheelMove();

    if (ray::IsKeyPressed(ray::KEY_F5)) {
        navigator.load_current_directory(navigator.current_path);
    }

    bool navigated = false;
    if (ray::IsKeyPressed(ray::KEY_LEFT_CONTROL) || (l_kat && current_ms <= last_moved + 50)) {
        audio.play_sound("skip", VolumePreset::SOUND);
        navigator.skip_left();
        last_moved = current_ms;
        navigated = true;
    } else if (l_kat || wheel > 0) {
        audio.play_sound("kat", VolumePreset::SOUND);
        navigator.move_left();
        last_moved = current_ms;
        navigated = true;
    }

    if (ray::IsKeyPressed(ray::KEY_RIGHT_CONTROL) || (r_kat && current_ms <= last_moved + 50)) {
        audio.play_sound("skip", VolumePreset::SOUND);
        navigator.skip_right();
        last_moved = current_ms;
    } else if (r_kat || wheel < 0) {
        audio.play_sound("kat", VolumePreset::SOUND);
        navigator.move_right();
        last_moved = current_ms;
        navigated = true;
    }

    if (ray::IsKeyPressed(ray::KEY_SPACE)) {
        BaseBox* item = navigator.get_current_item();
        if (navigator.is_song(item)) {
            navigator.toggle_favorite(static_cast<SongBox*>(item));
            audio.play_sound("add_favorite", VolumePreset::SOUND);
        }
    }

    // A don landing on the same frame as a navigation would open the newly
    // selected item while the boxes are still mid-slide, corrupting the
    // wheel display - navigation wins, the select is dropped for this frame.
    if (!navigated && (l_don || r_don)) {
        BaseBox* item = navigator.get_current_item();
        if (navigator.is_directory(item) && item->collection == COLLECTIONS[5])
            return SongSelectState::SEARCHING;
        return select_song();
    }
    return SongSelectState::BROWSING;
}

std::optional<std::pair<int,int>> SongSelectPlayer::handle_input_diff_sort(DiffSortSelect* diff_sort_selector) {
    // ROUND 15: the arcade window plays its own SEs (katsu_c on a value change,
    // don_c on a decide, beep_c when the star row has no songs) and swallows input
    // during its 1 s start wait and its 2 s end hold, so the feedback has to come
    // from inside it - a don played here would fire on a press the window ignored.
    const bool arcade = diff_sort_selector->is_arcade();
    if (is_l_kat_pressed(player_num)) {
        diff_sort_selector->input_left();
        if (!arcade) audio.play_sound("kat", VolumePreset::SOUND);
    }
    if (is_r_kat_pressed(player_num)) {
        diff_sort_selector->input_right();
        if (!arcade) audio.play_sound("kat", VolumePreset::SOUND);
    }
    if (is_l_don_pressed(player_num) || is_r_don_pressed(player_num)) {
        if (!arcade) audio.play_sound("don", VolumePreset::SOUND);
        return diff_sort_selector->input_select();
    }
    return std::nullopt;
}

std::optional<std::string> SongSelectPlayer::handle_input_search() {
    if (ray::IsKeyPressed(ray::KEY_BACKSPACE)) {
        if (!search_string.empty())
            search_string.pop_back();
    } else if (ray::IsKeyPressed(ray::KEY_ENTER)
#ifdef PLATFORM_ANDROID
               || is_l_don_pressed(player_num) || is_r_don_pressed(player_num)
#endif
    ) {
        std::string result = search_string;
        search_string = "";
        clear_input_buffers();
        return result;
    }

    int key = ray::GetCharPressed();
    while (key > 0) {
        if (key == '\n' || key == '\r') {
            std::string result = search_string;
            search_string = "";
            clear_input_buffers();
            return result;
        }
        search_string += (char)key;
        key = ray::GetCharPressed();
    }
    return std::nullopt;
}

// Opt-in skin feature (skin_config "option_neiro_row"): the 音色 row lives inside
// the option panel, so the separate NEIRO slot is skipped by the cursor and its
// panel is never opened.
static bool neiro_in_options() { return tex.skin_flag("option_neiro_row"); }

SongSelectState SongSelectPlayer::handle_input_selecting() {
    bool l_kat = is_l_kat_pressed(player_num);
    bool r_kat = is_r_kat_pressed(player_num);
    bool l_don = is_l_don_pressed(player_num);
    bool r_don = is_r_don_pressed(player_num);

    if (voice_played) return SongSelectState::SONG_SELECTED;

    if (l_kat) {
        if (modifier_selector.has_value()) {
            audio.play_sound("kat", VolumePreset::SOUND);
            modifier_selector->left();
        } else if (neiro_selector.has_value()) {
            neiro_selector->left();
        } else {
            audio.play_sound("kat", VolumePreset::SOUND);
            navigate_difficulty_left();
            if (selected_difficulty >= Difficulty::EASY) {
                selected_diff_bounce->start();
                selected_diff_fadein->start();
            }
        }
    } else if (r_kat) {
        if (modifier_selector.has_value()) {
            audio.play_sound("kat", VolumePreset::SOUND);
            modifier_selector->right();
        } else if (neiro_selector.has_value()) {
            neiro_selector->right();
        } else {
            audio.play_sound("kat", VolumePreset::SOUND);
            navigate_difficulty_right();
            Difficulty prev_difficulty = selected_difficulty;
            if (selected_difficulty >= Difficulty::EASY) {
                selected_diff_bounce->start();
                if (prev_difficulty != selected_difficulty)
                    selected_diff_fadein->start();
            }
        }
    } else if (l_don || r_don) {
        audio.play_sound("don", VolumePreset::SOUND);
        if (modifier_selector.has_value()) {
            modifier_selector->confirm();
        } else if (neiro_selector.has_value()) {
            neiro_selector->confirm();
        } else {
            if (selected_difficulty == Difficulty::MODIFIER) {
                modifier_selector = ModifierSelector(player_num, &player_data);
                chara->set_anim(AnimIndex::DON_SELECT_PANELUP);
            } else if (selected_difficulty == Difficulty::NEIRO && !neiro_in_options()) {
                neiro_selector = NeiroSelector(player_num, &player_data);
                chara->set_anim(AnimIndex::DON_SELECT_PANELUP);
            } else if (selected_difficulty >= Difficulty::EASY) {
                voice_played = true;
                start_background_diffs();
                audio.play_sound("voice_start_song_" + std::to_string((int)player_num) + "p", VolumePreset::VOICE);
            } else if (selected_difficulty == Difficulty::BACK) {
                is_ready = true;
            }
        }
    }
    return SongSelectState::SONG_SELECTED;
}

void SongSelectPlayer::navigate_difficulty_left() {
    diff_select_move_right = false;

    if (is_ura && selected_difficulty == Difficulty::URA) {
        diff_selector_move_1->start();
        prev_diff = selected_difficulty;
        selected_difficulty = (curr_diffs.size() == 1)
            ? (neiro_in_options() ? Difficulty::MODIFIER : Difficulty::NEIRO)
            : curr_diffs[curr_diffs.size() - 3];
    } else if (selected_difficulty == Difficulty::NEIRO || selected_difficulty == Difficulty::MODIFIER) {
        diff_selector_move_2->start();
        prev_diff = selected_difficulty;
        selected_difficulty = Difficulty((int)selected_difficulty - 1);
    } else if (selected_difficulty == Difficulty::BACK) {
        // no-op
    } else if (std::find(curr_diffs.begin(), curr_diffs.end(), selected_difficulty) == curr_diffs.end()) {
        prev_diff = selected_difficulty;
        diff_selector_move_1->start();
        selected_difficulty = curr_diffs.front();
    } else if (selected_difficulty == curr_diffs.front()) {
        diff_selector_move_2->start();
        prev_diff = selected_difficulty;
        selected_difficulty = neiro_in_options() ? Difficulty::MODIFIER : Difficulty::NEIRO;
    } else {
        diff_selector_move_1->start();
        prev_diff = selected_difficulty;
        auto it = std::find(curr_diffs.begin(), curr_diffs.end(), selected_difficulty);
        selected_difficulty = *std::prev(it);
    }
}

void SongSelectPlayer::navigate_difficulty_right() {
    diff_select_move_right = true;

    if (is_ura && selected_difficulty == Difficulty::HARD) {
        prev_diff = selected_difficulty;
        selected_difficulty = Difficulty::URA;
        diff_selector_move_1->start();
    }

    bool has_ura = std::find(curr_diffs.begin(), curr_diffs.end(), Difficulty::URA) != curr_diffs.end();
    bool has_oni = std::find(curr_diffs.begin(), curr_diffs.end(), Difficulty::ONI) != curr_diffs.end();

    if ((selected_difficulty == Difficulty::ONI || selected_difficulty == Difficulty::URA) && has_ura && has_oni) {
        ura_toggle = (ura_toggle + 1) % 10;
        if (ura_toggle == 0) toggle_ura_mode();
    } else if (selected_difficulty == Difficulty::NEIRO
               || (selected_difficulty == Difficulty::MODIFIER && neiro_in_options())) {
        prev_diff = selected_difficulty;
        selected_difficulty = curr_diffs.front();
        diff_selector_move_2->start();
        diff_selector_move_1->start();
    } else if (selected_difficulty == Difficulty::MODIFIER || selected_difficulty == Difficulty::BACK) {
        prev_diff = selected_difficulty;
        selected_difficulty = Difficulty((int)selected_difficulty + 1);
        diff_selector_move_2->start();
    } else if (selected_difficulty < curr_diffs.back()) {
        prev_diff = selected_difficulty;
        auto it = std::find(curr_diffs.begin(), curr_diffs.end(), selected_difficulty);
        selected_difficulty = *std::next(it);
        diff_selector_move_1->start();
    }
}

void SongSelectPlayer::toggle_ura_mode() {
    ura_toggle = 0;
    is_ura = !is_ura;
    audio.play_sound("ura_switch", VolumePreset::SOUND);
    selected_difficulty = Difficulty(7 - (int)selected_difficulty);
    ura_switch.emplace();
    ura_switch->start(is_ura);
    SongBox* song = dynamic_cast<SongBox*>(navigator.get_current_item());
    if (song) song->is_ura = is_ura;
}

void SongSelectPlayer::draw_selector(bool is_half, float fade_in) {
    // Keep the cursor hidden until the difficulty panel finished expanding:
    // parts of the selector (the outline) draw unfaded, so a pre-placed
    // cursor popped in at full opacity over the still-fading panel.
    if (fade_in < 1.0f) return;
    float fade = (neiro_selector.has_value() || modifier_selector.has_value())
        ? 0.5f : fade_in;
    float direction = diff_select_move_right ? 1.0f : -1.0f;
    float offset = tex.skin_config[SC::SELECTOR_OFFSET].x;
    float balloon_offset_1 = tex.skin_config[SC::SELECTOR_BALLOON_OFFSET_1].x;
    float balloon_offset_2 = tex.skin_config[SC::SELECTOR_BALLOON_OFFSET_2].x;

    std::string p = std::to_string((int)player_num) + "p";
    std::string half_suffix = is_half ? "_half" : "";

    if (selected_difficulty <= Difficulty::NEIRO || prev_diff == Difficulty::NEIRO) {
        if (prev_diff == Difficulty::NEIRO && selected_difficulty >= Difficulty::EASY) {
            if (!diff_selector_move_2->is_finished) {
                float bx = (((int)prev_diff + 3) * balloon_offset_2) + balloon_offset_1 + (diff_selector_move_2->attribute * direction);
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_balloon" + half_suffix)),      {.x=bx, .fade=fade});
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline_back" + half_suffix)), {.x=(((int)prev_diff + 3) * balloon_offset_2) + ((float)diff_selector_move_2->attribute * direction)});
            } else {
                Difficulty difficulty = std::min(Difficulty::ONI, selected_difficulty);
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_balloon" + half_suffix)), {.x=((int)difficulty * offset), .fade=fade});
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline" + half_suffix)), {.x=((int)difficulty * offset)});
            }
        } else if (!diff_selector_move_2->is_finished) {
            if (selected_difficulty != Difficulty::BACK) {
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline_back" + half_suffix)), {.x=(((int)prev_diff + 3) * balloon_offset_2) + ((float)diff_selector_move_2->attribute * direction)});
                float bx = (((int)prev_diff + 3) * balloon_offset_2) + balloon_offset_1 + (diff_selector_move_2->attribute * direction);
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_balloon" + half_suffix)), {.x=bx, .fade=fade});
            } else {
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline_back" + half_suffix)), {.x=(((int)prev_diff + 3) * balloon_offset_2) + ((float)diff_selector_move_2->attribute * direction), .fade=fade});
            }
        } else {
            if (selected_difficulty != Difficulty::BACK) {
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline_back" + half_suffix)), {.x=(((int)selected_difficulty + 3) * balloon_offset_2)});
                float bx = (((int)selected_difficulty + 3) * balloon_offset_2) + balloon_offset_1;
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_balloon" + half_suffix)), {.x=bx, .fade=fade});
            } else {
                tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline_back" + half_suffix)), {.x=(((int)selected_difficulty + 3) * balloon_offset_2), .fade=fade});
            }
        }
    } else {
        if (prev_diff == Difficulty::NEIRO) return;
        if (!diff_selector_move_1->is_finished) {
            Difficulty difficulty = std::min(Difficulty::ONI, prev_diff);
            float bx = ((int)difficulty * offset) + (diff_selector_move_1->attribute * direction);
            tex.draw_texture(tex.get_enum("diff_select/" + (p + "_balloon" + half_suffix)), {.x=bx, .fade=fade});
            tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline" + half_suffix)), {.x=bx});
        } else {
            Difficulty difficulty = std::min(Difficulty::ONI, selected_difficulty);
            tex.draw_texture(tex.get_enum("diff_select/" + (p + "_balloon" + half_suffix)), {.x=((int)difficulty * offset), .fade=fade});
            tex.draw_texture(tex.get_enum("diff_select/" + (p + "_outline" + half_suffix)), {.x=((int)difficulty * offset)});
        }
    }
}

void SongSelectPlayer::draw_background_diffs(SongSelectState state) {
    if (!selected_song || state != SongSelectState::SONG_SELECTED || selected_difficulty < Difficulty::EASY)
        return;

    float x_offset = ((int)player_num == 2) ? tex.skin_config[SC::SONG_SELECT_BG_DIFF_P2_OFFSET].x : 0.0f;
    float bounce_y  = -selected_diff_bounce->attribute;
    float bounce_y2 =  selected_diff_bounce->attribute;
    // ROUND 34 (r34-songselectplayer-crash): curr_diffs can contain
    // Difficulty::TOWER(5) / Difficulty::DAN(6) for a TJA that embeds a
    // COURSE:Dan (or COURSE:Tower) chart alongside its normal courses -
    // navigate_difficulty_right() happily walks the cursor onto them since
    // they're just more entries in curr_diffs. diff_frame_oni already
    // clamped to ONI for the highlight/text art (which only ships frames
    // 0-3), but this unclamped diff_frame fed straight into
    // GLOBAL::BACKGROUND_DIFF, which only ships frames 0-4 (EASY..URA) -
    // frame 5/6 is out of range and TextureWrapper::draw_texture's
    // crop_data->at(params.frame) throws std::out_of_range. Clamp to URA,
    // the highest frame BACKGROUND_DIFF actually has, so a dan/tower pick
    // reuses the URA background instead of indexing past the array.
    int diff_frame     = (int)(std::min(Difficulty::URA, selected_difficulty));
    int diff_frame_oni = (int)(std::min(Difficulty::ONI, selected_difficulty));

    tex.draw_texture(GLOBAL::BACKGROUND_DIFF, {.frame=diff_frame, .x=x_offset, .y=bounce_y,  .y2=bounce_y2, .fade=std::min(0.5f, (float)selected_diff_fadein->attribute)});
    if (selected_diff_highlight_fade->is_reversing || selected_diff_highlight_fade->is_finished)
        tex.draw_texture(GLOBAL::BACKGROUND_DIFF, {.frame=diff_frame, .x=x_offset, .y=bounce_y, .y2=bounce_y2});
    tex.draw_texture(GLOBAL::BACKGROUND_DIFF_HIGHLIGHT,  {.frame=diff_frame_oni, .x=x_offset, .fade=selected_diff_highlight_fade->attribute});
    tex.draw_texture(GLOBAL::BG_DIFF_TEXT_BG, {.scale=(float)selected_diff_text_resize->attribute, .center=true, .x=x_offset, .fade=std::min(0.5f, (float)selected_diff_text_fadein->attribute)});
    tex.draw_texture(GLOBAL::BG_DIFF_TEXT,    {.frame=diff_frame_oni, .scale=(float)selected_diff_text_resize->attribute, .center=true, .x=x_offset, .fade=selected_diff_text_fadein->attribute});
}

void SongSelectPlayer::draw(SongSelectState state, bool is_half, float diff_fade_in) {
    // ROUND 15 (r15-audit-select): PASS 1 of the skin's selector - the part the
    // cabinet draws ABOVE the difficulty cards (player_cursor_N, depth 21/22 of
    // `common_song_select_main_diff` sprite 46). Pass 0 - the yellow course frame
    // / button glow, depth 7/10, which the cards sit on top of - already ran from
    // the scene before Navigator::draw(). Splitting it is what fixes both the 1P
    // bubble whose tail the card cut off and the 2P cursor that vanished entirely,
    // without turning the frame into an opaque block over the cards.
    // Both scenes go through this one call for pass 1, so 1P and 2P cannot drift.
    if (selected_song && state == SongSelectState::SONG_SELECTED) {
        try_lua_selector(is_half, diff_fade_in, 1);
        if (!selector_handled_by_lua) draw_selector(is_half, diff_fade_in);
    }
    selector_handled_by_lua = false;

    float offset = 0.0f;
    if (neiro_selector.has_value()) {
        offset = neiro_selector->move->attribute;
        offset = neiro_selector->is_confirmed
            ? offset + tex.skin_config[SC::SONG_SELECT_OFFSET].x
            : -offset;
    }
    if (modifier_selector.has_value()) {
        offset = modifier_selector->move->attribute;
        offset = modifier_selector->is_confirmed
            ? offset + tex.skin_config[SC::SONG_SELECT_OFFSET].x
            : -offset;
    }

    if (player_num == PlayerNum::P1) {
        nameplate.draw(tex.skin_config[SC::SONG_SELECT_NAMEPLATE_1P].x, tex.skin_config[SC::SONG_SELECT_NAMEPLATE_1P].y);
        chara->draw(tex.skin_config[SC::SONG_SELECT_CHARA_1P].x, tex.skin_config[SC::SONG_SELECT_CHARA_1P].y + (offset * 0.6f), 1.0f, "SONG_SELECT_P1");
    } else {
        nameplate.draw(tex.skin_config[SC::SONG_SELECT_NAMEPLATE_2P].x, tex.skin_config[SC::SONG_SELECT_NAMEPLATE_2P].y);
        chara->draw(tex.skin_config[SC::SONG_SELECT_CHARA_2P].x, tex.skin_config[SC::SONG_SELECT_CHARA_2P].y + (offset * 0.6f), 1.0f, "SONG_SELECT_P2");
    }

    // A skin that defines SongSelect:draw_option_panel repaints these boards
    // itself (see SongSelectScript::draw_option_panel); otherwise the C++ panels
    // draw exactly as before.
    if (neiro_selector.has_value()    && !(script && script->draw_option_panel(this, 2))) neiro_selector->draw();
    if (modifier_selector.has_value() && !(script && script->draw_option_panel(this, 1))) modifier_selector->draw();
    if (ura_switch.has_value()) ura_switch->draw();
}
