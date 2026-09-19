#include "box_song_osu.h"

SongBoxOsu::SongBoxOsu(const fs::path& path, const BoxDef& box_def, SongParser parser)
    : SongBox(path, box_def, std::move(parser))
{
    // The base constructor already parsed the metadata and owns the parser -
    // read from the member instead of keeping two more copies alive.
    text_name = this->parser.get_difficulty_name();

    const std::string& lang = global_data.config->general.language;
    auto& subtitles = this->parser.metadata.subtitle;
    text_subtitle = subtitles.count(lang) ? subtitles.at(lang) : subtitles.count("en") ? subtitles.at("en") : subtitles.empty() ? "" : subtitles.begin()->second;

    is_favorite = false;
    diff_fade_in = (FadeAnimation*)tex.get_animation(12);
    refresh_scores();

    load_textures();
}

void SongBoxOsu::load_textures() {
    SongBox::load_textures();
    t_favorite_1p = tex.get_texture("yellow_box/favorite_1p");
    t_favorite_2p = tex.get_texture("yellow_box/favorite_2p");
}

void SongBoxOsu::draw_closed() {
    BaseBox::draw_closed();

    if (!text_loaded) return;
    float bx = box_x();
    float by = box_y();
    float name_x = bx + tex.skin_config[SC::SONG_BOX_NAME].x - (int)(this->name->width / 2);
    float name_y = tex.skin_config[SC::SONG_BOX_NAME].y + by;
    float name_h = std::min((float)this->name->height, tex.skin_config[SC::SONG_BOX_NAME].height);
    this->name->draw({.x = name_x, .y = name_y, .y2 = name_h - this->name->height, .fade=fade->attribute});

    draw_box_crown(bx, by, fade->attribute);
}

void SongBoxOsu::draw_open() {
    float bx = box_x();
    float by = box_y();
    tex.draw_texture(t_shadow_bottom_left,  {.x=bx, .y=by, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_bottom,       {.x=bx, .y=by, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_bottom_right, {.x=bx, .y=by, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_right,        {.x=bx, .y=by, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_top_right,    {.x=bx, .y=by, .fade=open_fade->attribute, .index=1});
    if (yellow_box.has_value())
        yellow_box->draw(1.0f, by);

    float offset = tex.skin_config[SC::YB_DIFF_OFFSET].x;

    for (const auto& [diff, course] : parser.metadata.course_data) {
        if (Difficulty(diff) >= Difficulty::URA) continue;
        draw_diff_outline(diff*offset, 0.0f, std::min((float)open_fade->attribute, 0.25f));
        draw_diff_crown(diff, diff*offset, 0.0f, open_fade->attribute);
    }

    if (global_data.config->general.display_bpm)
        bpm_text->draw({.x = tex.skin_config[SC::SONG_BOX_BPM].x, .y = tex.skin_config[SC::SONG_BOX_BPM].y, .fade=open_fade->attribute});

    if (is_favorite)
        tex.draw_texture(global_data.player_num == PlayerNum::P2 ? t_favorite_2p : t_favorite_1p, {.fade=open_fade->attribute});

    for (int i = 0; i < 4; i++) {
        tex.draw_texture(t_difficulty_bar,        {.frame=i, .x=i*offset, .fade=open_fade->attribute});
        if (!parser.metadata.course_data.count(i))
            tex.draw_texture(t_difficulty_bar_shadow, {.frame=i, .x=i*offset, .fade=std::min((float)open_fade->attribute, 0.25f)});
    }

    float offset_y = tex.skin_config[SC::YB_DIFF_OFFSET].y;
    for (const auto& [diff, course] : parser.metadata.course_data) {
        if (Difficulty(diff) >= Difficulty::URA) continue;
        for (int j = 0; j < course.level; j++)
            tex.draw_texture(t_star, {.x=diff*offset, .y=j*offset_y, .fade=open_fade->attribute});
        if (course.is_branching && ((int)(get_current_ms() / 1000)) % 2 == 0)
            tex.draw_texture(t_branch_indicator, {.x=diff*offset, .fade=open_fade->attribute});
    }
    draw_text();
}
