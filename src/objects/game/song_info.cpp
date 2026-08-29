#include "song_info.h"
#include "../../libs/global_data.h"

// ROUND 19: the arcade's own outline thicknesses live in the lumen
// DefineEditText records (`song_info.nulm`: 3.0 stage px on the 「N曲目」/「M曲」
// plate halves, 6.5 on the song title). This skin ships them as the optional
// `outline` key; a skin that never declared one keeps the historic hardcoded 5.
static float skin_outline(const SkinInfo& s) { return s.outline >= 0 ? s.outline : 5.0f; }

SongInfo::SongInfo(const std::string& song_name, const std::string& subtitle, bool show_subtitle, int genre, int song_num, int song_total)
    : song_name(song_name), genre(genre) {

    song_title = std::make_unique<OutlinedText>(song_name, tex.skin_config[SC::SONG_INFO].font_size, ray::WHITE, ray::BLACK, false,
                                                skin_outline(tex.skin_config[SC::SONG_INFO]));
    // Upstream subtitle support, drawn with this skin's own outline rule.
    if (show_subtitle && !subtitle.empty()) {
        song_subtitle = std::make_unique<OutlinedText>(subtitle, tex.skin_config[SC::SONG_INFO_SUBTITLE].font_size, ray::WHITE, ray::BLACK, false,
                                                       skin_outline(tex.skin_config[SC::SONG_INFO_SUBTITLE]));
    }
    // The gameplay plate's own outline, when the skin declared one on
    // `song_num_game`; otherwise the shared SC::SONG_NUM value.
    const SkinInfo* plate_cfg = tex.skin_entry("song_num_game");
    this->song_num = std::make_unique<SongNum>(
        song_num, plate_cfg ? plate_cfg->outline : -1.0f);
    // ROUND 17: the arcade's "M曲" half. Both the total and the skin key have to
    // be there -- a caller that knows no total, or a skin that never declared the
    // second field, gets exactly the old single-field plate.
    if (song_total > 0 && tex.skin_entry("song_num_max"))
        song_max = std::make_unique<SongNum>(song_total, "song_num_max");
    fade = (FadeAnimation*)tex.get_animation(3);
}

void SongInfo::update(double current_ms) {
    fade->update(current_ms);
}

void SongInfo::draw() {
    float text_x = tex.skin_config[SC::SONG_INFO].x;
    float text_y = tex.skin_config[SC::SONG_INFO].y - song_title->height / 2.0f;

    // Optional skin key `song_info_center {x, width}`: a title that fits in
    // `width` is centred on `x` instead of right-aligned at song_info.x, which
    // is the arcade rule (short titles centred in a 400 px box at 1734, long
    // ones right-aligned at 1871.5). Same shape as the song_info_result width
    // key result.cpp already honours. Without the key nothing changes.
    float title_x = text_x - song_title->width;
    if (const SkinInfo* c = tex.skin_entry("song_info_center")) {
        if (c->width > 0 && song_title->width <= c->width)
            title_x = c->x - song_title->width / 2.0f;
    }

    // Optional skin key `song_num_game {x, y, width}`: the title stays put and
    // the song counter cross-fades with the genre plate in the plate slot,
    // centred on (x, y) (arcade layout). Without the key the counter swaps
    // with the title as before.
    if (const SkinInfo* plate = tex.skin_entry("song_num_game")) {
        song_title->draw({.x=title_x, .y=text_y, .fade=1.0});
        if (genre < 9) {
            tex.draw_texture(SONG_INFO::GENRE, {.frame = genre, .fade = 1 - fade->attribute,});
        }
        if (tex.has_texture("song_info/song_num_plate")) {
            tex.draw_texture(tex.get_enum("song_info/song_num_plate"), {.fade = fade->attribute});
        }
        song_num->draw(plate->x - song_num->width / 2.0f, plate->y - song_num->height / 2.0f, fade->attribute);
        // The second half sits on the same plate at its own centre; the arcade's
        // is x 1759..1869 against the count half's 1594..1754 (song_info.nulm).
        if (song_max) {
            if (const SkinInfo* m = tex.skin_entry("song_num_max_game"))
                song_max->draw(m->x - song_max->width / 2.0f,
                               m->y - song_max->height / 2.0f, fade->attribute);
        }
        return;
    }

    song_num->draw(text_x - song_num->width, text_y, fade->attribute);

    song_title->draw({.x=title_x, .y=text_y, .fade=1 - fade->attribute});

    if (song_subtitle) {
        float sub_y = tex.skin_config[SC::SONG_INFO_SUBTITLE].y - song_subtitle->height / 2.0f;
        song_subtitle->draw({.x=text_x - song_subtitle->width, .y=sub_y, .fade=1 - fade->attribute});
    }

    if (genre < 9) {
        float genre_y_offset = song_subtitle ? song_subtitle->height : 0;
        tex.draw_texture(SONG_INFO::GENRE, {.frame = genre, .y = genre_y_offset, .fade = 1 - fade->attribute,});
    }
}

SongNum::SongNum(int song_num, float outline_override) {
    std::string song_format = tex.skin_config[SC::SONG_NUM].text[global_data.config->general.language];
    size_t pos = song_format.find("{0}");
    if (pos != std::string::npos) {
        song_format.replace(pos, 3, std::to_string(song_num));
    }
    ray::Color outline_color;
    if (global_data.config->general.song_limit > 0 && global_data.config->general.song_limit == song_num) {
        outline_color = ray::RED;
    } else {
        outline_color = ray::BLACK;
    }
    text = std::make_unique<OutlinedText>(song_format, tex.skin_config[SC::SONG_NUM].font_size, ray::WHITE, outline_color, false,
                                          outline_override >= 0 ? outline_override
                                                                : skin_outline(tex.skin_config[SC::SONG_NUM]));
    width = text->width;
    height = text->height;
}

SongNum::SongNum(int value, const std::string& config_key) {
    const SkinInfo* cfg = tex.skin_entry(config_key);
    if (!cfg) return;
    std::string fmt;
    auto it = cfg->text.find(global_data.config->general.language);
    if (it != cfg->text.end()) fmt = it->second;
    else if (!cfg->text.empty()) fmt = cfg->text.begin()->second;
    else fmt = "{0}";
    size_t pos = fmt.find("{0}");
    if (pos != std::string::npos) fmt.replace(pos, 3, std::to_string(value));
    text = std::make_unique<OutlinedText>(fmt, cfg->font_size, ray::WHITE, ray::BLACK, false, skin_outline(*cfg));
    width = text->width;
    height = text->height;
}

void SongNum::draw(float x, float y, float fade) {
    if (!text) return;
    text->draw({.x=x, .y=y, .fade = fade});
}
