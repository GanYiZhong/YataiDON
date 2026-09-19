#pragma once

#include "box_base.h"
#include "../../../libs/global_data.h"
#include "../../game/exam_caption.h"

class DanBox : public BaseBox {
public:
    std::string dan_title;
    int dan_color = 0;
    int dan_rank = -1;
    int dan_index = -1;
    bool gaiden = false;
    std::vector<DanSongEntry> songs;
    std::vector<Exam> exams;
    int total_notes = 0;
    std::vector<std::pair<std::string, std::string>> song_titles;

    ExamCaptionCache exam_captions;

    std::unique_ptr<OutlinedText> hori_name;
    std::vector<std::pair<std::unique_ptr<OutlinedText>, std::unique_ptr<OutlinedText>>> song_texts;

    DanBox(const fs::path& path, const std::string& title, int color,
           const std::vector<DanSongEntry>& songs, const std::vector<Exam>& exams,
           int total_notes);

    void load_text() override;
    void update(double current_ms) override;

protected:
    void draw_chip();
    void draw_closed() override;
    void draw_open() override;
    void draw_diff_select() override { draw_open(); }
    void load_textures() override;

private:
    void draw_exam_box();
    void draw_exam_grid();
    void draw_digit_counter(const std::string& digits, float margin_x, float y, TextureObject* digit_tex);

    // Fixed-path textures resolved once in the constructor (songs/exams never change
    // after construction) instead of calling tex.get_texture() every frame from draw().
    TextureObject* t_dan_folder = nullptr;
    TextureObject* t_genre_banner = nullptr;
    TextureObject* t_song_label = nullptr;
    TextureObject* t_difficulty = nullptr;
    TextureObject* t_difficulty_x = nullptr;
    TextureObject* t_difficulty_star = nullptr;
    TextureObject* t_difficulty_num = nullptr;
    TextureObject* t_total_notes_bg = nullptr;
    TextureObject* t_total_notes = nullptr;
    TextureObject* t_total_notes_counter = nullptr;
    TextureObject* t_rank_plate = nullptr;
    TextureObject* t_dan_rank_frame = nullptr;
    TextureObject* t_exam_box_bottom_right = nullptr;
    TextureObject* t_exam_box_bottom_left = nullptr;
    TextureObject* t_exam_box_top_right = nullptr;
    TextureObject* t_exam_box_top_left = nullptr;
    TextureObject* t_exam_box_bottom = nullptr;
    TextureObject* t_exam_box_right = nullptr;
    TextureObject* t_exam_box_left = nullptr;
    TextureObject* t_exam_box_top = nullptr;
    TextureObject* t_exam_box_center = nullptr;
    TextureObject* t_exam_header = nullptr;
    TextureObject* t_judge_box = nullptr;
    TextureObject* t_exam_percent = nullptr;
    TextureObject* t_judge_num = nullptr;
    TextureObject* t_exam_more = nullptr;
    TextureObject* t_exam_less = nullptr;
    TextureObject* t_exam_frame = nullptr;
    // exam.type ("gauge"/"combo"/.../"renda") -> icon, resolved through exam_icon_id()
    // (its "exam_roll" vs "exam_drumroll" skin-version fallback), used by draw_exam_box().
    std::unordered_map<std::string, TextureObject*> t_exam_icon_by_type;
};
