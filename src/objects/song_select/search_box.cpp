#include "search_box.h"
#include "../../libs/text.h"

SearchBox::SearchBox() {
    current_search = "";
    bg_resize = (TextureChangeAnimation*)tex.get_animation(19);
    diff_fade_in = (FadeAnimation*)tex.get_animation(20);
    bg_resize->start();
    diff_fade_in->start();
    font = font_manager.get_font(current_search, 30);
}

void SearchBox::update(double current_ms) {
    bg_resize->update(current_ms);
    diff_fade_in->update(current_ms);
}

void SearchBox::draw() {}
