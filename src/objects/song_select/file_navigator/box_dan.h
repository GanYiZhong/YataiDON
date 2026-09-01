#pragma once

#include "box_base.h"
#include "../../../libs/global_data.h"
#include "../../game/exam_caption.h"

class DanBox : public BaseBox {
public:
    std::string dan_title;
    int dan_color = 0;
    int dan_rank = -1;   // ROUND 19: dan.json "rank_art"; -1 = none
    int dan_index = -1;  // ROUND 50: dan.json "dan_index" (nameplate chip 0..24); -1 = none
    bool gaiden = false; // ROUND 57: dan.json "gaiden" (Cabinet.GaidenDaniInfo semantics)
    std::vector<DanSongEntry> songs;
    std::vector<Exam> exams;
    int total_notes = 0;
    // ROUND 95 -- the localised (title, subtitle) per song, filled by the
    // DAN_SELECT course-scan worker (DanNavigator::make_box). When it is present
    // and the right length, `load_text()` uses it instead of running a fresh
    // SongParser over every chart of the course on the main thread. Empty for any
    // other construction path, which keeps the old parse as the fallback.
    std::vector<std::pair<std::string, std::string>> song_titles;

    // ROUND 89 -- the 合格条件 threshold captions, when the skin opts in with
    // dan_select_exam_border_text. Same cache the GAME_DAN / DAN_RESULT rows
    // use; a threshold only changes when the course or the language does.
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

private:
    void draw_exam_box();
    // ROUND 64 (r64-danselect-fidelity) -- the cabinet's 合格条件 GRID, used
    // when the skin declares `dan_exam_grid`. A skin that does not (PyTaikoGreen)
    // keeps draw_exam_box()'s stacked full-width bars, unchanged.
    void draw_exam_grid();
    void draw_digit_counter(const std::string& digits, float margin_x, float y, TexID digit_tex);
};
