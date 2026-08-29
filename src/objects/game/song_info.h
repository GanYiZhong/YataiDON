#pragma once

#include "../../libs/text.h"

class SongNum {
private:
    std::unique_ptr<OutlinedText> text;
public:
    float width;
    float height;
    SongNum() = default;
    // ROUND 19: `outline_override` (>=0) is the arcade outline radius for the
    // GAMEPLAY plate, which differs from the song-select plate's
    // (song_info.nulm text_song_count border 3.0 vs
    //  common_song_select_number_of_songs.nulm ET 4 border 4.5) while both
    // still share the single SC::SONG_NUM entry for text/font_size.
    SongNum(int song_num, float outline_override = -1.0f);
    // ROUND 17 -- the arcade pill's SECOND half. `song_info.nulm`
    // (enso_normal/enso/information) draws TWO right-aligned 24 px fields on one
    // 288x32 plate: `text_song_count` ("N曲目", pen 1616,104) on the white left
    // half and `text_song_max` ("M曲", pen 1761,104) on the blue right half. We
    // only ever drew the first, which is why our plate's blue half was empty on
    // every screen and why a 段位道場 run could not say "of 3".
    //
    // Read by NAME (`tex.skin_entry`) rather than through a new SC enum member:
    // the SC enum is generated from the PARENT skin's skin_config.json
    // (cmake/codegen.cmake), so a new member would mean editing PyTaikoGreen.
    SongNum(int value, const std::string& config_key);

    void draw(float x, float y, float fade);
};

class SongInfo {
private:
    std::string song_name;
    int genre;
    FadeAnimation* fade;
    std::unique_ptr<OutlinedText> song_title;
    std::unique_ptr<OutlinedText> song_subtitle;
    std::unique_ptr<SongNum> song_num;
    // ROUND 17: built only when the caller passes a total AND the skin declares
    // `song_num_max` / `song_num_max_game`. Absent -> nothing drawn, i.e. every
    // skin that predates this is bit-for-bit unchanged.
    std::unique_ptr<SongNum> song_max;

public:
    SongInfo() = default;
    SongInfo(const std::string& song_name, const std::string& subtitle, bool show_subtitle, int genre, int song_num, int song_total = 0);

    void update(double current_ms);
    void draw();
};
