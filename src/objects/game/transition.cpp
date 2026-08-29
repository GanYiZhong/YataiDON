#include "transition.h"
#include "../../libs/global_data.h"
#include <algorithm>

Transition::Transition(const std::string& title, const std::string& subtitle, bool is_second) :
    is_second(is_second) {
    rainbow_up = (MoveAnimation*)global_tex.get_animation(0);
    mini_up = (MoveAnimation*)global_tex.get_animation(1);
    chara_down = (MoveAnimation*)global_tex.get_animation(2);
    song_info_fade = (FadeAnimation*)global_tex.get_animation(3);
    song_info_fade_out = (FadeAnimation*)global_tex.get_animation(4);

    this->title = std::make_unique<OutlinedText>(title, global_tex.skin_config[SC::TRANSITION_TITLE].font_size, ray::WHITE, ray::BLACK, false, 5);
    this->subtitle = std::make_unique<OutlinedText>(subtitle, global_tex.skin_config[SC::TRANSITION_SUBTITLE].font_size, ray::WHITE, ray::BLACK, false, 5);

    if (!load("SongTransition", "transition", title, subtitle, is_second)) return;
    fn_update    = lua_object["update"];
    fn_draw_bg   = lua_object["draw_bg"];
    fn_draw_info = lua_object["draw_info"];
}

Transition::~Transition() {
    if (loading_graphic.has_value()) {
        ray::UnloadTexture(loading_graphic.value());
    }
}

void Transition::add_loading_graphic(const std::string& path) {
    loading_graphic.emplace(ray::LoadTexture(path.c_str()));
    ray::GenTextureMipmaps(&loading_graphic.value());
    ray::SetTextureFilter(loading_graphic.value(), ray::TEXTURE_FILTER_TRILINEAR);
}

void Transition::set_dan(int color, const std::string& rank_name) {
    dan_color = color;
    // Loaded on demand: `Graphics/dan_loading/` is not a screen name, so nothing
    // loads it automatically and a player who never enters the dojo never pays
    // the ~15 MB. `load_folder` is idempotent.
    global_tex.load_folder("dan_loading", "loading_dan");
    if (!rank_name.empty()) {
        dan_rank_text = std::make_unique<OutlinedText>(
            rank_name, global_tex.skin_config[SC::DAN_TITLE].font_size,
            ray::WHITE, ray::BLACK, true);
    }
}

// The cabinet's own timeline, sampled at 1:1 (60 fps) from
// `lumen_anim_dump --sprite 68 --all --range 0,302`:
//
//   f36        the night scene appears, ty 544 (top-left y = 544 - 650 = -106)
//   f36..215   ty 544 -> 432, exactly -0.625 px per frame (-112 px over 179 f)
//   f36..64    the full-stage black quad fades 1 -> 0   (the reveal)
//   f45..64    `dani_rank` and `save_caution` fade 0 -> 1, alpha (f-44)/20
//   f64..242   steady state -- the frame the cabinet parks on while it loads
//
// We enter at the cabinet's f36 rather than f0: its frames 5..34 are the black
// quad fading UP over the *previous* screen, and our Transition is only ever
// drawn over the screen that has already come up, so replaying that half would
// be a black flash. Starting fully black and revealing the scene is the same
// picture from the player's side.
//
// The `total_offset` the curtain rides is applied to the whole rig so the exit
// is our engine's existing one (the curtain sliding up). NOT ported: the
// cabinet's own `out` half (f243..272, a black fade back in), for the same
// reason ROUND 15 records for `loading_kuro` -- the outgoing screen has already
// torn down by the time the loop learns of the change.
void Transition::draw_dan(float /*total_offset*/) {
    const double f = 36.0 + (get_current_ms() - dan_start_ms) * 0.06;
    const float  y = -106.0f - 0.625f * (float)std::clamp(f - 36.0, 0.0, 179.0);
    const float  a = (float)std::clamp((f - 44.0) / 20.0, 0.0, 1.0);
    const float  black = (float)std::clamp(1.0 - (f - 36.0) / 29.0, 0.0, 1.0);
    // NOT `-total_offset`. That offset is the SECOND-SONG shift the rainbow rig
    // applies to its curtain, and `DanGameScreen` constructs its Transition with
    // is_second = true for every dan song, so riding it parked the whole night
    // scene off the top of the screen (measured: only the black quad reached the
    // framebuffer, scratchpad/r17dn/after/gamedan/g00_00000.png). The rig's exit
    // instead rides the curtain's own upward slide, which is the motion this
    // engine already uses to end a loading screen.
    const float  dy = -(float)rainbow_up->attribute;

    global_tex.draw_texture(LOADING_DAN::NIGHT, {.y = y + dy});
    if (a > 0.0f) {
        global_tex.draw_texture(LOADING_DAN::PLAQUE,
                                {.frame = std::clamp(dan_color, 0, 6), .y = dy, .fade = a});
        if (dan_rank_text) {
            const SkinInfo* p = global_tex.skin_entry("dan_loading_rank");
            const float rx = p ? p->x : 1697.0f;
            const float ry = p ? p->y : 290.0f;
            dan_rank_text->draw({.x = rx - dan_rank_text->width / 2.0f,
                                 .y = ry - dan_rank_text->height / 2.0f + dy,
                                 .fade = a});
        }
    }
    // The reveal fade. MEASURED, so nobody re-litigates it: the first rendered
    // frame after start() comes out at mean 5.7-6.1 / 255 against the arcade
    // art's own 118.98, i.e. an effective black of 0.949-0.952 -- exactly what
    // `1 - (f-36)/29` predicts for the ~22-25 ms that frame is old. The plain
    // DrawRectangle composites correctly inside the screen's
    // BLEND_CUSTOM_SEPARATE scope; it does NOT need a BeginBlendMode wrapper.
    //
    // HARNESS TRAP that cost this round a build: the automation `wait` command
    // FREEZES the render loop, so every `shot` taken inside a GAME_DAN
    // transition returns that same first frame no matter how long you wait
    // (scratchpad/r17dn/after/gamedan{2,3,4,5,6}/g00 are all within 1/255 of each
    // other, at 0 ms and at +900 ms alike). That is a measurement artefact, not a
    // stuck fade -- do not "fix" the fade because of it. The reveal window
    // (~470 ms) is simply not sampleable with this harness.
    if (black > 0.0f)
        ray::DrawRectangle(0, 0, global_tex.screen_width, global_tex.screen_height,
                           ray::Fade(ray::BLACK, black));
}

void Transition::start() {
    dan_start_ms = get_current_ms();
    rainbow_up->start();
    mini_up->start();
    chara_down->start();
    song_info_fade->start();
    song_info_fade_out->start();
}

void Transition::update(double current_ms) {
    call(fn_update, "SongTransition:update", current_ms,
         (double)rainbow_up->attribute, (double)song_info_fade->attribute);
    rainbow_up->update(current_ms);
    chara_down->update(current_ms);
    mini_up->update(current_ms);
    song_info_fade->update(current_ms);
    song_info_fade_out->update(current_ms);
}

bool Transition::is_finished() {
    return song_info_fade->is_finished;
}

void Transition::draw_song_info() {
    float fade_1 = song_info_fade->attribute;
    float fade_2 = std::min(0.70, song_info_fade->attribute);
    float offset = 0;
    if (is_second) {
        fade_1 = song_info_fade_out->attribute;
        fade_2 = std::min(0.70, song_info_fade_out->attribute);
        offset = global_tex.skin_config[SC::TRANSITION_OFFSET].y - rainbow_up->attribute;
    }
    global_tex.draw_texture(RAINBOW_TRANSITION::TEXT_BG, {.y=(float)-rainbow_up->attribute - offset, .fade=fade_2});

    float x = (float)global_tex.screen_width/2 - title->width/2;
    float y = global_tex.skin_config[SC::TRANSITION_TITLE].y - title->height/2 - rainbow_up->attribute - offset;
    title->draw({.x = x, .y = y, .fade = fade_1});

    x = (float)global_tex.screen_width/2 - subtitle->width/2;
    y = global_tex.skin_config[SC::TRANSITION_SUBTITLE].y - subtitle->height/2 - rainbow_up->attribute - offset;
    subtitle->draw({.x = x, .y = y, .fade = fade_1});
}

void Transition::draw_default(float total_offset) {
    global_tex.draw_texture(RAINBOW_TRANSITION::RAINBOW_BG_BOTTOM, {.y=(float)-rainbow_up->attribute - total_offset});
    global_tex.draw_texture(RAINBOW_TRANSITION::RAINBOW_BG_TOP, {.y=(float)-rainbow_up->attribute - total_offset});
    global_tex.draw_texture(RAINBOW_TRANSITION::RAINBOW_BG, {.y=(float)-rainbow_up->attribute - total_offset});
    float offset = chara_down->attribute;
    float chara_offset = 0;
    if (is_second) {
        offset = chara_down->attribute - mini_up->attribute/3;
        chara_offset = global_tex.skin_config[SC::TRANSITION_CHARA_OFFSET].y;
    }
    global_tex.draw_texture(RAINBOW_TRANSITION::CHARA_LEFT, {.x=(float)-mini_up->attribute/2 - chara_offset, .y=(float)-mini_up->attribute + offset - total_offset});
    global_tex.draw_texture(RAINBOW_TRANSITION::CHARA_RIGHT, {.x=(float)mini_up->attribute/2 + chara_offset, .y=(float)-mini_up->attribute + offset - total_offset});
    global_tex.draw_texture(RAINBOW_TRANSITION::CHARA_CENTER, {.y=(float)-rainbow_up->attribute + offset - total_offset});
}

void Transition::draw() {
    float total_offset = 0;
    if (is_second) total_offset = global_tex.skin_config[SC::TRANSITION_OFFSET].y;
    // ROUND 15 (r15-audit-global), LUA_CAPABILITIES item 40c.
    // A scripted backdrop used to be skipped entirely when the song shipped its own
    // `Loading.png`, so that song lost the whole arcade rig (curtain, sparkles, don/katsu)
    // and got a bare scrolling image instead.
    // Evidence for what the cabinet does: it has no per-song loading art at all - `loading_song`
    // is one shared LumenPlayer preloaded by `SharedResourceManager` (index 11) and
    // `LoadingHelper::StartFadeIn` selects it purely by "is there a song id", never by song.
    // `Loading.png` is a simulator extension (0 songs in this library ship one).  So the
    // cabinet-faithful behaviour is: the rig ALWAYS draws; the per-song graphic is drawn ON TOP
    // of it, sharing the same curtain offset, instead of replacing it.
    // A dan run owns the whole backdrop: the cabinet has a different movie here,
    // not a re-dressed rainbow, so neither the rig nor the title band runs.
    if (dan_color >= 0) {
        draw_dan(total_offset);
        return;
    }

    const bool scripted_bg = fn_draw_bg.valid();
    if (scripted_bg) {
        call(fn_draw_bg, "SongTransition:draw_bg", total_offset,
             (double)rainbow_up->attribute, (double)mini_up->attribute,
             (double)chara_down->attribute);
        if (!loading_graphic.has_value()) {
            if (fn_draw_info.valid()) {
                call(fn_draw_info, "SongTransition:draw_info", total_offset,
                     (double)rainbow_up->attribute, (double)song_info_fade->attribute,
                     (double)song_info_fade_out->attribute);
            } else {
                draw_song_info();
            }
            return;
        }
    }
    if (loading_graphic.has_value()) {
        ray::Rectangle src = {0, 0, (float)loading_graphic.value().width, (float)loading_graphic.value().height};
        ray::Rectangle dst = {0, global_tex.screen_height + (global_tex.skin_config[SC::TRANSITION_OFFSET].y - global_tex.screen_height) - (float)rainbow_up->attribute - total_offset, (float)global_tex.screen_width, (float)global_tex.screen_height};
        ray::DrawTexturePro(loading_graphic.value(), src, dst, {0,0}, 0, ray::WHITE);
    } else {
        draw_default(total_offset);
    }

    if (fn_draw_info.valid()) {
        call(fn_draw_info, "SongTransition:draw_info", total_offset,
             (double)rainbow_up->attribute, (double)song_info_fade->attribute,
             (double)song_info_fade_out->attribute);
    } else {
        draw_song_info();
    }
}
