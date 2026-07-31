#include "box_song_osu.h"

SongBoxOsu::SongBoxOsu(const fs::path& path, const BoxDef& box_def, SongParser parser)
    : SongBox(path, box_def, parser)
{
    this->parser = parser;
    parser.get_metadata();
    text_name = parser.get_difficulty_name();

    const std::string& lang = global_data.config->general.language;
    auto& subtitles = parser.metadata.subtitle;
    text_subtitle = subtitles.count(lang) ? subtitles.at(lang) : subtitles.count("en") ? subtitles.at("en") : subtitles.empty() ? "" : subtitles.begin()->second;

    is_favorite = false;
    diff_fade_in = (FadeAnimation*)tex.get_animation(12);
    refresh_scores();
}

void SongBoxOsu::draw_closed() {}

void SongBoxOsu::draw_open() {}
