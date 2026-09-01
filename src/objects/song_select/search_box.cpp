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

    TextureObject* background = tex.textures[DIFF_SORT::BACKGROUND].get();
    float fade = diff_fade_in->attribute;

    // ROUND 82 (r82-songselect-overflow-and-searchbox): put the board where the
    // cabinet puts it - the STAGE CENTRE - instead of where the plain-C++
    // `center=true` path happens to land it.
    //
    // `draw_texture(..., center=true)` (src/libs/texture.cpp:698-709) computes
    // dest.x = tex_obj->x[0] + width/2 - width*scale/2, so the board's own
    // centre ends up at (x[0] + width/2, y[0] + height/2) - for this skin's
    // 1320x760 `diff_sort/background.png` with texture.json x/y = 0/0 that is
    // (660, 380), NOT screen centre. Measured live this round on the untouched
    // build (scratchpad/r82/measure.py on shots/before_20_search.png): the
    // board's pink body sits at x:[51,1268] y:[58,701], centre (659.5, 379.5) -
    // matching ROUND 39's own "(655, 379.5)" and ROUND 37's "(659.5, 379.5)".
    //
    // The same board texture drawn by the arcade sort window
    // (`SongSelect:draw_sort_window`, Scripts/song_select/song_select.lua:2256:
    // `draw_centered(self._id_sw_board, 960, 540, SW_BOARD_W, SW_BOARD_H, ...)`,
    // SW_BOARD_W/H = 1320,760) is centred on the stage root (960, 540) = screen
    // centre. That is the cabinet-accurate placement, so SearchBox now uses it
    // too rather than carrying a hand-tuned offset: shift the centred draw by
    // (screen centre - the fallback centre) so it works at any resolution and
    // for any texture.json base offset.
    const float screen_cx = tex.screen_width  / 2.0f;
    const float screen_cy = tex.screen_height / 2.0f;
    const float board_dx = screen_cx - ((float)background->width  / 2 + background->x[0]);
    const float board_dy = screen_cy - ((float)background->height / 2 + background->y[0]);
    tex.draw_texture(DIFF_SORT::BACKGROUND, {.scale=(float)bg_resize->attribute, .center=true,
                                             .x=board_dx, .y=board_dy});

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
    // correction without touching the shared fallback formula itself.
    // ROUND 82 SUPERSEDES the last sentence of that: the board draw above is no
    // longer the un-nudged fallback placement - it is now the cabinet's own
    // stage-centre placement, for every skin reaching this file. `search_box.x`
    // /`.y` stays what ROUND 39 made it, a BOARD-RELATIVE offset, and is now
    // applied to the moved board's centre.
    float text_box_width  = tex.skin_config[SC::SEARCH_BOX].width;
    float text_box_height = tex.skin_config[SC::SEARCH_BOX].height;
    float offset_x = tex.skin_config[SC::SEARCH_BOX].x;
    float offset_y = tex.skin_config[SC::SEARCH_BOX].y;
    // ROUND 82: the pill is still positioned BOARD-RELATIVE (ROUND 39's rule -
    // never mix an absolute coordinate read off one rendering into the other's
    // offset formula), so it is now taken from the board's NEW centre. The
    // stored offset is unchanged: `search_box: {x:0, y:-130}` puts row 1 at
    // (960, 410), which is exactly SW_ROW_Y[1] = 410, the cabinet's own first
    // sort-window row - i.e. moving the board carries the pill onto the arcade
    // row for free, which is the point of keeping the offset board-relative.
    float x = screen_cx - text_box_width / 2 + offset_x;
    float y = screen_cy - text_box_height / 2 + offset_y;

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
