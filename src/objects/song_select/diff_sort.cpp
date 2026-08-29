#include "diff_sort.h"
#include "song_select_script.h"
#include "../../libs/audio.h"

// ROUND 15 - arcade timings, taken from the cabinet, not tuned by eye:
//   root MC `common_song_select_sort_window` labels in@5..14 / out@15..24 = 10 frames
//   song_select_select_sort_window.lua: kStartWait = 1*FPS, kEndWait = 2*FPS
//   arrow MC (sprite 23) label select@5 runs to f15 = 10 frames, x 0 -> -12.5 -> 0
static constexpr double SORT_IN_MS         = 1000.0 / 60.0 * 10.0;
static constexpr double SORT_OUT_MS        = 1000.0 / 60.0 * 10.0;
static constexpr double SORT_START_WAIT_MS = 1000.0;
static constexpr double SORT_END_WAIT_MS   = 2000.0;
static constexpr double SORT_ARROW_MS      = 1000.0 / 60.0 * 10.0;
static constexpr float  SORT_ARROW_PUSH    = 12.5f;

DiffSortSelect::DiffSortSelect(Statistics statistics, int prev_diff, int prev_level,
                               SongSelectScript* script, int prev_order)
    : prev_diff(prev_diff), prev_level(prev_level), statistics(statistics), script(script) {
    arcade = script && script->has_sort_window();
    selected_box = -1;
    selected_level = 1;
    in_level_select = false;
    confirmation = false;
    confirm_index = 1;
    num_boxes = 6;
    limits = {5, 7, 8, 10, 10};

    bg_resize = (TextureResizeAnimation*)tex.get_animation(19);
    diff_fade_in = (FadeAnimation*)tex.get_animation(20);
    box_flicker = (FadeAnimation*)tex.get_animation(21);
    bounce_up_1 = (MoveAnimation*)tex.get_animation(22);
    bounce_down_1 = (MoveAnimation*)tex.get_animation(23);
    bounce_up_2 = (MoveAnimation*)tex.get_animation(24);
    bounce_down_2 = (MoveAnimation*)tex.get_animation(25);
    blue_arrow_fade = (FadeAnimation*)tex.get_animation(29);
    blue_arrow_move = (MoveAnimation*)tex.get_animation(30);

    // The old UI's intro animations LOCK INPUT while they run: Animation::start()
    // does `global_data.input_locked++` and only the update() that finishes them
    // gives it back (src/libs/animation.cpp), and `poll_keyboard_once()` produces no
    // key events at all while it is non-zero. The arcade window has its own timeline
    // and never updates these, so starting them here left input locked for as long as
    // the window was open - it drew perfectly and answered nothing.
    if (!arcade) {
        bg_resize->start();
        diff_fade_in->start();
        box_flicker->start();
    }

    for (const auto& [course, levels] : statistics) {
        std::array<int, 3> sums = {0, 0, 0};
        for (const auto& [level, stats] : levels) {
            sums[0] += stats.total;
            sums[1] += stats.clears;
            sums[2] += stats.full_combos;
        }
        diff_sort_sum_stat[course] = sums;
    }

    if (arcade) {
        // SetWindowParam: 0 / 99 (never chosen yet) fall back to the first entry.
        sort_param[0] = (prev_diff  >= 0 && prev_diff  < diff_count) ? prev_diff + 1 : 1;
        sort_param[1] = (prev_level >= star_min && prev_level <= star_max) ? prev_level : 1;
        sort_param[2] = (prev_order >= 1 && prev_order <= order_count) ? prev_order : 1;
        session = 0;
        t_open = now_ms = get_current_ms();
        arcade_refresh_song_num();
    }

    // StartSortWindow plays voice_senkuoku_v12a/select_muzukashisa + se_common/don_c.
    audio.play_sound("voice_diff_sort_enter", VolumePreset::VOICE);

}

void DiffSortSelect::arcade_refresh_song_num() {
    // PlayDataManager.GetDiffcultySongNum(diff, star) = how many songs carry that
    // (course, level).  Our Statistics is exactly that table.  Course 5 (oni-ura)
    // is not a separate course in the engine's stats, so it reads the oni row.
    song_num = statistics[std::min(sort_param[0], 5) - 1][sort_param[1]].total;
}

bool DiffSortSelect::arcade_input_locked() const {
    // isStartWait (1 s) and isEndWait (the 2 s hold) both swallow input.
    return (now_ms - t_open) < SORT_START_WAIT_MS || t_end > 0.0;
}

int DiffSortSelect::lua_phase() const {
    if (t_out > 0.0) return 3;
    if (t_end > 0.0) return 2;
    if ((now_ms - t_open) < SORT_IN_MS) return 0;
    return 1;
}

float DiffSortSelect::lua_alpha() const {
    if (t_out > 0.0) {
        float u = (float)((now_ms - t_out) / SORT_OUT_MS);
        return std::max(0.0f, 1.0f - u);
    }
    float u = (float)((now_ms - t_open) / SORT_IN_MS);
    return std::min(1.0f, std::max(0.0f, u));
}

float DiffSortSelect::lua_arrow_offset(int row) const {
    if (row < 0 || row > 2 || arrow_dir[row] == 0) return 0.0f;
    double e = now_ms - arrow_t0[row];
    if (e < 0.0 || e >= SORT_ARROW_MS) return 0.0f;
    // f5..f9 push out (0 -> -12.5), f10..f15 ease back to 0 (the .nulm keyframes are
    // -10.4, -8.4, -6.2, -4.2, -2.1, 0 - linear within each half is exact to <0.3 px).
    double f = e / SORT_ARROW_MS * 10.0;
    float local = (f <= 5.0) ? (float)(-SORT_ARROW_PUSH * (f / 5.0))
                             : (float)(-SORT_ARROW_PUSH * (1.0 - (f - 5.0) / 5.0));
    return local * (float)arrow_dir[row];
}

// ChangeParam(move): clamp per row, refresh the counter, bounce that row's arrow.
void DiffSortSelect::arcade_change_param(int move) {
    if (arcade_input_locked()) return;
    audio.play_sound("kat", VolumePreset::SOUND);     // arcade se_common_v12a/katsu_c
    int& v = sort_param[session];
    v += move;
    if (session == 1)      v = std::min(star_max,    std::max(star_min, v));
    else if (session == 0) v = std::min(diff_count,  std::max(1, v));
    else                   v = std::min(order_count, std::max(1, v));
    arcade_refresh_song_num();
    arrow_dir[session] = (move > 0) ? 1 : -1;
    arrow_t0[session]  = now_ms;
}

std::optional<std::array<int, 3>> DiffSortSelect::take_result() {
    if (!finished) return std::nullopt;
    finished = false;
    return arcade_result;
}

void DiffSortSelect::update(double current_ms) {
    if (arcade) {
        now_ms = current_ms;
        if (t_end > 0.0 && t_out == 0.0 && (current_ms - t_end) >= SORT_END_WAIT_MS)
            t_out = current_ms;                       // CloseSortWindow -> label "out"
        if (t_out > 0.0 && !arcade_result && (current_ms - t_out) >= SORT_OUT_MS) {
            arcade_result = std::array<int, 3>{ sort_param[0] - 1, sort_param[1], sort_param[2] };
            finished = true;
        }
        return;
    }
    bg_resize->update(current_ms);
    diff_fade_in->update(current_ms);
    box_flicker->update(current_ms);
    bounce_up_1->update(current_ms);
    bounce_down_1->update(current_ms);
    bounce_up_2->update(current_ms);
    bounce_down_2->update(current_ms);
}

std::optional<std::pair<int, int>> DiffSortSelect::input_select() {
    if (arcade) {
        if (arcade_input_locked()) return std::nullopt;
        // CursorMove: the star row refuses to be left when nothing matches.
        if (session == 1 && song_num <= 0) {
            audio.play_sound("cancel", VolumePreset::SOUND);   // arcade se_common_v12a/beep_c
            return std::nullopt;
        }
        audio.play_sound("don", VolumePreset::SOUND);
        session++;
        if (session > 2) t_end = now_ms;              // isEndWait
        return std::nullopt;
    }
    if (confirmation) {
        if (confirm_index == 0) {
            confirmation = false;
        } else if (confirm_index == 1) {
            return {{selected_box, selected_level}};
        } else if (confirm_index == 2) {
            confirmation = false;
            in_level_select = false;
            return std::nullopt;
        }
    } else if (in_level_select) {
        confirmation = true;
        bounce_up_1->start();
        bounce_down_1->start();
        bounce_up_2->start();
        bounce_down_2->start();
        confirm_index = 1;
        audio.play_sound("voice_diff_sort_confirm", VolumePreset::VOICE);
        return std::nullopt;
    }
    if (selected_box == -1) return {{-1, -1}};
    if (selected_box == 5) return {{prev_diff, prev_level}};

    audio.play_sound("voice_diff_sort_level", VolumePreset::VOICE);
    in_level_select = true;
    bg_resize->start();
    diff_fade_in->start();
    selected_level = std::min(selected_level, limits[selected_box]);
    return std::nullopt;
}

void DiffSortSelect::input_left() {
    if (arcade) { arcade_change_param(-1); return; }
    if (confirmation) {
        confirm_index = std::max(confirm_index - 1, 0);
    } else if (in_level_select) {
        selected_level = std::max(selected_level - 1, 1);
    } else {
        selected_box = std::max(selected_box - 1, -1);
    }
}

void DiffSortSelect::input_right() {
    if (arcade) { arcade_change_param(1); return; }
    if (confirmation) {
        confirm_index = std::min(confirm_index + 1, 2);
    } else if (in_level_select) {
        selected_level = std::min(selected_level + 1, limits[selected_box]);
    } else {
        selected_box = std::min(selected_box + 1, num_boxes - 1);
    }
}

void DiffSortSelect::draw_statistics() {
    std::string player_num_str = std::to_string((int)global_data.player_num);
    tex.draw_texture(tex.get_enum("diff_sort/stat_bg_" + player_num_str + "p"));
    tex.draw_texture(DIFF_SORT::STAT_OVERLAY);
    tex.draw_texture(DIFF_SORT::STAT_DIFF, {.frame=std::min(selected_box, 4)});

    if (in_level_select || selected_box == 5) {
        tex.draw_texture(DIFF_SORT::STAT_STARX);
        std::string counter;
        if (selected_box == 5) {
            tex.draw_texture(DIFF_SORT::STAT_PREV);
            counter = std::to_string(prev_level);
        } else {
            counter = std::to_string(selected_level);
        }
        float margin = tex.skin_config[SC::DIFF_SORT_MARGIN_1].x;
        float total_width = counter.size() * margin;
        for (size_t i = 0; i < counter.size(); i++) {
            int digit = counter[i] - '0';
            tex.draw_texture(DIFF_SORT::STAT_NUM_STAR, {.frame=digit, .x=tex.skin_config[SC::DIFF_SORT_STAT_NUM_STAR].x-(counter.size() - i) * margin, .y=tex.skin_config[SC::DIFF_SORT_STAT_NUM_STAR].y});
        }

        counter = std::to_string(statistics[selected_box][selected_level].total);
        if (selected_box == 5) counter = std::to_string(statistics[prev_diff][prev_level].total);
        margin = tex.skin_config[SC::DIFF_SORT_MARGIN_2].x;
        total_width = counter.size() * margin;
        for (size_t i = 0; i < counter.size(); i++) {
            int digit = counter[i] - '0';
            tex.draw_texture(DIFF_SORT::STAT_NUM, {.frame=digit, .x=-(total_width/2)+(i*margin)});
        }

        for (int j = 0; j < 2; j++) {
            std::string counter;
            if (selected_box == 5) {
                counter = std::to_string(statistics[prev_diff][prev_level].total);
            } else {
                counter = std::to_string(statistics[selected_box][selected_level].total);
            }

            margin = tex.skin_config[SC::DIFF_SORT_MARGIN_3].x;
            total_width = counter.size() * margin;

            for (int i = 0; i < (int)counter.size(); i++) {
                int digit = counter[i] - '0';
                tex.draw_texture(DIFF_SORT::STAT_NUM_SMALL, {.frame=digit, .x=-(total_width / 2) + (i * margin), .index=j});
            }
        }

        for (int j = 0; j < 2; j++) {
            std::string counter;
            if (selected_box == 5) {
                if (j + 1 == 1) {
                    counter = std::to_string(statistics[prev_diff][prev_level].full_combos);
                } else {
                    counter = std::to_string(statistics[prev_diff][prev_level].clears);
                }
            } else {
                if (j + 1 == 1) {
                    counter = std::to_string(statistics[selected_box][selected_level].full_combos);
                } else {
                    counter = std::to_string(statistics[selected_box][selected_level].clears);
                }
            }

            margin = tex.skin_config[SC::DIFF_SORT_MARGIN_1].x;
            total_width = counter.size() * margin;

            for (int i = 0; i < (int)counter.size(); i++) {
                int digit = counter[i] - '0';
                tex.draw_texture(DIFF_SORT::STAT_NUM_STAR, {.frame=digit, .x=-(total_width / 2) + (i * margin), .index=j+1});
            }
        }
    } else {
        std::string counter = std::to_string(diff_sort_sum_stat[selected_box][0]);
        float margin = tex.skin_config[SC::DIFF_SORT_MARGIN_2].x;
        float total_width = counter.size() * margin;
        for (size_t i = 0; i < counter.size(); i++) {
            int digit = counter[i] - '0';
            tex.draw_texture(DIFF_SORT::STAT_NUM, {.frame=digit, .x=-(total_width/2)+(i*margin)});
        }

        for (int j = 0; j < 2; j++) {
            counter = std::to_string(diff_sort_sum_stat[selected_box][0]);
            margin = tex.skin_config[SC::DIFF_SORT_MARGIN_3].x;
            total_width = counter.size() * margin;
            for (size_t i = 0; i < counter.size(); i++) {
                int digit = counter[i] - '0';
                tex.draw_texture(DIFF_SORT::STAT_NUM_SMALL, {.frame=digit, .x=-(total_width/2)+(i*margin), .index=j});
            }
        }

        for (int j = 0; j < 2; j++) {
            if (j + 1 == 1) {
                counter = std::to_string(diff_sort_sum_stat[selected_box][2]);
            } else {
                counter = std::to_string(diff_sort_sum_stat[selected_box][1]);
            }
            margin = tex.skin_config[SC::DIFF_SORT_MARGIN_1].x;
            total_width = counter.size() * margin;
            for (size_t i = 0; i < counter.size(); i++) {
                int digit = counter[i] - '0';
                tex.draw_texture(DIFF_SORT::STAT_NUM_STAR, {.frame=digit, .x=-(total_width/2)+(i*margin), .index=j+1});
            }
        }
    }
}

void DiffSortSelect::draw_diff_select() {
    tex.draw_texture(DIFF_SORT::BACKGROUND, {.scale=(float)bg_resize->attribute, .center=true});

    tex.draw_texture(tex.get_enum("diff_sort/back_" + global_data.config->general.language), {.fade=diff_fade_in->attribute});
    float offset = tex.skin_config[SC::DIFF_SORT_OFFSET].x;
    for (size_t i = 0; i < num_boxes; i++) {
        if (i == selected_box) {
            tex.draw_texture(DIFF_SORT::BOX_HIGHLIGHT, {.x=(offset*i), .fade=diff_fade_in->attribute});
            tex.draw_texture(DIFF_SORT::BOX_TEXT_HIGHLIGHT, {.frame=(int)i, .x=(offset*i), .fade=diff_fade_in->attribute});
        } else {
            tex.draw_texture(DIFF_SORT::BOX, {.x=(offset*i), .fade=diff_fade_in->attribute});
            tex.draw_texture(DIFF_SORT::BOX_TEXT, {.frame=(int)i, .x=(offset*i), .fade=diff_fade_in->attribute});
        }
    }

    if (selected_box == -1) {
        tex.draw_texture(DIFF_SORT::BACK_OUTLINE, {.fade=box_flicker->attribute});
    } else {
        tex.draw_texture(DIFF_SORT::BOX_OUTLINE, {.x=(offset*selected_box), .fade=box_flicker->attribute});
    }

    for (size_t i = 0; i < num_boxes; i++) {
        if (i < 5) {
            tex.draw_texture(DIFF_SORT::BOX_DIFF, {.frame=(int)i, .x=(offset*i)});
        }
    }
    if (selected_box != -1 && selected_box != num_boxes - 1) {
        draw_statistics();
    }
}

void DiffSortSelect::draw_level_select() {
    tex.draw_texture(DIFF_SORT::BACKGROUND, {.scale=(float)bg_resize->attribute, .center=true});
    if (confirmation) {
        tex.draw_texture(DIFF_SORT::STAR_SELECT_PROMPT);
    } else {
        tex.draw_texture(DIFF_SORT::STAR_SELECT_TEXT, {.fade=diff_fade_in->attribute});
    }
    tex.draw_texture(DIFF_SORT::STAR_LIMIT, {.frame=selected_box, .fade=diff_fade_in->attribute});
    tex.draw_texture(DIFF_SORT::LEVEL_BOX, {.fade=diff_fade_in->attribute});
    tex.draw_texture(DIFF_SORT::DIFF, {.frame=selected_box, .fade=diff_fade_in->attribute});
    tex.draw_texture(DIFF_SORT::STAR_NUM, {.frame=selected_level, .fade=diff_fade_in->attribute});
    for (size_t i = 0; i < selected_level; i++) {
        tex.draw_texture(DIFF_SORT::STAR, {.x=(float)(i * tex.skin_config[SC::DIFF_SORT_STAR_SPACING].x), .fade=diff_fade_in->attribute});
    }

    if (confirmation) {
        TextureObject* texture = tex.textures[DIFF_SORT::LEVEL_BOX].get();
        ray::DrawRectangle(texture->x[0], texture->y[0], texture->x2[0], texture->y2[0], ray::Fade(ray::BLACK, 0.5));
        float y = -bounce_up_1->attribute + bounce_down_1->attribute - bounce_up_2->attribute + bounce_down_2->attribute;
        float offset = tex.skin_config[SC::DIFF_SORT_OFFSET_2].x;
        for (size_t i = 0; i < 3; i++) {
            if (i == confirm_index) {
                tex.draw_texture(DIFF_SORT::SMALL_BOX_HIGHLIGHT, {.x=(i*offset), .y=y});
                tex.draw_texture(DIFF_SORT::SMALL_BOX_TEXT_HIGHLIGHT, {.frame=(int)i, .x=(i*offset), .y=y});
                tex.draw_texture(DIFF_SORT::SMALL_BOX_OUTLINE, {.x=(i*offset), .y=y, .fade=box_flicker->attribute});
            } else {
                tex.draw_texture(DIFF_SORT::SMALL_BOX, {.x=(i*offset), .y=y});
                tex.draw_texture(DIFF_SORT::SMALL_BOX_TEXT, {.frame=(int)i, .x=(i*offset), .y=y});
            }
        }
    } else {
        tex.draw_texture(DIFF_SORT::PONGOS);
        if (selected_level != 1) {
            tex.draw_texture(DIFF_SORT::ARROW, {.x=(float)-blue_arrow_move->attribute, .fade=blue_arrow_fade->attribute, .index=0});
        }
        if (selected_level != limits[selected_box]) {
            tex.draw_texture(DIFF_SORT::ARROW, {.mirror=Mirror::HORIZONTAL, .x=(float)blue_arrow_move->attribute, .fade=blue_arrow_fade->attribute, .index=1});
        }
    }
    draw_statistics();
}

void DiffSortSelect::draw() {
    if (arcade) {
        // The whole window belongs to the skin; the cabinet dims nothing behind it.
        if (script && script->draw_sort_window(this)) return;
    }
    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::BLACK, 0.6));
    if (in_level_select) {
        draw_level_select();
    } else {
        draw_diff_select();
    }
}
