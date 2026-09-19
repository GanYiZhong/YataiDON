#pragma once

#include "../../libs/animation.h"
#include "../../libs/ray.h"

struct TextureObject;

class Combo {
private:
    int combo;
    TextStretchAnimation* stretch;
    std::vector<ray::Color> color;
    std::unordered_map<int, int> glimmer_map;
    int total_time;
    int cycle_time;
    std::vector<double> start_times;
    TextureObject* t_counter = nullptr;
    TextureObject* t_counter_gold = nullptr;
    TextureObject* t_counter_100 = nullptr;
    TextureObject* t_gleam = nullptr;
    TextureObject* t_combo = nullptr;
    TextureObject* t_combo_100 = nullptr;

    void update_count(int combo);
public:
    Combo() = default;
    Combo(int combo, double current_ms);

    void update(double current_ms, int curr_combo);

    void draw(float y);
};
