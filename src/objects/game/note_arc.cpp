#include "note_arc.h"
#include <cmath>

std::unordered_map<NoteArc::CacheKey, std::vector<std::pair<int, int>>, NoteArc::CacheKeyHash> NoteArc::_arc_points_cache;

NoteArc::NoteArc(NoteType note_type, double current_ms, PlayerNum player_num, bool big, bool is_balloon, float start_x, float start_y)
    : note_type(note_type), start_ms(current_ms), player_num(player_num), is_big(big), is_balloon(is_balloon)
{
    arc_points = 100;
    // note_arc_frames: how many frames the flight takes
    const float frames = tex.skin_config[SC::NOTE_ARC_FRAMES].x;
    arc_duration = frames > 0 ? (int)frames : 31;
    current_progress = 0;
    elapsed_ms = 0;
    note_finished_handled = false;

    float curve_height = tex.skin_config[SC::NOTE_ARC_CURVE_HEIGHT].height;
    this->start_x = start_x + tex.skin_config[SC::NOTE_ARC_START_X_OFFSET].x;
    this->start_y = start_y + tex.skin_config[SC::NOTES].y;
    end_x = tex.skin_config[SC::GAUGE_HIT_EFFECT_NOTE].x;
    end_y = tex.skin_config[SC::GAUGE_HIT_EFFECT_NOTE].y;

    if (player_num == PlayerNum::P2) {
        end_y += tex.skin_config[SC::GAUGE_HIT_EFFECT_NOTE].height;
    }

    if (player_num == PlayerNum::P1) {
        // Control point influences the curve shape
        control_x = (this->start_x + end_x) / 2;
        control_y = std::min(this->start_y, end_y) - curve_height;  // Arc upward
    } else {
        control_x = (this->start_x + end_x) / 2;
        control_y = std::max(this->start_y, end_y) + curve_height;  // Arc downward
    }

    // note_arc_cubic: a cubic Bezier instead of the quadratic one, its two control points given
    // by note_arc_cubic_control as offsets from the start (x, y) and from the end (width,
    // height); the vertical offsets are mirrored for P2, whose arc runs downward
    arc_cubic = tex.options[SCO::NOTE_ARC_CUBIC];
    if (arc_cubic) {
        const SkinInfo& c = tex.skin_config[SC::NOTE_ARC_CUBIC_CONTROL];
        const float flip = player_num == PlayerNum::P2 ? -1.0f : 1.0f;
        control1_x = this->start_x + c.x;
        control1_y = this->start_y + c.y * flip;
        control2_x = end_x + c.width;
        control2_y = end_y + c.height * flip;
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
            float t = (float)i / arc_points;
            float t_inv = 1.0f - t;

            int x = (int)(t_inv * t_inv * this->start_x +
                          2 * t_inv * t * control_x +
                          t * t * end_x);
            int y = (int)(t_inv * t_inv * this->start_y +
                          2 * t_inv * t * control_y +
                          t * t * end_y);

            arc_points_list.emplace_back(x, y);
        }

        _arc_points_cache[cache_key] = arc_points_list;
    }

    arc_points_cache = &_arc_points_cache[cache_key];

    NoteType texture_note_type = note_type;
    if (big) {
        if (note_type == NoteType::DON) {
            texture_note_type = NoteType::DON_L;
        } else if (note_type == NoteType::KAT) {
            texture_note_type = NoteType::KAT_L;
        }
    }
    if (note_type == NoteType::BALLOON_HEAD || is_balloon) {
        texture_note_type = NoteType::DON_L;
    }
    t_note = tex.get_texture("notes/" + std::to_string((int)texture_note_type));
    // note_arc_firework_cabinet: the cabinet only trails sparks behind a hit big don/ka, not
    // behind big drumroll hits (which arc as plain DON/KAT with big set)
    firework_cabinet = tex.options[SCO::NOTE_ARC_FIREWORK_CABINET];
    const bool big_hit = note_type == NoteType::DON_L || note_type == NoteType::KAT_L;
    if (big && (!firework_cabinet || big_hit)) {
        t_firework = tex.get_texture("hit_effect/firework");
    }
    t_rainbow_mask = tex.get_texture("balloon/rainbow_mask");
}

// cropped sheets are a single texture, so frame_count() alone reports 1
int NoteArc::firework_frames() const {
    int n = t_firework->frame_count();
    if (t_firework->crop_data.has_value()) n = std::max(n, (int)t_firework->crop_data->size());
    return n;
}

// where the note is, progress 0..1 along the flight
std::pair<float, float> NoteArc::point_at(double progress) const {
    if (arc_cubic) {
        const float t = (float)std::clamp(progress, 0.0, 1.0);
        const float u = 1.0f - t;
        const float b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
        return {b0 * start_x + b1 * control1_x + b2 * control2_x + b3 * end_x,
                b0 * start_y + b1 * control1_y + b2 * control2_y + b3 * end_y};
    }
    const std::size_t point_index = std::min((std::size_t)(progress * arc_points), arc_points_cache->size() - 1);
    return {(float)(*arc_points_cache)[point_index].first, (float)(*arc_points_cache)[point_index].second};
}

void NoteArc::update(double current_ms) {
    // elapsed_ms keeps running past the arc so trailing fireworks can finish
    elapsed_ms = std::max(0.0, current_ms - start_ms);
    current_progress = std::min(1.0, elapsed_ms / ((double)arc_duration * 16.67));

    if (arc_points_cache == nullptr || arc_points_cache->empty()) return;
    const auto [x, y] = point_at(current_progress);
    x_i = x;
    y_i = y;
}

void NoteArc::draw(float y, ray::Shader mask_shader) {
    if (is_balloon) {
        const std::shared_ptr<TextureObject>& rainbow = tex.textures["balloon/rainbow"];
        if (!rainbow || !t_rainbow_mask) {
            if (current_progress < 1.0) tex.draw_texture(t_note, {.x=x_i, .y=y + y_i});
            return;
        }
        float rainbow_height;
        if (player_num == PlayerNum::P2) {
            rainbow_height = -t_rainbow_mask->height;
        } else {
            rainbow_height = t_rainbow_mask->height;
        }
        float trail_length_ratio = 0.5f;
        const float raw_progress = elapsed_ms / ((double)arc_duration * 16.67);
        float trail_start_progress = std::max(0.0f, raw_progress - trail_length_ratio);
        float trail_end_progress = std::min(raw_progress, 1.0f);

        if (trail_end_progress > trail_start_progress) {
            float crop_start_x = std::round(trail_start_progress * t_rainbow_mask->width);
            float crop_end_x = std::round(trail_end_progress * t_rainbow_mask->width);
            float crop_width = crop_end_x - crop_start_x;

            if (crop_width > 0) {
                ray::Rectangle src = {crop_start_x, 0, crop_width, rainbow_height};
                Mirror mirror = Mirror::NONE;
                float y_pos;
                if (player_num == PlayerNum::P2) {
                    y_pos = tex.skin_config[SC::NOTE_ARC_BALLOON_P2_Y].y;
                } else {
                    y_pos = 0;
                }
                ray::BeginShaderMode(mask_shader);
                // rlgl clears the extra sampler slots after every batch, so GameScreen's one-time
                // binding of texture1 (the rainbow's colours) only lasted for the first batch; after
                // that the shader sampled an empty unit and the trail came out opaque black
                if (const ray::Texture2D* rt = rainbow->frame_texture(0)) {
                    ray::SetShaderValueTexture(mask_shader, ray::GetShaderLocation(mask_shader, "texture1"), *rt);
                }
                tex.draw_texture(t_rainbow_mask, {.mirror=mirror, .x=crop_start_x, .y=y + y_pos, .x2=-t_rainbow_mask->width + crop_width, .src=src});
                ray::EndShaderMode();
            }
        }
    } else if (is_big && !is_balloon && t_firework && firework_frames() > 0 &&
               arc_points_cache && !arc_points_cache->empty()) {
        // note_arc_firework_spawn: the first spark's frame (x), the frames between sparks (y)
        // and how many (width); note_arc_firework_path: where each sits on the arc, in frames
        // of the flight (x, then y apart)
        const double frame_ms = 16.67;
        const int frame_count = firework_frames();
        const SkinInfo& spawn = tex.skin_config[SC::NOTE_ARC_FIREWORK_SPAWN];
        const SkinInfo& path = tex.skin_config[SC::NOTE_ARC_FIREWORK_PATH];
        const float scale = tex.skin_config[SC::NOTE_ARC_FIREWORK_SCALE].x;
        const int count = (int)spawn.width;
        for (int i = 0; i < count; ++i) {
            const double spawn_ms = (spawn.x + spawn.y * i) * frame_ms;
            const double age_ms = elapsed_ms - spawn_ms;
            const int frame = (int)(age_ms / frame_ms);
            if (age_ms < 0 || frame >= frame_count) continue;
            const double position_progress = std::clamp((path.x + path.y * i) / (double)arc_duration, 0.0, 1.0);
            const auto [firework_x, firework_y] = point_at(position_progress);
            if (firework_cabinet) {
                // centred on the note as it passed that point, added onto what is below
                const float dx = t_note->x[0] - t_firework->x[0] + (t_note->x2[0] - t_firework->x2[0]) / 2.0f;
                const float dy = t_note->y[0] - t_firework->y[0] + (t_note->y2[0] - t_firework->y2[0]) / 2.0f;
                tex.draw_texture(t_firework, {
                    .frame=frame,
                    .scale=scale,
                    .center=true,
                    .x=firework_x + dx,
                    .y=y + firework_y + dy,
                    .blend=ray::BLEND_ADDITIVE
                });
            } else {
                tex.draw_texture(t_firework, {
                    .frame=frame,
                    .scale=scale,
                    .x=firework_x - (t_note->width*0.6f) / 2.0f,
                    .y=y + firework_y - (t_note->width*0.6f) / 2.0f
                });
            }
        }
    }
    if (!is_note_finished()) {
        tex.draw_texture(t_note, {.x=x_i, .y=y + y_i});
    }
}

bool NoteArc::is_finished() const {
    double end_ms = (double)arc_duration * 16.67;
    if (is_balloon) end_ms *= 1.5;
    if (t_firework) end_ms = std::max(end_ms + firework_frames() * 16.67, firework_end_ms());
    return elapsed_ms >= end_ms;
}

// when the last spark finishes playing
double NoteArc::firework_end_ms() const {
    const SkinInfo& spawn = tex.skin_config[SC::NOTE_ARC_FIREWORK_SPAWN];
    const int count = (int)spawn.width;
    if (count <= 0) return 0.0;
    return (spawn.x + spawn.y * (count - 1) + firework_frames()) * 16.67;
}

bool NoteArc::is_note_finished() const {
    return current_progress >= 1.0;
}

bool NoteArc::consume_note_finished() {
    if (note_finished_handled || !is_note_finished()) return false;
    note_finished_handled = true;
    return true;
}
