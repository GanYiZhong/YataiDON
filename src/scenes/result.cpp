#include "result.h"
#include "../libs/input.h"

void ResultScreen::on_screen_start() {
    Screen::on_screen_start();
    SessionData& session_data = global_data.session_data[(int)global_data.player_num];
    song_info = std::make_unique<OutlinedText>(session_data.song_title, tex.skin_config[SC::SONG_INFO_RESULT].font_size, ray::WHITE, ray::BLACK, false, 5);
    song_info_subtitle.reset();
    if (session_data.song_subtitle_full_display && !session_data.song_subtitle.empty()) {
        song_info_subtitle = std::make_unique<OutlinedText>(session_data.song_subtitle, tex.skin_config[SC::SONG_INFO_RESULT_SUBTITLE].font_size, ray::WHITE, ray::BLACK, false, 5);
    }
    audio.play_sound("bgm", VolumePreset::MUSIC);
    fade_out = (FadeAnimation*)tex.get_animation(0);
    fade_in.emplace(global_data.player_num);
    start_ms = get_current_ms();
    fs::path loading_graphic_path = global_data.session_data[(int)global_data.player_num].selected_song.parent_path() / "Loading.png";
    if (exists(loading_graphic_path)) {
        loading_graphic.emplace(ray::LoadTexture(loading_graphic_path.string().c_str()));
    } else {
        background.emplace(global_data.player_num, tex.screen_width);
    }
    player_1.emplace(global_data.player_num, false, false);
    song_num = std::make_unique<SongNum>(global_data.songs_played + 1);
}

Screens ResultScreen::on_screen_end(Screens next_screen) {
    if (loading_graphic.has_value()) {
        ray::UnloadTexture(loading_graphic.value());
        loading_graphic.reset();
    }
    reset_session();
    return Screen::on_screen_end(next_screen);
}

double ResultScreen::reveal_end_ms() {
    return player_1.has_value() ? player_1->reveal_end_ms() : 0.0;
}

// ROUND 19 -- the cabinet's press rule, 39.06 `script_lua/result/ResultMain.lua`.
//
// There are TWO different things a drum press does on the arcade result screen, and
// which one you get depends on whether the reveal is still running:
//
//   * WHILE it runs, `Main:Update()` (line 849) does
//         if this.isEnableSkip_ == true and (Input.GetTrigger(Input.AVAILABLE_OK) == true
//                                         or Input.GetTrigger(Input.AVAILABLE_CANCEL) == true) then
//             this.isEffectSkip_ = true
//     and every state from `UpdateTamashiiGage` to `Crown` tests `isEffectSkip_` in its
//     exit condition -- so one press collapses the whole reveal.
//
//   * ONCE it is over the machine sits in `WaitNextScene` (line 712) and a press is an
//     ADVANCE, not a skip:
//         if this.timer_ > 500 and (AVAILABLE_OK or AVAILABLE_CANCEL) then isNextScene = true
//         elseif this.timer_ > 3600 then isNextScene = true
//
// What we used to do was treat EVERY press as "collapse": the first press set
// `skipped_time` and any later one needed `skipped_time + 5000`.  A player who simply
// watched the reveal finish therefore had `skipped_time == 0` for ever, and their press
// silently armed the skip of a sequence that had already played -- the button was inert
// and the screen only left on the (non-existent) timeout.  That is the reported bug.
//
// `isEnableSkip_` is never cleared, so a press after the reveal ALSO sets `isEffectSkip_`
// on the cabinet -- but by then nothing reads it, so the observable rule is the two-way
// split above.
void ResultScreen::handle_input(double current_ms) {
    // Both must be evaluated: `check_key_pressed` consumes, and short-circuiting one of
    // them leaves that pad's presses queued up for a later frame (the old `&&`/`||`
    // precedence bug here did exactly that to the right don).
    bool l = is_l_don_pressed();
    bool r = is_r_don_pressed();
    if (!(l || r)) return;

    double reveal_end = reveal_end_ms();
    if (reveal_end == 0) {
        // Still revealing -> isEffectSkip_.
        if (skipped_time == 0) {
            skipped_time = current_ms;
            audio.play_sound("don", VolumePreset::SOUND);
        }
        return;
    }
    // Reveal over -> WaitNextScene.  The two 500-frame dwells are the cabinet's
    // (kWaitEffectEndTime_, then WaitNextScene's own `timer_ > 500`).
    if (current_ms >= reveal_end + kWaitEffectEndMs + kWaitNextSceneMs && !fade_out->is_started) {
        fade_out->start();
        audio.play_sound("don", VolumePreset::SOUND);
    }
}

void ResultScreen::update_input_and_timeout(double current_ms) {
    if (!fade_out || fade_out->is_started) return;

    if (skip_enabled_ms == 0 && fade_in.has_value() && fade_in->is_finished())
        skip_enabled_ms = current_ms + kEnableSkipMs;

    if (skip_enabled_ms > 0 && current_ms >= skip_enabled_ms) handle_input(current_ms);

    // WaitNextScene's `elseif this.timer_ > 3600`.  Until this round RESULT had no
    // timeout at all: with no press the screen sat there for ever.
    double reveal_end = reveal_end_ms();
    if (reveal_end > 0 && !fade_out->is_started
        && current_ms >= reveal_end + kWaitEffectEndMs + kAutoNextSceneMs) {
        fade_out->start();
    }
}

std::optional<Screens> ResultScreen::update() {
    Screen::update();
    double current_time = get_current_ms();
    allnet_indicator.update(current_time);
    if (fade_in.has_value()) fade_in->update(current_time);
    if (player_1.has_value()) player_1->update(current_time, fade_in.has_value() && fade_in->is_finished(), skipped_time > 0);

    update_input_and_timeout(current_time);

    if (fade_out) {
        fade_out->update(current_time);
        if (fade_out->is_finished) {
            fade_out->update(current_time);
            if (global_data.config->general.song_limit > 0 && global_data.config->general.song_limit == global_data.songs_played) {
                global_data.songs_played = 0;
                return on_screen_end(Screens::GAME_OVER);
            }
            global_data.returned_from_result = true;
            return on_screen_end(Screens::SONG_SELECT);
        }
    }
    return std::nullopt;
}

void ResultScreen::draw_overlay() {
    if (fade_in.has_value()) fade_in->draw();
    if (fade_out) ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::BLACK, fade_out->attribute));
    coin_overlay.draw();
    allnet_indicator.draw();
}

void ResultScreen::draw_song_info() {
    song_num->draw(tex.skin_config[SC::SONG_NUM_RESULT].x, tex.skin_config[SC::SONG_NUM_RESULT].y, 1.0);
    // Right-aligned at x by default; a skin that gives song_info_result a
    // width gets the title centred in [x, x+width] instead (arcade layout).
    const SkinInfo& si = tex.skin_config[SC::SONG_INFO_RESULT];
    float title_x = si.width > 0 ? si.x + (si.width - song_info->width) / 2 : si.x - song_info->width;
    song_info->draw({.x=title_x, .y=si.y - song_info->height / 2, .fade=1.0});
    // Upstream subtitle row, following the same centre-or-right-align rule.
    if (song_info_subtitle) {
        float sub_x = si.width > 0 ? si.x + (si.width - song_info_subtitle->width) / 2
                                   : si.x - song_info_subtitle->width;
        song_info_subtitle->draw({.x=sub_x, .y=tex.skin_config[SC::SONG_INFO_RESULT_SUBTITLE].y - song_info_subtitle->height / 2, .fade=1.0});
    }
}

void ResultScreen::draw() {
    if (background.has_value()) background->draw();
    if (loading_graphic.has_value()) {
        ray::Rectangle src = {0, 0, (float)loading_graphic.value().width, (float)loading_graphic.value().height};
        ray::Rectangle dst = {0, 0, (float)tex.screen_width, (float)tex.screen_height};
        ray::DrawTexturePro(loading_graphic.value(), src, dst, {0,0}, 0, ray::WHITE);
    }
    draw_song_info();
    if (player_1.has_value()) player_1->draw();
    draw_overlay();
}
