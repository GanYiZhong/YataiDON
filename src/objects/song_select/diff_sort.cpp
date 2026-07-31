#include "diff_sort.h"
#include "../../libs/audio.h"

DiffSortSelect::DiffSortSelect(Statistics statistics, int prev_diff, int prev_level) : prev_diff(prev_diff), prev_level(prev_level), statistics(statistics) {
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

    bg_resize->start();
    diff_fade_in->start();
    box_flicker->start();

    for (const auto& [course, levels] : statistics) {
        std::array<int, 3> sums = {0, 0, 0};
        for (const auto& [level, stats] : levels) {
            sums[0] += stats.total;
            sums[1] += stats.clears;
            sums[2] += stats.full_combos;
        }
        diff_sort_sum_stat[course] = sums;
    }

    audio.play_sound("voice_diff_sort_enter", VolumePreset::VOICE);

}

void DiffSortSelect::update(double current_ms) {
    bg_resize->update(current_ms);
    diff_fade_in->update(current_ms);
    box_flicker->update(current_ms);
    bounce_up_1->update(current_ms);
    bounce_down_1->update(current_ms);
    bounce_up_2->update(current_ms);
    bounce_down_2->update(current_ms);
}

std::optional<std::pair<int, int>> DiffSortSelect::input_select() {
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
    if (confirmation) {
        confirm_index = std::max(confirm_index - 1, 0);
    } else if (in_level_select) {
        selected_level = std::max(selected_level - 1, 1);
    } else {
        selected_box = std::max(selected_box - 1, -1);
    }
}

void DiffSortSelect::input_right() {
    if (confirmation) {
        confirm_index = std::min(confirm_index + 1, 2);
    } else if (in_level_select) {
        selected_level = std::min(selected_level + 1, limits[selected_box]);
    } else {
        selected_box = std::min(selected_box + 1, num_boxes - 1);
    }
}

void DiffSortSelect::draw_statistics() {}

void DiffSortSelect::draw_diff_select() {}

void DiffSortSelect::draw_level_select() {}

void DiffSortSelect::draw() {}
