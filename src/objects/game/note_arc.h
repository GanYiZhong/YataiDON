#pragma once

#include "../../libs/global_data.h"
#include "../../libs/parsers/tja.h"

#include "../../libs/texture.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

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
            // Normalize -0.0f to 0.0f so that values comparing equal via CacheKey::operator==
            // (which uses IEEE-754 == and treats -0.0f == 0.0f) always hash the same.
            static float norm(float x) { return x == 0.0f ? 0.0f : x; }

            std::size_t operator()(const CacheKey& k) const {
                std::size_t h1 = std::hash<float>{}(norm(k.start_x));
                std::size_t h2 = std::hash<float>{}(norm(k.start_y));
                std::size_t h3 = std::hash<float>{}(norm(k.end_x));
                std::size_t h4 = std::hash<float>{}(norm(k.end_y));
                std::size_t h5 = std::hash<float>{}(norm(k.control_x));
                std::size_t h6 = std::hash<float>{}(norm(k.control_y));
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
    double elapsed_ms;
    bool note_finished_handled;
    double start_ms;
    PlayerNum player_num;

    float start_x;
    float start_y;
    float end_x;
    float end_y;
    float control_x;
    float control_y;
    bool arc_cubic = false;
    float control1_x = 0, control1_y = 0, control2_x = 0, control2_y = 0;
    std::pair<float, float> point_at(double progress) const;
    float x_i;
    float y_i;
    int firework_frames() const;
    double firework_end_ms() const;
    bool firework_cabinet = false;
    TextureObject* t_note = nullptr;
    TextureObject* t_firework = nullptr;
    TextureObject* t_rainbow_mask = nullptr;
public:
    NoteType note_type;
    bool is_big;

    NoteArc(NoteType note_type, double current_ms, PlayerNum player_num, bool big, bool is_balloon, float start_x = 0, float start_y = 0);

    void update(double current_ms);

    void draw(float y, ray::Shader mask_shader);

    bool is_finished() const;

    bool is_note_finished() const;
    bool consume_note_finished();
};
