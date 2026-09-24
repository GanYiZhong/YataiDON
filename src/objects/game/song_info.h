#pragma once

#include "../../libs/text.h"

class SongNum {
private:
    std::unique_ptr<OutlinedText> text;
public:
    float width;
    float height;
    SongNum() = default;
    SongNum(int song_num, float outline_override = -1.0f);
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
    std::unique_ptr<OutlinedText> genre_text;
    std::unique_ptr<SongNum> song_num;
    std::unique_ptr<SongNum> song_max;

    TextureObject* t_genre = nullptr;
    TextureObject* t_song_num_plate = nullptr;
    ray::Shader genre_shader{};
    bool genre_shader_loaded = false;

public:
    SongInfo() = default;
    SongInfo(const std::string& song_name, const std::string& subtitle, bool show_subtitle, int genre, int song_num, int song_total = 0, const std::string& genre_label = "");

    void update(double current_ms);
    void draw();
};
