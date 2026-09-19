#include "box_back.h"

BackBox::BackBox(const fs::path& path, const BoxDef& box_def) : BaseBox(path, box_def) {
    this->text_name = "BACK_BOX";
    load_textures();
}

void BackBox::load_textures() {
    BaseBox::load_textures();
    t_back_text = tex.get_texture("box/back_text");
    t_back_text_highlight = tex.get_texture("box/back_text_highlight");
    t_back_graphic = tex.get_texture("box/back_graphic");
}

void BackBox::draw_closed() {
    BaseBox::draw_closed();
    tex.draw_texture(t_back_text, {.x=box_x(), .y=box_y(), .fade=fade->attribute});
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
    tex.draw_texture(t_back_text_highlight, {.x=x, .y=by, .fade=mfade});
    tex.draw_texture(t_back_graphic, {.y=by, .fade=mfade});
}
