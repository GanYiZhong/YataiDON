#include "combo_announce.h"
#include "../../libs/texture.h"
#include "../../libs/audio.h"
#include "../../libs/perf.h"

// ---------------------------------------------------------------------------
// ROUND 91 -- the cabinet's own milestone-banner layout.
//
// Source: 39.06 `lumen/000_default/enso_normal/enso/information/
// don_fukidashi.nulm` ("Don's speech bubble" -- the makimono/scroll that pops
// on every 100 combo).  Stage 560x240, root char 59 -> main_1p (char 58, red)
// / main_2p (char 30, cyan) -> combo_num (char 53 / 25).
//
// The cabinet does NOT compose a number at run time: `combo_num` carries one
// pre-authored keyframe per milestone (labels combo10/30/50 then every 100 up
// to combo5000) and the game does gotoAndStop(label).  Reading those keyframes
// out with `lumen_anim_dump --sprite 53 --all --leaves` gives, in the scroll's
// own 560x240 frame (digit cell is 104x104, ink rows 7..96 inside it, cell
// centre y = 120 in every single case):
//
//   2 digits : centres 172, 236            pitch 64, sx 1.00, text_mc x 392
//   3 digits : centres 140, 204, 268       pitch 64, sx 1.00, text_mc x 392
//   4 digits : centres 134, 188, 242, 296  pitch 54, sx 0.85, text_mc x 398
//
// At four digits the cabinet condenses the group HORIZONTALLY ONLY -- sy stays
// 1.0, so it is a condense and not a uniform shrink -- and the 「コンボ！」
// suffix condenses and shifts with it, because the suffix lives INSIDE
// `combo_num` (as `text_mc/text_combo`) and therefore tracks the digit group,
// not the paper.  The paper itself never changes: same 560x240 art, same shape
// ids, for every digit count.
//
// 5+ digits is NOT AUTHORED BY THE CABINET (its largest label is combo5000).
// `layout()` below extrapolates -- that is a ROUND 91 DECISION, not a
// measurement: keep the 4-digit ink envelope by holding the rightmost cell at
// +92 and scaling pitch and sx by 4/n.
//
// The suffix itself is the movie's single DefineEditText (char 3):
// text "コンボ！", size 36, rgba 255,200,0 (gold), font MS UI Gothic.
//
// ***CORRECTED BY ROUND 101.***  ROUND 91 wrote here that the field has "NO
// visible outline ring -- so ROUND 78's 'border colour defaults to black' does
// not put a black outline here".  That is WRONG, and it is the ROUND 73 trap
// one round later.  The record carries `border = 5.25` (measured with
// `lumen_shape_dump <don_fukidashi> --check-text -v --language jpn`), and the
// citation ROUND 91 leaned on -- `lumen_parse.c:1087` -- is OUR DUMPER's
// parser, not the cabinet's renderer.  The dumper skips the ring whenever the
// record's `+50` flag is clear (it is clear in all 705 shipped records); that
// is precisely the blind spot ROUND 77 documented after ROUND 73 deleted a
// real outline on the strength of the same silence.
//
// The real renderer does NOT skip it.  `AppLumenRenderer::RenderText`
// @0x14037C580 branches on `+50` only to pick the ring's COLOUR SOURCE, and
// with `+50 == 0` it takes the default at `AppLumenRenderer+0x44E0`, which
// `AppLumenRenderer::set_font` @0x14037C180 sets to (0,0,0,1) BLACK whenever
// the fill's packed RGB is non-zero.  This fill is (255,200,0), so the ring is
// OPAQUE #000000 at radius 5.25, laid down as RenderBorderText's 16 copies at
// `border*(cos, sin)`.  (ROUND 77's extraction; re-derived and re-used here.)
//
// Re-rendering char 3 with `--set-border 3=000000` -- the switch that turns
// the dumper's own ring pass on -- gives 123x46 of ink around the 113x36 gold
// fill, i.e. a 5 px skirt on all four sides, and rendering the whole ancestor
// chain with and without `-c` is byte-identical (max|diff| = 0), so no
// PlaceObject colour transform recolours it -- ROUND 90's brown-`cadd` case
// does NOT apply to this element.  `Graphics/game/combo/announce_text.png` is
// that ringed cabinet render, 123x46.
//
// The digits are BAKED ART, not text: 20 x 104x104 shapes in the same .nulm,
// two colour sets, authored in the order 0 1 3 5 2 4 6 7 8 9.  Measured, their
// outline is pure (0,0,0) and their fill red is (255,36,0) -- identical to what
// this skin already shipped, so no colour changed this round.
// ---------------------------------------------------------------------------
namespace {

// All in the scroll's own 560x240 frame.  `announce_digit_*` texture.json
// carries x = 362 (the scroll's left edge in skin coords) and y = -196 (the
// cell top, 120 - 104/2, already offset by the scroll's own -264), so the
// params below are literally the cabinet's numbers.
constexpr float CELL       = 104.0f;
constexpr float GROUP_CX   = 204.0f;   // combo_num's origin inside the scroll
constexpr float PITCH_WIDE = 64.0f;
constexpr float PITCH_NARROW = 54.0f;
constexpr float SX_NARROW  = 0.85f;
constexpr float NARROW_X0  = -70.0f;   // leftmost cell, local to GROUP_CX
constexpr float TEXTMC_WIDE   = 392.0f;
constexpr float TEXTMC_NARROW = 398.0f;
// Where our shipped announce_text.png's ink sits relative to text_mc's origin,
// and how wide it is.  These describe the ART, measured off the cabinet's own
// render of text_combo: the GOLD FILL is 113 px wide with its left edge at
// x 313 while text_mc is at 392, i.e. fill_dx = -79.
//
// ROUND 101: the art is now 123x46 -- the same 113x36 gold fill with the
// cabinet's own 5.25 px black outline ring around it, so the fill sits 5 px in
// from the art's left edge.  TEXT_DX therefore describes the ART's left edge,
// -79 - 5 = -84, and TEXT_W is the ART's width, 123.  Both the offset and the
// width are scaled by `sx`, so the fill lands at `textmc_x + sx*(-84) + sx*5 =
// textmc_x + sx*(-79)` -- byte-for-byte the pre-ROUND-101 position at EVERY
// digit count, not just at sx = 1.  (`announce_text` texture.json y moved
// -132 -> -137 for the same reason; there is no vertical scale, sy is 1.0.)
constexpr float TEXT_DX = -84.0f;   // ROUND 101: -79 (fill) - 5 (outline pad)
constexpr float TEXT_W  = 123.0f;   // ROUND 101: 113 (fill) + 2*5 (outline pad)

struct Layout {
    float sx;
    float pitch;
    float first_cx;   // centre of the leftmost cell, scroll frame
    float textmc_x;
};

Layout layout(int digits) {
    if (digits <= 3) {
        return {1.0f, PITCH_WIDE,
                GROUP_CX - PITCH_WIDE * (digits - 1) * 0.5f, TEXTMC_WIDE};
    }
    if (digits == 4) {
        return {SX_NARROW, PITCH_NARROW, GROUP_CX + NARROW_X0, TEXTMC_NARROW};
    }
    // ROUND 91 DECISION -- extrapolated, the cabinet authors no 5-digit case.
    const float k     = 4.0f / static_cast<float>(digits);
    const float pitch = PITCH_NARROW * k;
    return {SX_NARROW * k, pitch,
            GROUP_CX + 92.0f - pitch * (digits - 1), TEXTMC_NARROW};
}

}  // namespace

// ---------------------------------------------------------------------------
// ROUND 110 -- the cross-player milestone-banner leak.
//
// SYMPTOM (user, verbatim): 「假設1p 達到300 combo,會正常出現300combo的圖示，
// 但是2p也會閃現1-2fps」-- when 1P reaches a milestone, 2P ALSO flashes a
// banner for a moment.
//
// ROOT CAUSE.  `tex.get_animation(65)` (texture.cpp:135, default
// `is_copy = false`) returns the pointer to the ONE GLOBAL FadeAnimation in
// `TextureWrapper::animations`, so BOTH players' ComboAnnounce instances aimed
// their `fade` member at the SAME object.  Together with two facts about the
// player side of this:
//
//   * `Player::combo_announce` is a `std::optional` that is assigned on every
//     milestone (player.cpp:1308-1310) and only ever CLEARED in
//     `Player::reset()` (player.cpp:2218).  A finished announce therefore keeps
//     being updated (player.cpp:469-471) and DRAWN (player.cpp:2150-2152) for
//     the rest of the song; it stays invisible only because its fade has run
//     down to `attribute == 0`.
//   * `draw()` below reads `fade_value = is_finished ? fade->attribute : ...`,
//     i.e. a finished announce's opacity IS that shared `attribute`.
//
// ...the constructor's `fade->start()` -> `BaseAnimation::restart()` ->
// `FadeAnimation::restart()` sets `attribute = initial_opacity = 1.0` on the
// object 2P's stale announce is still reading.  So the instant 1P's 300-combo
// banner is constructed, 2P's long-finished 100/200-combo banner snaps from
// opacity 0 to opacity 1 and then fades back out over animation 65's 100 ms.
// At 39.06's 120 fps that is 12 frames of a fully-opaque 2P banner that nothing
// on 2P triggered -- exactly the reported flash.
//
// Reproduced deterministically before the fix in `scratchpad/r110/sim.py`
// (a transcription of the three state machines involved, NOT the game binary)
// and then in the engine itself with the trace below; see MAPPING_hud.md.
//
// NOT A ROUND 91 REGRESSION.  `git show HEAD:src/objects/game/combo_announce.cpp`
// carries the identical `fade = (FadeAnimation*)tex.get_animation(65);` at what
// is now line 129, i.e. the shared pointer predates ROUND 91's per-digit
// rewrite and ROUND 101's suffix outline.  It is upstream code; no round of
// this skin introduced it.
//
// FIX, two parts, both confined to this file (no `player.cpp` edit):
//   1. `is_copy = true` -- every ComboAnnounce gets its OWN FadeAnimation, so
//      one player's milestone can no longer touch the other's opacity.  The
//      copy is owned by `TextureWrapper::copied_animations` (texture.cpp:146),
//      whose raw pointers stay valid across the vector's growth and which is
//      cleared by `unload_textures()`; at <= one copy per 100 combo per player
//      this is a handful of 100-byte objects per song.
//   2. A `draw()` early-out once the fade-out has actually completed, so a
//      stale announce that the player never clears cannot render at all,
//      whatever any animation's `attribute` happens to be.
//
//      Part 2 alone would NOT have fixed this, and it is worth being precise
//      about why: the leak's trigger is `fade->start()`, which goes through
//      `BaseAnimation::restart()` and sets `is_finished = false` on the shared
//      animation. The part-2 guard tests `fade->is_finished`, so on exactly the
//      frames the ghost appears the guard is disarmed. Part 1 is the fix; part 2
//      is a belt on the class of bug, not the braces on this one. (This is also
//      why the after-trace below is a real test of part 1 and not an artefact of
//      part 2 suppressing its own rows.)
// ---------------------------------------------------------------------------
ComboAnnounce::ComboAnnounce(int combo, double current_ms, PlayerNum player_num)
    : combo(combo), wait(current_ms), player_num(player_num),
      is_finished(false), audio_played(false) {

    // ROUND 110: `true` = per-instance COPY. Was the shared global; see above.
    // `-DR110_LEAK_REPRO` compiles the pre-fix behaviour back in, unchanged, so
    // the before/after traces differ in NOTHING but the fix. Never defined by
    // the build; it exists only so the A/B cannot be two runs of different code.
#ifdef R110_LEAK_REPRO
    fade = (FadeAnimation*)tex.get_animation(65);
#else
    fade = (FadeAnimation*)tex.get_animation(65, true);
#endif
    fade->start();
}

void ComboAnnounce::update(double current_ms) {
    if (current_ms >= wait + 1666.67f && !is_finished) {
        fade->start();
        is_finished = true;
    }

    fade->update(current_ms);

    if (!audio_played && combo >= 100) {
        std::string sound_name = "combo_" + std::to_string(combo) + "_" + std::to_string(static_cast<int>(player_num)) + "p";
        audio.play_sound(sound_name, VolumePreset::VOICE);
        audio_played = true;
    }
}

void ComboAnnounce::draw(float y) {
    if (combo == 0) {
        return;
    }

    // ROUND 110, part 2. `Player::combo_announce` is never cleared when a
    // banner ends (player.cpp only resets it in Player::reset()), so a spent
    // announce is still drawn every frame for the rest of the song and is
    // invisible only by virtue of its fade having reached 0. Make that
    // explicit: once the fade-out has completed, this object renders nothing.
#ifndef R110_LEAK_REPRO
    if (is_finished && fade->is_finished) {
        return;
    }
#endif

    float fade_value = is_finished ? fade->attribute : 1 - fade->attribute;

    // ROUND 110 evidence trail. Reuses ROUND 103's event recorder verbatim
    // (src/libs/perf.h): OFF unless the engine is started with
    // YATAIDON_R103_TRACE set, in which case each row is stamped with the
    // frame index of `perfdump`'s series and the whole log comes out over the
    // automation socket with `perfevents <path.csv>`. When off the cost is the
    // one static bool load inside events_enabled().
    if (perf::events_enabled()) {
        perf::note_event("combo_banner",
                         "p" + std::to_string(static_cast<int>(player_num)) +
                             " combo=" + std::to_string(combo) +
                             " fin=" + std::to_string(is_finished ? 1 : 0),
                         fade_value);
    }

    const std::string suffix = std::to_string(static_cast<int>(player_num)) + "p";
    tex.draw_texture(tex.get_enum("combo/announce_bg_" + suffix),
                     {.y = y, .fade = fade_value});

    // ROUND 91: per-digit path.  A skin that does not ship `announce_digit_*`
    // (the parent PyTaikoGreen does not) keeps the old pre-baked-triple
    // rendering below, unchanged.
    const std::string digit_name = "combo/announce_digit_" + suffix;
    if (tex.has_texture(digit_name)) {
        const std::string number = std::to_string(combo);
        const int n = static_cast<int>(number.size());
        const Layout lay = layout(n);
        const uint32_t digit_id = static_cast<uint32_t>(tex.get_enum(digit_name));
        const float dw = CELL * lay.sx;

        for (int i = 0; i < n; i++) {
            const float cx = lay.first_cx + lay.pitch * i;
            tex.draw_texture(digit_id, {
                .frame = number[i] - '0',
                .x  = cx - dw * 0.5f,
                .y  = y,
                .x2 = dw - CELL,          // horizontal condense only; sy stays 1
                .fade = fade_value,
            });
        }

        tex.draw_texture(COMBO::ANNOUNCE_TEXT, {
            .x  = lay.textmc_x + lay.sx * TEXT_DX,
            .y  = y,
            .x2 = TEXT_W * (lay.sx - 1.0f),
            .fade = fade_value,
        });
        return;
    }

    if (combo >= 1000) {
        int thousands = combo / 1000;
        int remaining_hundreds = (combo % 1000) / 100;
        float thousands_offset = tex.skin_config[SC::COMBO_ANNOUNCE_THOUSANDS_OFFSET].x;
        float hundreds_offset = tex.skin_config[SC::COMBO_ANNOUNCE_HUNDREDS_OFFSET].x;

        if (combo % 1000 == 0) {
            tex.draw_texture(COMBO::ANNOUNCE_NUMBER, {.frame = thousands - 1, .x = tex.skin_config[SC::COMBO_ANNOUNCE_NUMBER_THOUSANDS_X].x, .y = y, .fade = fade_value});
            tex.draw_texture(COMBO::ANNOUNCE_ADD, {.frame = 0, .x = tex.skin_config[SC::COMBO_ANNOUNCE_ADD_X].x, .y = y, .fade = fade_value});
        } else {
            if (thousands <= 5) {
                tex.draw_texture(COMBO::ANNOUNCE_ADD, {.frame = thousands, .x = tex.skin_config[SC::COMBO_ANNOUNCE_THOUSANDS_ADD_X].x + thousands_offset, .y = y, .fade = fade_value});
            }
            if (remaining_hundreds > 0) {
                tex.draw_texture(COMBO::ANNOUNCE_NUMBER, {.frame = remaining_hundreds - 1, .x = hundreds_offset, .y = y, .fade = fade_value});
            }
        }
        float text_offset = tex.skin_config[SC::COMBO_ANNOUNCE_TEXT_OFFSET].x;
        tex.draw_texture(COMBO::ANNOUNCE_TEXT, {.x = -text_offset / 2, .y = y, .fade = fade_value});
    } else {
        tex.draw_texture(COMBO::ANNOUNCE_NUMBER, {.frame = combo / 100 - 1, .x = 0, .y = y, .fade = fade_value});
        tex.draw_texture(COMBO::ANNOUNCE_TEXT, {.x = 0, .y = y, .fade = fade_value});
    }
}
