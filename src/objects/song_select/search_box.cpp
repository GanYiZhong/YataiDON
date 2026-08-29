#include "search_box.h"
#include "../../libs/text.h"

SearchBox::SearchBox() {
    current_search = "";
    bg_resize = (TextureChangeAnimation*)tex.get_animation(19);
    diff_fade_in = (FadeAnimation*)tex.get_animation(20);
    bg_resize->start();
    diff_fade_in->start();
}

void SearchBox::update(double current_ms) {
    bg_resize->update(current_ms);
    diff_fade_in->update(current_ms);
}

void SearchBox::draw() {
    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::BLACK, 0.6));
    tex.draw_texture(DIFF_SORT::BACKGROUND, {.scale=(float)bg_resize->attribute, .center=true});

    TextureObject* background = tex.textures[DIFF_SORT::BACKGROUND].get();
    float fade = diff_fade_in->attribute;

    float text_box_width  = tex.skin_config[SC::SEARCH_BOX].width;
    float text_box_height = tex.skin_config[SC::SEARCH_BOX].height;
    float offset_x = tex.skin_config[SC::SEARCH_BOX].x;
    float offset_y = tex.skin_config[SC::SEARCH_BOX].y;
    float x = (float)background->width / 2 + background->x[0] - text_box_width / 2 + offset_x;
    float y = (float)background->height / 2 + background->y[0] - text_box_height / 2 + offset_y;

    const ray::Color PILL_OUTLINE = {119, 26, 45, 255};
    ray::Rectangle text_box = {x, y, text_box_width, text_box_height};
    float roundness = text_box_height > 0 ? 40.0f / text_box_height : 0.0f;
    ray::DrawRectangleRounded(text_box, roundness, 16, ray::Fade(ray::WHITE, fade));
    ray::DrawRectangleRoundedLinesEx(text_box, roundness, 16, 3.0f, ray::Fade(PILL_OUTLINE, fade));

    int search_font_size = (int)(30 * tex.screen_scale);
    ray::Font font = font_manager.get_font(current_search, search_font_size);

    ray::Vector2 text_size = ray::MeasureTextEx(font, current_search.c_str(), (float)search_font_size, 1);
    ray::DrawTextEx(font, current_search.c_str(),
        ray::Vector2{x + text_box_width / 2 - text_size.x / 2, y + text_box_height / 2 - text_size.y / 2},
        (float)search_font_size, 1, ray::BLACK);
}
