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

    // ROUND 37 (r37-search-ui): this file draws the shared sort_select board
    // (DIFF_SORT::BACKGROUND) through the plain C++ fallback path
    // (draw_texture with center=true), same as DiffSortSelect::draw_level_select()
    // would - EXCEPT this skin has a real arcade sort-window Lua script
    // (script->has_sort_window(), wired in ROUND 19), so DiffSortSelect::draw()
    // never actually reaches draw_level_select() here; it takes the
    // `script->draw_sort_window(this)` branch instead, which lays the same
    // board out at the cabinet-accurate on-screen position/scale. SearchBox
    // has no Lua counterpart (search is keyboard-only, PC/skin convenience,
    // no cabinet UI to decode - see ENGINE_BINDINGS.md), so it only ever had
    // the plain-C++ placement, and that placement was never the arcade one:
    // measured in game (own exe copy, HWND-driven, PIL pixel scan) the
    // un-nudged formula below centres at (659.5, 379.5) while the arcade sort
    // window's own first row ("Select by difficulty") sits at (960, 409.5) -
    // a ~300px/~30px mismatch, which is what read as the box floating
    // crookedly across two of the board's content rows. `search_box.x`/`.y`
    // in skin_config.json (previously unread by this file) supplies that
    // correction without touching the shared fallback formula itself, so any
    // other skin/context using this same code path keeps today's placement
    // until it opts in.
    float text_box_width  = tex.skin_config[SC::SEARCH_BOX].width;
    float text_box_height = tex.skin_config[SC::SEARCH_BOX].height;
    float offset_x = tex.skin_config[SC::SEARCH_BOX].x;
    float offset_y = tex.skin_config[SC::SEARCH_BOX].y;
    float x = (float)background->width / 2 + background->x[0] - text_box_width / 2 + offset_x;
    float y = (float)background->height / 2 + background->y[0] - text_box_height / 2 + offset_y;

    // Restyled to match this board's own established convention (see the
    // "Select by difficulty" rows drawn by DiffSortSelect on the identical
    // DIFF_SORT::BACKGROUND board): a rounded white pill with the board's own
    // dark-red outline (measured in game: 119, 26, 45 - MAPPING.md "root list
    // | search / sort folder title") instead of raylib's flat default
    // LIGHTGRAY/DARKGRAY, which matched nothing else drawn on this board.
    const ray::Color PILL_OUTLINE = {119, 26, 45, 255};
    ray::Rectangle text_box = {x, y, text_box_width, text_box_height};
    float roundness = text_box_height > 0 ? 40.0f / text_box_height : 0.0f;
    ray::DrawRectangleRounded(text_box, roundness, 16, ray::Fade(ray::WHITE, fade));
    ray::DrawRectangleRoundedLinesEx(text_box, roundness, 16, 3.0f, ray::Fade(PILL_OUTLINE, fade));

    // Re-acquire every frame: the atlas is per-size and only carries the
    // codepoints requested at that size, so the typed string must be passed in.
    int search_font_size = (int)(30 * tex.screen_scale);
    ray::Font font = font_manager.get_font(current_search, search_font_size);

    ray::Vector2 text_size = ray::MeasureTextEx(font, current_search.c_str(), (float)search_font_size, 1);
    ray::DrawTextEx(font, current_search.c_str(),
        ray::Vector2{x + text_box_width / 2 - text_size.x / 2, y + text_box_height / 2 - text_size.y / 2},
        (float)search_font_size, 1, ray::BLACK);
}
