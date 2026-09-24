#include "box_back.h"
#include "../../../libs/global_data.h"

BackBox::BackBox(const fs::path& path, const BoxDef& box_def) : BaseBox(path, box_def) {
    this->text_name = "BACK_BOX";
    load_textures();
}

void BackBox::load_textures() {
    BaseBox::load_textures();
    t_back_icon = tex.get_texture("box/back_icon");
    t_back_icon_highlight = tex.get_texture("box/back_icon_highlight");
    t_back_graphic = tex.get_texture("box/back_graphic");
}

void BackBox::load_text() {
    BaseBox::load_text();
    static constexpr ray::Color OUTLINE_COLOR = ray::Color(77, 39, 0, 255);
    const SkinInfo& cfg = tex.skin_config[SC::BOX_BACK_TEXT];
    const std::string& lang = global_data.config->general.language;
    auto it = cfg.text.find(lang);
    std::string label = it != cfg.text.end() ? it->second
                       : !cfg.text.empty()    ? cfg.text.begin()->second
                                               : "Back";
    back_text = std::make_unique<OutlinedText>(label, cfg.font_size, ray::WHITE, OUTLINE_COLOR, true);
    back_text_highlight = std::make_unique<OutlinedText>(label, cfg.font_size, ray::WHITE, ray::BLACK, true);
}

void BackBox::draw_closed() {
    BaseBox::draw_closed();
    tex.draw_texture(t_back_icon, {.x=box_x(), .y=box_y(), .fade=fade->attribute});
    if (back_text) {
        const SkinInfo& cfg = tex.skin_config[SC::BOX_BACK_TEXT];
        back_text->draw({
            .x = box_x() + cfg.x - (back_text->width / 2.0f),
            .y = box_y() + cfg.y,
            .fade = fade->attribute
        });
    }
}

void BackBox::draw_open() {
    float bx = box_x();
    float by = box_y();
    float mfade = std::min(fade->attribute, open_fade->attribute);
    tex.draw_texture(t_shadow_bottom_left,  {.x=bx, .y=by, .fade=mfade, .index=1});
    tex.draw_texture(t_shadow_bottom,       {.x=bx, .y=by, .fade=mfade, .index=1});
    tex.draw_texture(t_shadow_bottom_right, {.x=bx, .y=by, .fade=mfade, .index=1});
    tex.draw_texture(t_shadow_right,        {.x=bx, .y=by, .fade=mfade, .index=1});
    tex.draw_texture(t_shadow_top_right,    {.x=bx, .y=by, .fade=mfade, .index=1});
    if (yellow_box.has_value())
        yellow_box->draw(mfade, by);
    float x = bx + (yellow_box->right_out->attribute*0.85 - (yellow_box->right_out->start_position*0.85)) + yellow_box->right_out_2->attribute - yellow_box->right_out_2->start_position;
    tex.draw_texture(t_back_icon_highlight, {.x=x, .y=by, .fade=mfade});
    if (back_text_highlight) {
        const SkinInfo& cfg = tex.skin_config[SC::BOX_BACK_TEXT];
        back_text_highlight->draw({
            .x = x + cfg.x - (back_text_highlight->width / 2.0f),
            .y = by + cfg.y,
            .fade = mfade
        });
    }
    tex.draw_texture(t_back_graphic, {.y=by, .fade=mfade});
}
