#pragma once

#include "../../../libs/scores.h"

#include "../../../libs/texture.h"

class ScoreHistory {
public:
    ScoreHistory(const std::array<std::optional<Score>, 5>& scores, double current_ms);
    void update(double current_ms);
    void draw();

private:
    struct DiffScore { int diff; Score score; };
    std::vector<DiffScore> available;
    int curr_index = 0;
    double last_ms = 0.0;

    void draw_long();
    void draw_short();

    // Fixed-path textures resolved once in the constructor instead of calling
    // tex.get_texture() every frame from draw_long()/draw_short().
    TextureObject* t_background_2 = nullptr;
    TextureObject* t_background = nullptr;
    TextureObject* t_title = nullptr;
    TextureObject* t_shinuchi_ura = nullptr;
    TextureObject* t_shinuchi = nullptr;
    TextureObject* t_pts = nullptr;
    TextureObject* t_normal = nullptr;
    TextureObject* t_normal_ura = nullptr;
    TextureObject* t_ura = nullptr;
    TextureObject* t_difficulty = nullptr;
    TextureObject* t_judge_good = nullptr;
    TextureObject* t_judge_ok = nullptr;
    TextureObject* t_judge_bad = nullptr;
    TextureObject* t_judge_drumroll = nullptr;
    TextureObject* t_counter = nullptr;
    TextureObject* t_judge_num = nullptr;
};
