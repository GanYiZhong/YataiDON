#pragma once

#include "../../libs/global_data.h"
#include "../../libs/parsers/tja.h"

class NoteArc {
private:

    struct CacheKey {
            float start_x, start_y, end_x, end_y, control_x, control_y;
            int arc_points;

            bool operator==(const CacheKey& other) const {
                return start_x == other.start_x && start_y == other.start_y &&
                    end_x == other.end_x && end_y == other.end_y &&
                    control_x == other.control_x && control_y == other.control_y &&
                    arc_points == other.arc_points;
            }
        };

        struct CacheKeyHash {
            std::size_t operator()(const CacheKey& k) const {
                std::size_t h1 = std::hash<float>{}(k.start_x);
                std::size_t h2 = std::hash<float>{}(k.start_y);
                std::size_t h3 = std::hash<float>{}(k.end_x);
                std::size_t h4 = std::hash<float>{}(k.end_y);
                std::size_t h5 = std::hash<float>{}(k.control_x);
                std::size_t h6 = std::hash<float>{}(k.control_y);
                std::size_t h7 = std::hash<int>{}(k.arc_points);
                return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5) ^ (h7 << 6);
            }
        };

        static std::unordered_map<CacheKey, std::vector<std::pair<int, int>>, CacheKeyHash> _arc_points_cache;
        const std::vector<std::pair<int, int>>* arc_points_cache;

    bool is_balloon;
    int arc_points;
    int arc_duration;
    float current_progress;
    double start_ms;
    PlayerNum player_num;

    float start_x;
    float start_y;
    float end_x;
    float end_y;
    float control_x;
    float control_y;
    float x_i;
    float y_i;

    // --- arcade balloon rig (onp_jump.nulm sprite 68); see note_arc.cpp -----
    // All of this stays inert unless the skin declares
    // `note_arc_balloon_pivot`, so every other skin keeps the Bezier path and
    // the crop-trail rainbow untouched.
    bool  circular      = false;   // circular note path + masked rainbow
    float elapsed_frames = 0.0f;   // 60 fps frames since the pop (unclamped)
    float pivot_x = 0, pivot_y = 0;   // circle centre, lane-local
    float arc_radius = 0;
    float half_w = 0, half_h = 0;     // note texture half-size (centre <-> top-left)
    float arc_a0 = 0, arc_a1 = 0;     // note sweep, degrees
    float art_a0 = -151.47f, art_a1 = -38.37f, art_cut = -37.06f;
    float art_px = 820.0f, art_py = 824.0f;   // arc centre inside the texture
    float art_r0 = 636.0f, art_r1 = 804.0f;   // band radii inside the texture
    int   ph_reveal = 30, ph_hold = 39, ph_wipe = 60, ph_end = 65;

    void draw_balloon_rainbow(float y) const;
public:
    NoteType note_type;
    bool is_big;

    NoteArc(NoteType note_type, double current_ms, PlayerNum player_num, bool big, bool is_balloon, float start_x = 0, float start_y = 0);

    void update(double current_ms);

    void draw(float y, ray::Shader mask_shader);

    bool is_finished() const;
};
