#include "box_dan.h"
#include "../../../libs/song_parser.h"

DanBox::DanBox(const fs::path& path, const std::string& title, int color,
               const std::vector<DanSongEntry>& songs_in,
               const std::vector<Exam>& exams_in, int total_notes_in)
    : BaseBox(path, BoxDef{title, static_cast<TextureIndex>(color), GenreIndex::DAN, "", std::nullopt, std::nullopt})
    , dan_title(title), dan_color(color)
    , songs(songs_in), exams(exams_in), total_notes(total_notes_in)
{
    text_name = title;
}

void DanBox::load_text() {
    BaseBox::load_text();  // populates name (vertical) for closed state
    float base_font = tex.skin_config[SC::SONG_BOX_NAME].font_size;
    if (utf8_char_count(text_name) >= 30) base_font -= (int)(10 * tex.screen_scale);
    name = std::make_unique<OutlinedText>(text_name, (int)base_font, ray::WHITE, ray::BLACK, true);
    int font_size = tex.skin_config[SC::DAN_TITLE].font_size;
    hori_name = std::make_unique<OutlinedText>(dan_title, font_size, ray::WHITE, ray::BLACK, false);

    const std::string& lang = global_data.config->general.language;
    for (auto& entry : songs) {
        SongParser sp(entry.song_path);
        std::string title_str = sp.metadata.title.count(lang) ? sp.metadata.title.at(lang) : sp.metadata.title.at("en");
        std::string sub_str   = sp.metadata.subtitle.count(lang) ? sp.metadata.subtitle.at(lang) : "";

        int sub_font = tex.skin_config[SC::DAN_SUBTITLE].font_size;
        if (sub_str.size() >= 30)
            sub_font -= (int)(10 * tex.screen_scale);

        song_texts.push_back({
            std::make_unique<OutlinedText>(title_str, font_size, ray::WHITE, ray::BLACK, true),
            std::make_unique<OutlinedText>(sub_str,   sub_font,  ray::WHITE, ray::BLACK, true)
        });
    }
    text_loaded = true;
}

void DanBox::update(double current_ms) {
    BaseBox::update(current_ms);
    if (yellow_box.has_value() && yellow_box_opened && !yellow_box->is_diff_select)
        yellow_box->create_anim_2();
}

void DanBox::draw_closed() {}

void DanBox::draw_open() {}

void DanBox::draw_exam_box() {}
