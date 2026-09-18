#include "gogo_time.h"
#include "../../libs/texture.h"
#include <algorithm>

GogoTime::GogoTime() {
    int resize_anim = 24;
    if (!tex.has_animation(resize_anim))
        throw std::runtime_error("gogo time: animation " + std::to_string(resize_anim) + " is missing");
    fire_resize = dynamic_cast<TextureResizeAnimation*>(tex.get_animation(resize_anim, true));
    if (!fire_resize)
        throw std::runtime_error("gogo time: animation 24 is not a resize animation");

    int change_anim = 25;
    if (const SkinInfo* a = tex.skin_entry("gogo_fire_anim"); a && a->x > 0 && tex.has_animation((int)a->x))
        change_anim = (int)a->x;
    if (!tex.has_animation(change_anim))
        throw std::runtime_error("gogo time: animation " + std::to_string(change_anim) + " is missing");
    fire_change = (TextureChangeAnimation*)tex.get_animation(change_anim, true);
    fire_fade = 0.5f;
    if (const SkinInfo* f = tex.skin_entry("gogo_fire_fade"); f && f->x > 0)
        fire_fade = std::clamp((float)f->x, 0.0f, 1.0f);

    fire_resize->start();
    fire_change->start();
}

void GogoTime::update(double current_ms) {
    fire_resize->update(current_ms);
    fire_change->update(current_ms);
}

void GogoTime::draw(float judge_x, float judge_y) {
    tex.draw_texture(GOGO_TIME::FIRE, {
        .frame = (int)fire_change->attribute,
        .scale = (float)(fire_resize->attribute),
        .center = true,
        .x = judge_x,
        .y = judge_y,
        .fade = fire_fade});
}
