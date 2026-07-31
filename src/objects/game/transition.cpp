#include "transition.h"

Transition::Transition(const std::string& title, const std::string& subtitle, bool is_second) :
    is_second(is_second) {
    rainbow_up = (MoveAnimation*)global_tex.get_animation(0);
    mini_up = (MoveAnimation*)global_tex.get_animation(1);
    chara_down = (MoveAnimation*)global_tex.get_animation(2);
    song_info_fade = (FadeAnimation*)global_tex.get_animation(3);
    song_info_fade_out = (FadeAnimation*)global_tex.get_animation(4);

    this->title = std::make_unique<OutlinedText>(title, global_tex.skin_config[SC::TRANSITION_TITLE].font_size, ray::WHITE, ray::BLACK, false, 5);
    this->subtitle = std::make_unique<OutlinedText>(subtitle, global_tex.skin_config[SC::TRANSITION_SUBTITLE].font_size, ray::WHITE, ray::BLACK, false, 5);
}

Transition::~Transition() {
    if (loading_graphic.has_value()) {
        ray::UnloadTexture(loading_graphic.value());
    }
}

void Transition::add_loading_graphic(const std::string& path) {
    loading_graphic.emplace(ray::LoadTexture(path.c_str()));
    ray::GenTextureMipmaps(&loading_graphic.value());
    ray::SetTextureFilter(loading_graphic.value(), ray::TEXTURE_FILTER_TRILINEAR);
}

void Transition::start() {
    rainbow_up->start();
    mini_up->start();
    chara_down->start();
    song_info_fade->start();
    song_info_fade_out->start();
}

void Transition::update(double current_ms) {
    rainbow_up->update(current_ms);
    chara_down->update(current_ms);
    mini_up->update(current_ms);
    song_info_fade->update(current_ms);
    song_info_fade_out->update(current_ms);
}

bool Transition::is_finished() {
    return song_info_fade->is_finished;
}

void Transition::draw_song_info() {
}

void Transition::draw_default(float total_offset) {
}

void Transition::draw() {
}
