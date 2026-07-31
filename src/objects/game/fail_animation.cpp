#include "fail_animation.h"
#include "../../libs/texture.h"
#include "../../libs/audio.h"

FailAnimation::FailAnimation(bool is_2p)
    : is_2p(is_2p), name("in"), frame(0) {

    bachio_fade_in = (FadeAnimation*)tex.get_animation(46);
    bachio_texture_change = (TextureChangeAnimation*)tex.get_animation(47);
    bachio_fall = (MoveAnimation*)tex.get_animation(48);
    bachio_move_out = (MoveAnimation*)tex.get_animation(49);
    bachio_boom_fade_in = (FadeAnimation*)tex.get_animation(50);
    bachio_boom_scale = (TextureResizeAnimation*)tex.get_animation(51);
    bachio_up = (MoveAnimation*)tex.get_animation(52);
    bachio_down = (MoveAnimation*)tex.get_animation(53);
    text_fade_in = (FadeAnimation*)tex.get_animation(54);

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
}
