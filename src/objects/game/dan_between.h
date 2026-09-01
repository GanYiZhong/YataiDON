#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "../../libs/audio.h"
#include "../../libs/global_data.h"
#include "../../libs/sample_table.h"
#include "../../libs/screen.h"
#include "../../libs/text.h"
#include "../../libs/texture.h"

// ROUND 69 (r69-dan-song-transition) -- 段位道場's BETWEEN-SONG presentation.
//
// User request:「換到下一首歌的時候，應該有一個聲音特效以及lane圖像特效，以及歌名
// 的展出」-- a sound effect, a lane graphic effect and the next song's name when a
// course advances.  All three are ONE cabinet object:
//
//   asset  D:\lumen3906\lumen\000_default\enso_dani\enso\dani_enso\dani_enso_between.nulm
//   code   App::DojoEnsoGraphicFusuma  (CHN05 39.06, DojoEnsoGraphicFusuma.obj.c)
//
// The movie is a 1920x264 LANE STRIP (not a full-screen clip), root character 9
// placing `main` (character 8, 374 frames, 60 fps) at stage (1209, 109), with
// labels init=0 start=5 stay=123 open=204 end=369.  Its whole display list is
// four nodes, and the per-frame dump (`lumen_anim_dump --sprite 8 --all --leaves
// --range 0,373`) is exported to `Scripts/anim/dani_between.lua`:
//
//   next_song_name  DEPTH 0 -- the BOTTOM node.  A sprite carrying the two
//         runtime-bound text fields `text_kanban_title_song` (size 54) and
//         `text_song_title_sub` (size 30), both align=2 (centred) on x = 1209;
//         alpha is 0 until f123 (`stay`), 1 through f339, then fades to 0 over
//         f340..f368.  Being at depth 0 it draws UNDER the doors: it is switched
//         on behind the SHUT doors and is only revealed when they slide open.
//   #4@1  DEPTH 1, **ClipDepth = 3** -- a CLIP MASK, not a painted shape.
//         ROUND 75 read the raw PlaceObject at file offset 4496:
//             +8 char_id=4  +24 mode=1  +26 blend=0  +28 depth=1  +30 clip=3
//         Per specs/lumen_render.md 1.2 a non-sentinel ClipDepth is rasterised
//         into the STENCIL ONLY and never to the colour buffer; it bounds the
//         depth range (1, 3], i.e. exactly the two doors below.  Its geometry
//         (1422x195 at 498,12 -- the skin's own lane_background x / lane band y)
//         is therefore the DOORS' CLIP RECT, not a lane blackout.
//         ROUND 69 read it as "an opaque black quad" and painted it: that is
//         where 「白色推門動畫效果會錯誤的把 play lane 變成黑色」 came from.
//         The reading was an artefact of the tool -- lumen_shape_dump's -m
//         composite path (src/lumen_shape_dump.c:249-357) has NO clip_depth
//         check at all and paints masks as ordinary shapes, unlike
//         lumen_compose.c:156 draw_stage which skips them.  The shape itself
//         really is black (RGBA 0,0,0,255) and its placement really is an
//         identity transform (cmul [256,256,256,256], cadd [0,0,0,0]) -- the
//         colour was never the question; whether it is PAINTED was.
//   #6@2  the fusuma door, a 720x208 shape whose canvas top-left sits at
//         (-360, -104) from `main`; tx runs 1071 -> 360 over f5..f67 (close),
//         holds, then 360 -> 1071 over f204..f219 (open).  WHITE paper
//         (measured 245,245,245 / 231,231,228) with the orange Don face
//         (216,69,6) -- the "white effect" and "white doors" of the report.
//   #6@3  the SAME shape at -tx with m = [-1,0,0,1], i.e. a pure horizontal
//         mirror -- so one texture, drawn twice.  Depth 3 is ABOVE depth 2.
//
// TIMING -- App::DojoEnsoGraphicFusuma::Process (0x1401083E0) drives the clip by
// LABEL, not by free playback (the clip stops itself: `lumen_anim_dump
// --auto-stop` never leaves frame 0):
//
//   enso state == 5 (song over), counter > 240  -> goto_label("start") + play
//                                                  + Sound::KeyOn("fusuma_close")
//   close fired, ++counter >= 360               -> goto_label("open")  + play
//                                                  + Sound::KeyOn("fusuma_open")
//                                                  + EnsoData+95 = 1  <-- the
//                                                    "next song may run" flag,
//                                                    set on the SAME line as the
//                                                    open, which is why
//                                                    SONG_OPEN_MS below is the
//                                                    moment start_song() is let go.
//
// Those counters are on the same tick that advances the clip (the same Process
// calls MovieClipInterface::play on it), i.e. 60 Hz clip frames -- NOT the Lua
// `Common.FPS` 120 clock (ENGINE_BINDINGS.md ROUND 24 scopes that correction to
// cabinet .lua timers).  Corroboration: DojoEnsoGameManager::Process banks the
// finished song and loads the next one at its own `>= 300`, and 240 + (67-5) =
// 302 is exactly the frame the doors finish closing -- the next song is loaded
// behind shut doors, which only lines up at 1 tick = 1 clip frame.
//
// Fail-soft by contract (the ANIM_PIPELINE rule): a missing table or a skin
// without the two `lane/dan_between_*` textures simply draws nothing and the
// course still advances -- `song_may_start()` stays honest either way.
//
// HEADER-ONLY ON PURPOSE. CMakeLists.txt:52 globs src/*.cpp WITHOUT
// CONFIGURE_DEPENDS, so a new .cpp would silently not be compiled until someone
// re-ran cmake -- and this repo's build discipline is `bash
// scratchpad/build_once.sh` (a plain `cmake --build`, no reconfigure) precisely
// because several agents share one build tree. Adding no translation unit keeps
// this round buildable by that one command. The two texture.json entries this
// object needs went into the EXISTING Graphics/game/lane/texture.json for the
// same reason: that file is already in the codegen DEPENDS list, so editing it
// re-runs gen_textures.py while adding a new subset would not.
class DanBetween {
public:
    // The clip's own frame numbers (its label table, verified against the dump).
    static constexpr double FPS     = 60.0;
    static constexpr double F_START = 5.0;
    static constexpr double F_STAY  = 123.0;
    static constexpr double F_OPEN  = 204.0;
    static constexpr double F_END   = 369.0;
    // DojoEnsoGraphicFusuma::Process: 360 ticks from the close to the open.
    static constexpr double CLOSE_TO_OPEN_FRAMES = 360.0;

    // ms from start() to the fusuma opening == the cabinet's EnsoData+95, i.e.
    // the moment the next song is allowed to run.  6000 ms.
    static constexpr double SONG_OPEN_MS = CLOSE_TO_OPEN_FRAMES * 1000.0 / FPS;
    // ms from start() to the last clip frame; after this the lane is clear.
    static constexpr double TOTAL_MS = SONG_OPEN_MS + (F_END - F_OPEN) * 1000.0 / FPS;

    // Arms the clip at `now` and fills `next_song_name` from the SAME strings
    // DanGameScreen::change_song() already hands SongInfo (no parallel title
    // object: this takes the finished strings, it does not re-derive them).
    void start(double now, const std::string& title, const std::string& subtitle,
               bool show_subtitle);
    // Hard stop -- used by the course-ending path so a skip cannot leave the
    // doors half-slid under the result ribbon.
    void stop();

    void update(double now);
    // `lane_y` is the SAME lane offset DanGameScreen::draw() passes Player::draw
    // (184 * screen_scale), so the strip always sits on the lane it covers.
    void draw(float lane_y);

    bool is_active() const { return active; }
    // True when nothing is holding the next song back: either the clip is not
    // running at all, or its `open` label has been reached.
    bool song_may_start() const { return !active || elapsed >= SONG_OPEN_MS; }

private:
    bool   active     = false;
    bool   open_fired = false;
    double start_ms   = 0.0;
    double elapsed    = 0.0;
    const SampleTable* table = nullptr;
    std::unique_ptr<OutlinedText> title_text;
    std::unique_ptr<OutlinedText> subtitle_text;

    // The clip frame for the current `elapsed`, honouring the two stops.
    double clip_frame() const;
    double text_alpha() const;
    double door_tx() const;
};

// ---------------------------------------------------------------------------
// inline implementation
// ---------------------------------------------------------------------------

namespace dan_between_detail {

// ROUND 19's rule: a skin `outline` value is in OutlinedText's radius units and
// is NOT screen-scaled here (OutlinedText's own ctor does that).
inline float skin_outline(const SkinInfo& s) { return s.outline >= 0 ? s.outline : 3.0f; }

// Fail-soft door track for when Scripts/anim/dani_between.lua is missing: the
// measured endpoints, linear. The shipped table carries the cabinet's own eased
// curve; this only keeps the doors moving if the table ever goes away.
inline double fallback_tx(double f) {
    if (f <= DanBetween::F_START) return 1071.0;
    if (f < 67.0)  return 1071.0 + (360.0 - 1071.0) * (f - 5.0) / 62.0;
    if (f <= DanBetween::F_OPEN) return 360.0;
    if (f < 219.0) return 360.0 + (1071.0 - 360.0) * (f - 204.0) / 15.0;
    return 1071.0;
}

}  // namespace dan_between_detail

inline void DanBetween::start(double now, const std::string& title,
                              const std::string& subtitle, bool show_subtitle) {
    active     = true;
    open_fired = false;
    start_ms   = now;
    elapsed    = 0.0;
    if (!table) table = get_sample_table("dani_between");

    const SkinInfo* t = tex.skin_entry("dan_between_title");
    const SkinInfo* s = tex.skin_entry("dan_between_sub");
    title_text.reset();
    subtitle_text.reset();
    if (t && !title.empty()) {
        title_text = std::make_unique<OutlinedText>(
            title, t->font_size > 0 ? t->font_size : 54, ray::WHITE, ray::BLACK,
            false, dan_between_detail::skin_outline(*t));
    }
    if (s && show_subtitle && !subtitle.empty()) {
        subtitle_text = std::make_unique<OutlinedText>(
            subtitle, s->font_size > 0 ? s->font_size : 30, ray::WHITE, ray::BLACK,
            false, dan_between_detail::skin_outline(*s));
    }

    // `start` label -> Sound::KeyOn("fusuma_close") on the cabinet
    // (DojoEnsoGraphicFusuma::Process:440-444). Named exactly as the arcade cue
    // so a YATAIDON_AUDIO_TRACE line reads the same on both sides.
    audio.play_sound("fusuma_close", VolumePreset::SOUND);
}

inline void DanBetween::stop() {
    active     = false;
    open_fired = false;
    elapsed    = 0.0;
    title_text.reset();
    subtitle_text.reset();
}

inline void DanBetween::update(double now) {
    if (!active) return;
    elapsed = now - start_ms;
    if (!open_fired && elapsed >= SONG_OPEN_MS) {
        open_fired = true;
        // `open` label -> Sound::KeyOn("fusuma_open") (Process:511-515).
        audio.play_sound("fusuma_open", VolumePreset::SOUND);
    }
    if (elapsed >= TOTAL_MS) stop();
}

inline double DanBetween::clip_frame() const {
    if (elapsed < SONG_OPEN_MS) {
        // goto_label("start") + play, and the clip stops itself at `stay`.
        return std::min(F_STAY, F_START + elapsed * FPS / 1000.0);
    }
    // goto_label("open") + play, through to `end`.
    return std::min(F_END, F_OPEN + (elapsed - SONG_OPEN_MS) * FPS / 1000.0);
}

inline double DanBetween::door_tx() const {
    const double f = clip_frame();
    if (table) {
        if (const SampleTrack* trk = table->track("fusuma_r")) {
            const int col = table->field("tx");
            if (col >= 0) return table->sample(*trk, col, f);
        }
    }
    return dan_between_detail::fallback_tx(f);
}

inline double DanBetween::text_alpha() const {
    const double f = clip_frame();
    if (table) {
        if (const SampleTrack* trk = table->track("next_song_name")) {
            const int col = table->field("a");
            if (col >= 0) return std::clamp(table->sample(*trk, col, f), 0.0, 1.0);
        }
    }
    // Fallback: hard on at `stay`, out over the clip's last 29 frames.
    if (f < F_STAY) return 0.0;
    if (f < 340.0)  return 1.0;
    return std::clamp((368.0 - f) / 28.0, 0.0, 1.0);
}

inline void DanBetween::draw(float lane_y) {
    if (!active) return;
    const double f = clip_frame();
    // f < `start` and f >= `end` are the two frames whose display list is empty.
    if (f < F_START || f >= F_END) return;
    if (!tex.has_texture("lane/dan_between_fusuma")) return;

    const uint32_t door_id = (uint32_t)tex.get_enum("lane/dan_between_fusuma");
    auto dit = tex.textures.find(door_id);
    if (dit == tex.textures.end()) return;
    const TextureObject& door = *dit->second;

    // ---- DEPTH 0: next_song_name --------------------------------------------
    // The clip puts the two text fields at the BOTTOM of the display list, under
    // the doors. Drawing them first is what produces the ordering the cabinet
    // has and the user described: white doors slide shut, the name switches on
    // at `stay` BEHIND them, and the name is REVEALED as the doors slide open.
    // (ROUND 69 drew them last, on top of the doors -- which only looked right
    // because the mask was being painted as a black slab underneath.)
    const double a = text_alpha();
    if (a > 0.0) {
        if (title_text) {
            const SkinInfo& c = *tex.skin_entry("dan_between_title");
            title_text->draw({.x = c.x - title_text->width / 2.0f,
                              .y = c.y - title_text->height / 2.0f + lane_y,
                              .fade = a});
        }
        if (subtitle_text) {
            const SkinInfo& c = *tex.skin_entry("dan_between_sub");
            subtitle_text->draw({.x = c.x - subtitle_text->width / 2.0f,
                                 .y = c.y - subtitle_text->height / 2.0f + lane_y,
                                 .fade = a});
        }
    }

    // ---- DEPTH 1: the ClipDepth=3 MASK --------------------------------------
    // Never painted. Its rect is the clip region for depths (1,3] = both doors.
    // `lane/dan_between_fill` is kept as the carrier of that rect (its own 8x8
    // black art is no longer drawn); the numbers fall back to the measured
    // cabinet values if the skin does not ship it.
    float mask_x = 498.0f, mask_y = 12.0f, mask_h = 195.0f;
    if (tex.has_texture("lane/dan_between_fill")) {
        auto mit = tex.textures.find((uint32_t)tex.get_enum("lane/dan_between_fill"));
        if (mit != tex.textures.end()) {
            mask_x = (float)mit->second->x[0];
            mask_y = (float)mit->second->y[0];
            mask_h = (float)mit->second->y2[0];
        }
    }

    // Horizontal half of the mask as a scissor: the doors travel out to x -214
    // at both extremes and must not paint over the drum / judge chrome (this
    // engine draws that chrome BEFORE the lane, where the cabinet draws it
    // after -- same visible result, no GAME_DAN draw reorder).
    const int scissor_x = virtual_to_screen_x(mask_x);
    const int win_w = ray::GetScreenWidth();
    ray::BeginScissorMode(scissor_x, 0, win_w - scissor_x, ray::GetScreenHeight());

    // Vertical half of the mask as a SOURCE crop, so it is resolution
    // independent: the 208 px door art is placed at the strip's y=5 and the mask
    // band is y 12..207, so 7 rows off the top and 6 off the bottom.
    const float door_y = (float)door.y[0];
    const float door_h = (float)door.y2[0];
    const float crop_top = std::max(0.0f, mask_y - door_y);
    const float crop_h =
        std::max(0.0f, std::min(door_h - crop_top, (mask_y + mask_h) - (door_y + crop_top)));

    // texture.json places are multiplied by the skin scale on load, but a source
    // rect is in RAW texture pixels, so undo that ratio for the crop.
    const float sc = (door.height > 0 && door_h > 0) ? door_h / (float)door.height : 1.0f;
    const float src_top = crop_top / sc;
    const float src_h   = crop_h / sc;

    const float tx = (float)door_tx();
    // depth 2 = the right leaf (tx), depth 3 = its horizontal mirror (-tx) and
    // sits ABOVE it; they meet at x 1209 and never overlap, so the order is
    // cosmetic -- kept faithful anyway.
    tex.draw_texture(door_id, {.x = tx, .y = lane_y + crop_top,
                               .y2 = crop_h - door_h,
                               .src = ray::Rectangle{0, src_top,
                                                     (float)door.width, src_h}});
    tex.draw_texture(door_id, {.x = -tx, .y = lane_y + crop_top,
                               .y2 = crop_h - door_h,
                               .src = ray::Rectangle{0, src_top,
                                                     -(float)door.width, src_h}});

    ray::EndScissorMode();
}
