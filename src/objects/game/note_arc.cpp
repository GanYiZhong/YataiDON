#include <rlgl.h>

#include "note_arc.h"
#include "../../libs/texture.h"
#include <cmath>

std::unordered_map<NoteArc::CacheKey, std::vector<std::pair<int, int>>, NoteArc::CacheKeyHash> NoteArc::_arc_points_cache;

namespace {

// ---------------------------------------------------------------------------
// The cabinet's balloon-pop rainbow, read out of
//   enso_normal/enso/onpu/onp_jump.nulm  sprite 68 ("geki_hit" / "geki_hit_add")
// with
//   lumen_anim_dump.exe onp_jump.nulm --sprite 68 --all --leaves
//
// The rig is three layers over 65 frames at 60 fps:
//   depth 0  mask1p (char 64 -> shape 62)  a SOLID 5-vertex polygon: the whole
//            1640x824 quad minus the wedge below a ray leaving the arc's centre
//            of curvature at -37.06 deg.  It is a Flash clip mask, so it is
//            never drawn -- it only decides how much of depth 1 is visible.
//   depth 1  shape 65                      the rainbow art itself, an exact
//            annulus sector about the same centre.
//   depth 2  kirakira                      an additive sparkle sprite (not
//            ported here, see MAPPING_hud.md).
//
// Both layers rotate about the SAME pivot, which the timeline puts 411.94 units
// below the shape's registration point -- i.e. at the bottom-centre of the
// 1640x824 quad (the quad is +-820 x +-412 around its origin, straight out of
// the 0xF024 vertex record).  Every curve in the rig is a straight line:
//   f  0..30   mask rotates -118 -> 0        (linear to within 0.17 deg)
//   f 30..39   hold, full arc visible
//   f 39..60   the ART rotates 0 -> +120     (linear to within 0.16 deg)
//   f 60..64   alpha 1 -> 0.75 -> 0.5 -> 0.25 -> 0
// so the reveal is an angular wipe whose leading edge tracks the flying note to
// within 0.4 deg, and the wipe-out eats the arc from the tail end.
//
// The note head itself is NOT on a Bezier: it runs the same circle, radius
// ~721, sweeping -154.774 -> -38.370 deg linearly over 30 frames (max deviation
// 0.06 deg over the dumped 31 frames).
// ---------------------------------------------------------------------------
constexpr float kRevealSweep = 118.0f;   // mask1p rotation over frames 0..30
constexpr float kWipeSweep   = 120.0f;   // shape 65 rotation over frames 39..60
constexpr int   kSectorSteps = 128;      // quads used to draw the annulus sector

inline float deg2rad(float d) { return d * 0.01745329252f; }

} // namespace

NoteArc::NoteArc(NoteType note_type, double current_ms, PlayerNum player_num, bool big, bool is_balloon, float start_x, float start_y)
    : note_type(note_type), start_ms(current_ms), player_num(player_num), is_big(big), is_balloon(is_balloon)
{
    arc_points = 100;
    // How long the note takes to fly from the judge circle to the soul gauge, in
    // 60 fps frames. PyTaikoGreen's 22 is kept as the default; the arcade
    // (enso/onpu/onp_jump.nulm, sprite 12 `don1p`) takes exactly 30 frames from
    // the judge anchor to the tamashii badge, so a skin can say so by name.
    // Optional key -> no generated SC entry and no edit to the base skin.
    arc_duration = 22;
    if (const SkinInfo* d = tex.skin_entry("note_arc_duration"); d && d->x > 0)
        arc_duration = (int)d->x;
    current_progress = 0;
    elapsed_frames = 0;

    float curve_height = tex.skin_config[SC::NOTE_ARC_CURVE_HEIGHT].height;
    this->start_x = start_x + tex.skin_config[SC::NOTE_ARC_START_X_OFFSET].x;
    this->start_y = start_y + tex.skin_config[SC::NOTES].y;
    end_x = tex.skin_config[SC::GAUGE_HIT_EFFECT_NOTE].x;
    end_y = tex.skin_config[SC::GAUGE_HIT_EFFECT_NOTE].y;

    // ------------------------------------------------------------------
    // ROUND 109 -- the 2P seat.  `Player::draw_overlays(y)` is ALREADY handed
    // that seat's lane top (game.cpp:638-639 -> 276 / 540) and NoteArc::draw
    // paints at `y + y_i`, so adding `offset_2p` (264) here too counted the
    // same 264 twice: the shipped 2P arc began at screen y 818 instead of 554,
    // i.e. below its own lane on the background band, dived past y 1087 and
    // landed 60 px short of the badge.  That is the reported defect.
    //
    // Cabinet (enso/onpu/onp_jump.nulm, sprite 78 frame 0 `don1p` = char 12 vs
    // frame 5 `don2p` = char 14, both dumped `--all --leaves`):
    //   * the two seats START at the SAME lane-local point, (618,110) centre =
    //     (522,14) top-left -> start_y takes NO 2P offset;
    //   * they END mirrored about the lane block -- 1P centre lane-local -30
    //     (30 above the block top), 2P +294 (30 below the block bottom).
    // The end offset is therefore 324 in top-left terms, which is exactly the
    // number `GaugeHitEffect` already adds from `gauge_hit_effect_note.height`
    // (`pos_data.height * is_2p`).  Sharing that one key keeps the flying note
    // and the badge burst on the same point by construction instead of by
    // coincidence.
    // ------------------------------------------------------------------
    if (player_num == PlayerNum::P2) {
        end_y += tex.skin_config[SC::GAUGE_HIT_EFFECT_NOTE].height;
    }

    // The note glyph is drawn from its TOP-LEFT, but every cabinet coordinate
    // and every pivot below is in the note's CENTRE frame, so carry the
    // half-note offset explicitly.  (Was inside the balloon block; the ordinary
    // arc needs it too now.)
    {
        uint32_t nid = static_cast<uint32_t>(tex.get_enum("notes/" + std::to_string((int)note_type)));
        auto nit = tex.textures.find(nid);
        if (nit != tex.textures.end() && nit->second) {
            half_w = nit->second->width * 0.5f;
            half_h = nit->second->height * 0.5f;
        }
    }

    // Shared by the balloon rig and by the ordinary arc: turn a declared pivot
    // (note-CENTRE frame, lane-local) plus this arc's own start/end into the
    // circle the cabinet sweeps.
    auto set_circle = [&](float px, float py) {
        pivot_x = px;
        pivot_y = py;
        float sx = this->start_x + half_w, sy = this->start_y + half_h;
        float ex = end_x + half_w,         ey = end_y + half_h;
        float r0 = std::hypot(sx - pivot_x, sy - pivot_y);
        float r1 = std::hypot(ex - pivot_x, ey - pivot_y);
        arc_radius = 0.5f * (r0 + r1);
        arc_a0 = std::atan2(sy - pivot_y, sx - pivot_x) * 57.29577951f;
        arc_a1 = std::atan2(ey - pivot_y, ex - pivot_x) * 57.29577951f;
        // Always take the short way round, over the top (or bottom for 2P).
        while (arc_a1 - arc_a0 > 180.0f)  arc_a1 -= 360.0f;
        while (arc_a1 - arc_a0 < -180.0f) arc_a1 += 360.0f;
        circular = true;
    };

    // ------------------------------------------------------------------
    // Arcade balloon path: a circle about a skin-declared pivot, not a
    // Bezier.  Entirely optional -- a skin that does not declare the pivot
    // (PyTaikoGreen and every third-party skin) keeps the old Bezier and the
    // old crop-trail rainbow, byte for byte.
    // ------------------------------------------------------------------
    if (is_balloon) {
        if (const SkinInfo* p = tex.skin_entry("note_arc_balloon_pivot"); p && (p->x != 0 || p->y != 0)) {
            float px = p->x, py = p->y;
            if (player_num == PlayerNum::P2) {
                // ROUND 109 (measured): the cabinet's 2P geki clip (sprite 70)
                // carries the SAME note path as `don2p` -- sprite 78 frames
                // 95..99 shape 47 reproduce frames 5..9 shape 8 exactly -- so
                // when a 2P pivot for the ordinary arc is declared, reuse it
                // here: one path, one circle.
                if (const SkinInfo* q = tex.skin_entry("note_arc_pivot_2p"); q && (q->x != 0 || q->y != 0)) {
                    px = q->x;
                    py = q->y;
                } else {
                    // Legacy, and now known to be only approximate: mirroring
                    // the 1P pivot through the note's own start line.  Kept for
                    // skins that declare no 2P pivot.
                    py = 2.0f * (this->start_y + half_h) - p->y;
                }
            }
            set_circle(px, py);
        }
        if (const SkinInfo* a = tex.skin_entry("note_arc_balloon_angles"); a) {
            art_a0 = a->x; art_a1 = a->y; art_cut = a->width;
        }
        if (const SkinInfo* a = tex.skin_entry("note_arc_balloon_art"); a) {
            art_px = a->x; art_py = a->y; art_r0 = a->width; art_r1 = a->height;
        }
        if (const SkinInfo* f = tex.skin_entry("note_arc_balloon_frames"); f) {
            ph_reveal = (int)f->x; ph_hold = (int)f->y; ph_wipe = (int)f->width; ph_end = (int)f->height;
        }
    }

    // ------------------------------------------------------------------
    // ROUND 109 -- the ORDINARY note arc is not a Bezier in the cabinet either.
    // Both seats are a constant-angular-rate sweep on a circle of radius ~720:
    // fitting a circle to the 31 flight frames of `onp_jump` sprite 12 (1P) and
    // sprite 14 (2P) leaves a max residual of 1.33 / 1.26 px, and the swept
    // angle departs from linear by at most 0.053 deg, so the whole 30-frame
    // flight is reproduced to <=1.35 px.
    //   1P  centre (1269.22, 415.14)  r 719.48  sweep -154.89 -> -38.24 deg
    //   2P  centre (1282.19, -167.83) r 720.10  sweep +157.30 -> +39.93 deg
    // The two are SEPARATELY AUTHORED clips, not one clip mirrored (see the
    // header note), which is why each seat gets its own key.  Both keys are
    // optional: a skin that declares neither keeps the quadratic Bezier below
    // byte for byte.
    // ------------------------------------------------------------------
    if (!circular) {
        const SkinInfo* p = tex.skin_entry(player_num == PlayerNum::P2 ? "note_arc_pivot_2p"
                                                                      : "note_arc_pivot");
        if (p && (p->x != 0 || p->y != 0)) set_circle(p->x, p->y);
    }

    if (circular) {
        // No point cache: the circle is two trig calls per frame.
        x_i = this->start_x;
        y_i = this->start_y;
        arc_points_cache = nullptr;
        return;
    }

    if (player_num == PlayerNum::P1) {
        // Control point influences the curve shape
        control_x = (this->start_x + end_x) / 2;
        control_y = std::min(this->start_y, end_y) - curve_height;  // Arc upward
    } else {
        control_x = (this->start_x + end_x) / 2;
        control_y = std::max(this->start_y, end_y) + curve_height;  // Arc downward
    }

    x_i = this->start_x;
    y_i = this->start_y;

    // Create cache key
    CacheKey cache_key = {this->start_x, this->start_y, end_x, end_y, control_x, control_y, arc_points};

    // Check if arc points are already cached
    if (_arc_points_cache.find(cache_key) == _arc_points_cache.end()) {
        std::vector<std::pair<int, int>> arc_points_list;
        arc_points_list.reserve(arc_points + 1);

        for (int i = 0; i <= arc_points; ++i) {
            float t = static_cast<float>(i) / arc_points;
            float t_inv = 1.0f - t;

            int x = static_cast<int>(t_inv * t_inv * this->start_x +
                                     2 * t_inv * t * control_x +
                                     t * t * end_x);
            int y = static_cast<int>(t_inv * t_inv * this->start_y +
                                     2 * t_inv * t * control_y +
                                     t * t * end_y);

            arc_points_list.emplace_back(x, y);
        }

        _arc_points_cache[cache_key] = arc_points_list;
    }

    arc_points_cache = &_arc_points_cache[cache_key];
}

void NoteArc::update(double current_ms) {
    double elapsed_time = (current_ms - start_ms) / 16.67;
    elapsed_frames = (float)std::max(0.0, elapsed_time);
    elapsed_time = std::max(0.0, std::min(elapsed_time, (double)arc_duration));

    current_progress = elapsed_time / arc_duration;

    if (circular) {
        float a = deg2rad(arc_a0 + (arc_a1 - arc_a0) * current_progress);
        x_i = pivot_x + arc_radius * std::cos(a) - half_w;
        y_i = pivot_y + arc_radius * std::sin(a) - half_h;
        return;
    }

    int point_index = current_progress * arc_points;
    if (point_index < arc_points_cache->size()) {
        x_i = (*arc_points_cache)[point_index].first;
        y_i = (*arc_points_cache)[point_index].second;
    } else {
        x_i = arc_points_cache->back().first;
        y_i = arc_points_cache->back().second;
    }
}

// Draw the visible slice of the arcade rainbow as an annulus sector.  The art
// is an exact annulus about `art_px/art_py` in its own texture, so every quad's
// four texture coordinates are the polar map of its four screen corners -- the
// same thing the cabinet's clip mask produces, with no re-baked sprite sheet.
void NoteArc::draw_balloon_rainbow(float y) const {
    auto st = std::dynamic_pointer_cast<SingleTexture>(tex.textures[BALLOON::RAINBOW]);
    if (!st || st->texture.id == 0) return;

    const float f = elapsed_frames;
    if (f >= (float)ph_end) return;

    // Mask rotation (reveal) and art rotation (wipe-out), both linear.
    float theta = (f < (float)ph_reveal) ? -kRevealSweep * (1.0f - f / (float)ph_reveal) : 0.0f;
    float phi   = (f > (float)ph_hold)
                    ? kWipeSweep * (f - (float)ph_hold) / (float)(ph_wipe - ph_hold)
                    : 0.0f;
    if (phi > kWipeSweep) phi = kWipeSweep;
    float alpha = (f > (float)ph_wipe) ? ((float)ph_end - 1.0f - f) / (float)(ph_end - 1 - ph_wipe) : 1.0f;
    if (alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;

    // Visible range in ART angles: everything from the tail up to the mask cut.
    float lo = art_a0;
    float hi = art_cut + theta - phi;
    if (hi > art_a1) hi = art_a1;
    if (hi <= lo) return;

    const float cx = pivot_x;          // pivot is declared in lane-local coords,
    const float cy = pivot_y + y;      // the same frame start_x/start_y live in
    const float tw = (float)st->texture.width;
    const float th = (float)st->texture.height;
    const float mirror = (player_num == PlayerNum::P2) ? -1.0f : 1.0f;

    int steps = (int)std::ceil(kSectorSteps * (hi - lo) / (art_a1 - art_a0));
    if (steps < 1) steps = 1;
    const float da = (hi - lo) / steps;
    const unsigned char a8 = (unsigned char)(alpha * 255.0f + 0.5f);

    rlSetTexture(st->texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, a8);
    for (int i = 0; i < steps; ++i) {
        const float aa = lo + da * i;          // art angle
        const float ab = aa + da;
        const float sa = deg2rad(aa + phi);    // screen angle
        const float sb = deg2rad(ab + phi);
        const float ta = deg2rad(aa), tb = deg2rad(ab);
        const float ca = std::cos(ta), sna = std::sin(ta);
        const float cb = std::cos(tb), snb = std::sin(tb);
        const float sca = std::cos(sa), ssa = std::sin(sa);
        const float scb = std::cos(sb), ssb = std::sin(sb);

        // rlgl wants the quad wound consistently; RL_QUADS is emitted as two
        // triangles in vertex order, so go inner-a, outer-a, outer-b, inner-b.
        rlTexCoord2f((art_px + art_r0 * ca) / tw, (art_py + art_r0 * sna) / th);
        rlVertex2f(cx + art_r0 * sca, cy + mirror * art_r0 * ssa);

        rlTexCoord2f((art_px + art_r1 * ca) / tw, (art_py + art_r1 * sna) / th);
        rlVertex2f(cx + art_r1 * sca, cy + mirror * art_r1 * ssa);

        rlTexCoord2f((art_px + art_r1 * cb) / tw, (art_py + art_r1 * snb) / th);
        rlVertex2f(cx + art_r1 * scb, cy + mirror * art_r1 * ssb);

        rlTexCoord2f((art_px + art_r0 * cb) / tw, (art_py + art_r0 * snb) / th);
        rlVertex2f(cx + art_r0 * scb, cy + mirror * art_r0 * ssb);
    }
    rlEnd();
    rlSetTexture(0);
}

void NoteArc::draw(float y, ray::Shader mask_shader) {
    if (is_balloon && circular) {
        draw_balloon_rainbow(y);
        if (elapsed_frames <= (float)arc_duration)
            tex.draw_texture(tex.get_enum("notes/" + (std::to_string((int)note_type))), {.x=x_i, .y=y + y_i});
        return;
    }

    if (is_balloon) {
        std::shared_ptr<TextureObject> rainbow = tex.textures[BALLOON::RAINBOW];
        float rainbow_height;
        if (player_num == PlayerNum::P2) {
            rainbow_height = -rainbow->height;
        } else {
            rainbow_height = rainbow->height;
        }
        float trail_length_ratio = 0.5f;
        float trail_start_progress = std::max(0.0f, current_progress - trail_length_ratio);
        float trail_end_progress = current_progress;

        if (trail_end_progress > trail_start_progress) {
            float crop_start_x = int(trail_start_progress * rainbow->width);
            float crop_end_x = int(trail_end_progress * rainbow->width);
            float crop_width = crop_end_x - crop_start_x;

            if (crop_width > 0) {
                ray::Rectangle src = {crop_start_x, 0, crop_width, rainbow_height};
                Mirror mirror;
                float y_pos;
                if (player_num == PlayerNum::P2) {
                    mirror = Mirror::VERTICAL;
                    y_pos = tex.skin_config[SC::NOTE_ARC_BALLOON_P2_Y].y;
                } else {
                    mirror = Mirror::NONE;
                    y_pos = 0;
                }
                ray::BeginShaderMode(mask_shader);
                tex.draw_texture(BALLOON::RAINBOW_MASK, {.mirror=mirror, .x=crop_start_x, .y=y + y_pos, .x2=-rainbow->width + crop_width, .src=src});
                ray::EndShaderMode();
            }
        }
    }
    tex.draw_texture(tex.get_enum("notes/" + (std::to_string((int)note_type))), {.x=x_i, .y=y + y_i});
}

bool NoteArc::is_finished() const {
    if (is_balloon && circular) return elapsed_frames >= (float)ph_end;
    return current_progress >= 1.0;
}
