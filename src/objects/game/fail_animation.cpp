#include "fail_animation.h"
#include "../../libs/audio.h"

FailAnimation::FailAnimation(bool is_2p)
    : is_2p(is_2p), name("in"), frame(0) {

    bachio_fade_in = (FadeAnimation*)tex.get_animation(46, true);
    bachio_texture_change = (TextureChangeAnimation*)tex.get_animation(47, true);
    bachio_fall = (MoveAnimation*)tex.get_animation(48, true);
    bachio_move_out = (MoveAnimation*)tex.get_animation(49);
    bachio_boom_fade_in = (FadeAnimation*)tex.get_animation(50);
    bachio_boom_scale = (TextureResizeAnimation*)tex.get_animation(51);
    bachio_up = dynamic_cast<MoveAnimation*>(tex.get_animation(52, true));
    bachio_down = dynamic_cast<MoveAnimation*>(tex.get_animation(53, true));
    text_fade_in = dynamic_cast<FadeAnimation*>(tex.get_animation(54, true));

    text_fade_in->start();
    bachio_fade_in->start();
    bachio_texture_change->start();
    bachio_fall->start();
    bachio_move_out->start();
    bachio_boom_fade_in->start();
    bachio_boom_scale->start();
    bachio_up->start();
    bachio_down->start();

    audio.play_sound("fail", VolumePreset::SOUND);

    t_fail = tex.get_texture("ending_anim/fail");
    t_bachio_boom = tex.get_texture("ending_anim/bachio_boom");
    t_bachio_l_in = tex.get_texture("ending_anim/bachio_l_in");
    t_bachio_l_fall = tex.get_texture("ending_anim/bachio_l_fall");
    t_bachio_r_in = tex.get_texture("ending_anim/bachio_r_in");
    t_bachio_r_fall = tex.get_texture("ending_anim/bachio_r_fall");
}

void FailAnimation::update(double current_ms) {
    bachio_fade_in->update(current_ms);
    bachio_texture_change->update(current_ms);
    bachio_fall->update(current_ms);
    bachio_move_out->update(current_ms);
    bachio_boom_fade_in->update(current_ms);
    bachio_boom_scale->update(current_ms);
    bachio_up->update(current_ms);
    bachio_down->update(current_ms);
    text_fade_in->update(current_ms);

    if (bachio_texture_change->is_finished) {
        name = "fall";
        frame = (int)bachio_fall->attribute;
    } else {
        frame = (int)bachio_texture_change->attribute;
    }
}

void FailAnimation::draw() {
    tex.draw_texture(t_fail, {
        .fade = (float)(text_fade_in->attribute),
        .index = (int)is_2p
    });

    tex.draw_texture(name == "in" ? t_bachio_l_in : t_bachio_l_fall, {
        .frame = frame,
        .x = (float)(-bachio_move_out->attribute - (bachio_up->attribute / 2)),
        .y = (float)(bachio_down->attribute - bachio_up->attribute),
        .fade = (float)(bachio_fade_in->attribute),
        .index = (int)is_2p
    });

    tex.draw_texture(name == "in" ? t_bachio_r_in : t_bachio_r_fall, {
        .frame = frame,
        .x = (float)(bachio_move_out->attribute + (bachio_up->attribute / 2)),
        .y = (float)(bachio_down->attribute - bachio_up->attribute),
        .fade = (float)(bachio_fade_in->attribute),
        .index = (int)is_2p
    });

    tex.draw_texture(t_bachio_boom, {
        .scale = (float)(bachio_boom_scale->attribute),
        .center = true,
        .y = (is_2p * tex.skin_config[SC::OFFSET_2P].y),
        .fade = (float)(bachio_boom_fade_in->attribute),
        .index = 0
    });

    tex.draw_texture(t_bachio_boom, {
        .scale = (float)(bachio_boom_scale->attribute),
        .center = true,
        .y = (is_2p * tex.skin_config[SC::OFFSET_2P].y),
        .fade = (float)(bachio_boom_fade_in->attribute),
        .index = 1
    });
}
