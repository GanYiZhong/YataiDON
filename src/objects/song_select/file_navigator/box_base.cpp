#include "box_base.h"
#include "../../../libs/text.h"
#include "color_utils.h"
#include <string_view>

BaseBox::BaseBox(const fs::path& path, const BoxDef& box_def)
    : path(path), texture_index(box_def.texture_index),
      genre_index(box_def.genre_index),
      genre_label(box_def.genre_label),
      collection(box_def.collection),
      explanation(box_def.explanation)
{
    BoxColors colors = resolve_box_colors(box_def.box_color, box_def.back_color, box_def.fore_color);
    this->back_color = colors.box;
    this->fore_color = colors.outline;
    this->text_color = colors.text;

    position = std::numeric_limits<float>::infinity();
    target_position = std::numeric_limits<float>::infinity();

    open_anim = new MoveAnimation(233, 150.0f * tex.screen_scale, false, false, 0, 133);
    open_fade = new FadeAnimation(200, 0.0f, false, false, 1.0f, 133);
    move = std::make_unique<MoveAnimation>(133, 0, false, false, 0, 0.0, std::nullopt, std::nullopt, EaseType::Cubic);
    move->start();

    load_textures();

    fade_in(100);
}

BaseBox::~BaseBox() {
    if (shader_loaded)
        ray::UnloadShader(shader);
}

void BaseBox::preregister_text() {
    float font_size = tex.skin_config[SC::SONG_BOX_NAME].font_size;
    if (utf8_char_count(text_name) >= 30)
        font_size -= (int)(10 * tex.screen_scale);
    font_manager.register_text(text_name, (int)font_size);
}

void BaseBox::load_text() {
    float font_size = tex.skin_config[SC::SONG_BOX_NAME].font_size;
    if (utf8_char_count(text_name) >= 30)
        font_size -= (int)(10 * tex.screen_scale);
    name = make_unique<OutlinedText>(text_name, font_size, text_color, fore_color.value(), true);

    if (back_color.has_value()) {
        shader = load_shader("shader/dummy.vs", "shader/colortransform.fs");
        shader_loaded = true;
        const auto& target_rgb = back_color.value();
        float src[3] = { 142 / 255.0f, 212 / 255.0f, 30 / 255.0f };
        float tgt[3] = { target_rgb.r / 255.0f, target_rgb.g / 255.0f, target_rgb.b / 255.0f };
        int source_loc = ray::GetShaderLocation(shader, "sourceColor");
        int target_loc = ray::GetShaderLocation(shader, "targetColor");
        ray::SetShaderValue(shader, source_loc, src, ray::SHADER_UNIFORM_VEC3);
        ray::SetShaderValue(shader, target_loc, tgt, ray::SHADER_UNIFORM_VEC3);
    }

    text_loaded = true;
}

void BaseBox::reset() {
    yellow_box.reset();
    yellow_box_opened = false;
    open_anim->reset();
    open_fade->reset();
    load_textures();
}

void BaseBox::load_textures() {
    t_shadow_bottom_left  = tex.get_texture("yellow_box/shadow_bottom_left");
    t_shadow_bottom       = tex.get_texture("yellow_box/shadow_bottom");
    t_shadow_bottom_right = tex.get_texture("yellow_box/shadow_bottom_right");
    t_shadow_right        = tex.get_texture("yellow_box/shadow_right");
    t_shadow_top_right    = tex.get_texture("yellow_box/shadow_top_right");
    t_folder_texture_left  = tex.get_texture("box/folder_texture_left");
    t_folder_texture       = tex.get_texture("box/folder_texture");
    t_folder_texture_right = tex.get_texture("box/folder_texture_right");
    t_genre_overlay = tex.get_texture("box/genre_overlay");
    t_diff_overlay  = tex.get_texture("box/diff_overlay");
}

void BaseBox::expand_box() {
    yellow_box.emplace();
    yellow_box_opened = false;
    open_anim->start();
    bar_open_started_at = get_current_ms();
}

void BaseBox::close_box() {
    yellow_box.reset();
    yellow_box_opened = false;
}

void BaseBox::enter_box() {
    yellow_box->create_anim_2();
}

void BaseBox::exit_box() {
    yellow_box.reset();
    yellow_box.emplace();
    yellow_box->create_anim();
    open_fade->start();
}

void BaseBox::set_position(float target_position) {
    position = target_position;
    this->target_position = position;
}

void BaseBox::move_box(float target_position, float duration) {
    this->target_position = target_position;
    float delta = target_position - position;
    move_delta = delta;
    move = std::make_unique<MoveAnimation>(duration, delta, false, false, 0, 0.0, std::nullopt, std::nullopt, EaseType::Cubic);
    move->start();
}

void BaseBox::fade_in(float delay) {
    fade = new FadeAnimation(266, 0.0f, false, false, 1.0f, delay);
    fade->start();
}

void BaseBox::fade_out() {
    fade = new FadeAnimation(166);
    fade->start();
}

void BaseBox::update(double current_time) {
    open_anim->update(current_time);
    open_fade->update(current_time);
    fade->update(current_time);
    float prev_position = move->attribute;
    move->update(current_time);
    if (!move->is_finished) {
        position += move->attribute - prev_position;
        if (move_delta != 0.0f && cross_lead != 0.0f) {
            float p = (float)(move->attribute / move_delta);
            if (p < 0.0f) p = 0.0f; else if (p > 1.0f) p = 1.0f;
            cross_pos = cross_target + cross_lead * (1.0f - p);
        }
    } else {
        position = target_position;
        cross_pos = cross_target;
        cross_lead = 0.0f;
        if (yellow_box.has_value() && !yellow_box_opened) {
            yellow_box->create_anim();
            yellow_box_opened = true;
            open_fade->start();
        }
    }
    if (yellow_box.has_value()) {
        yellow_box->update(current_time);
        left_bound = position + yellow_box->left_distance;
        right_bound = yellow_box->right_distance;
    } else {
        left_bound = position;
        right_bound = position + (float)(t_folder_texture_left->width) + (float)(t_folder_texture_right->width) + (tex.skin_config[SC::SONG_BOX_BG].width);
    }
}

void BaseBox::draw_closed() {
    float bx = box_x();
    float by = box_y();

    tex.draw_texture(t_shadow_bottom_left,  {.x=bx, .y=by, .fade=fade->attribute, .index=0});
    tex.draw_texture(t_shadow_bottom,       {.x=bx, .y=by, .fade=fade->attribute, .index=0});
    tex.draw_texture(t_shadow_bottom_right, {.x=bx, .y=by, .fade=fade->attribute, .index=0});
    tex.draw_texture(t_shadow_right,        {.x=bx, .y=by, .fade=fade->attribute, .index=0});
    tex.draw_texture(t_shadow_top_right,    {.x=bx, .y=by, .fade=fade->attribute, .index=0});

    if (shader_loaded && texture_index == TextureIndex::NONE)
        ray::BeginShaderMode(shader);

    tex.draw_texture(t_folder_texture_left,  {.frame=(int)texture_index, .x=bx, .y=by, .fade=fade->attribute});
    tex.draw_texture(t_folder_texture,       {.frame=(int)texture_index, .x=bx, .y=by, .x2=tex.skin_config[SC::SONG_BOX_BG].width, .fade=fade->attribute});
    tex.draw_texture(t_folder_texture_right, {.frame=(int)texture_index, .x=bx, .y=by, .fade=fade->attribute});

    if (shader_loaded && texture_index == TextureIndex::NONE)
        ray::EndShaderMode();

    if (texture_index == TextureIndex::DEFAULT)
        tex.draw_texture(t_genre_overlay, {.x=bx, .y=by, .fade=fade->attribute});
    if (genre_index == GenreIndex::DIFFICULTY)
        tex.draw_texture(t_diff_overlay,  {.x=bx, .y=by, .fade=fade->attribute});
}

void BaseBox::draw_open() {
    if (yellow_box.has_value()) {
        yellow_box->draw(1.0f, box_y());
    }
}

void BaseBox::draw_diff_select() {
    if (yellow_box.has_value())
        yellow_box->draw();
}

void BaseBox::draw()
{
    std::string_view state = draw_state();
    if (state == "diff_select") {
        draw_diff_select();
    } else if (state == "open") {
        draw_open();
    } else {
        draw_closed();
    }
}
