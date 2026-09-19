#include "score_history.h"

ScoreHistory::ScoreHistory(const std::array<std::optional<Score>, 5>& scores, double current_ms)
    : last_ms(current_ms)
{
    for (int i = 0; i < 5; i++) {
        if (scores[i].has_value())
            available.push_back({i, scores[i].value()});
    }
    if (!available.empty())
        curr_index = 1 % (int)available.size();

    t_background_2 = tex.get_texture("leaderboard/background_2");
    t_background = tex.get_texture("leaderboard/background");
    t_title = tex.get_texture("leaderboard/title");
    t_shinuchi_ura = tex.get_texture("leaderboard/shinuchi_ura");
    t_shinuchi = tex.get_texture("leaderboard/shinuchi");
    t_pts = tex.get_texture("leaderboard/pts");
    t_normal = tex.get_texture("leaderboard/normal");
    t_normal_ura = tex.get_texture("leaderboard/normal_ura");
    t_ura = tex.get_texture("leaderboard/ura");
    t_difficulty = tex.get_texture("leaderboard/difficulty");
    t_judge_good = tex.get_texture("leaderboard/judge_good");
    t_judge_ok = tex.get_texture("leaderboard/judge_ok");
    t_judge_bad = tex.get_texture("leaderboard/judge_bad");
    t_judge_drumroll = tex.get_texture("leaderboard/judge_drumroll");
    t_counter = tex.get_texture("leaderboard/counter");
    t_judge_num = tex.get_texture("leaderboard/judge_num");
}

void ScoreHistory::update(double current_ms) {
    if (available.empty()) return;
    if (current_ms >= last_ms + 1000.0) {
        last_ms = current_ms;
        curr_index = (curr_index + 1) % (int)available.size();
    }
}

void ScoreHistory::draw() {
    if (available.empty()) return;
    draw_long();
}

void ScoreHistory::draw_long() {
    const auto& [curr_diff, score] = available[curr_index];
    const std::string& score_method = global_data.config->general.score_method;
    float offset_y = tex.skin_config[SC::SCORE_INFO_BG_OFFSET].y;
    float margin_w = tex.skin_config[SC::SCORE_INFO_COUNTER_MARGIN].width;
    float margin_x = tex.skin_config[SC::SCORE_INFO_COUNTER_MARGIN].x;

    tex.draw_texture(t_background_2, {});
    tex.draw_texture(t_title, {.index = 1});

    if (score_method == ScoreMethod::SHINUCHI) {
        if (curr_diff == (int)Difficulty::URA)
            tex.draw_texture(t_shinuchi_ura, {.index = 1});
        else
            tex.draw_texture(t_shinuchi, {.index = 1});
        tex.draw_texture(t_pts, {.color = ray::WHITE, .index = 1});
    } else {
        tex.draw_texture(t_normal, {.index = 1});
        tex.draw_texture(t_pts, {.color = ray::BLACK, .index = 1});
    }

    tex.draw_texture(t_difficulty, {.frame = curr_diff, .index = 1});

    for (int i = 0; i < 4; i++)
        tex.draw_texture(t_normal, {.y = offset_y + i * offset_y, .index = 1});

    tex.draw_texture(t_judge_good, {});
    tex.draw_texture(t_judge_ok, {});
    tex.draw_texture(t_judge_bad, {});
    tex.draw_texture(t_judge_drumroll, {});

    std::array<int, 5> values = {score.score, score.good, score.ok, score.bad, score.drumroll};
    ray::Color score_color = (score_method == ScoreMethod::SHINUCHI) ? ray::WHITE : ray::BLACK;

    for (int j = 0; j < 5; j++) {
        std::string counter = std::to_string(values[j]);
        int len = (int)counter.size();
        if (j == 0) {
            for (int i = 0; i < len; i++) {
                float x = -((len * margin_w) / 2.0f) + (i * margin_w);
                tex.draw_texture(t_counter, {.color = score_color, .frame = counter[i] - '0', .x = x, .index = 1});
            }
        } else {
            for (int i = 0; i < len; i++) {
                float x = -(float)(len - i) * margin_x;
                float y = (float)j * offset_y;
                tex.draw_texture(t_judge_num, {.frame = counter[i] - '0', .x = x, .y = y});
            }
        }
    }
}

void ScoreHistory::draw_short() {
    if (available.empty()) return;

    const auto& [curr_diff, score] = available[curr_index];
    float offset_y = tex.skin_config[SC::SCORE_INFO_BG_OFFSET].y;
    float margin_w = tex.skin_config[SC::SCORE_INFO_COUNTER_MARGIN].width;

    tex.draw_texture(t_background, {});
    tex.draw_texture(t_title, {});

    ray::Color color = ray::BLACK;
    if (curr_diff == (int)Difficulty::URA) {
        tex.draw_texture(t_normal_ura, {});
        tex.draw_texture(t_shinuchi_ura, {});
        color = ray::WHITE;
        tex.draw_texture(t_ura, {});
    } else {
        tex.draw_texture(t_normal, {});
        tex.draw_texture(t_shinuchi, {});
    }

    tex.draw_texture(t_pts, {.color = color});
    tex.draw_texture(t_pts, {.color = color, .y = offset_y});
    tex.draw_texture(t_difficulty, {.frame = curr_diff});

    std::string counter = std::to_string(score.score);
    float total_width = (float)counter.size() * margin_w;
    for (int i = 0; i < (int)counter.size(); i++) {
        float x = -(total_width / 2.0f) + (float)i * margin_w;
        tex.draw_texture(t_counter, {.color = color, .frame = counter[i] - '0', .x = x});
    }
    for (int i = 0; i < (int)counter.size(); i++) {
        float x = -(total_width / 2.0f) + (float)i * margin_w;
        tex.draw_texture(t_counter, {.color = ray::WHITE, .frame = counter[i] - '0', .x = x, .y = offset_y});
    }
}
