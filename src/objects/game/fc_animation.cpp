#include "fc_animation.h"
#include "../../libs/texture.h"
#include "../../libs/audio.h"

FCAnimation::FCAnimation(bool is_2p)
    : is_2p(is_2p), draw_clear_full(false), name("in"), frame(0) {

    bachio_fade_in = (FadeAnimation*)tex.get_animation(46);
    bachio_texture_change = (TextureChangeAnimation*)tex.get_animation(47);
    bachio_out = (TextureChangeAnimation*)tex.get_animation(55);
    bachio_move_out = (MoveAnimation*)tex.get_animation(49);

    bachio_fade_in->start();
    bachio_texture_change->start();
    bachio_out->start();
    bachio_move_out->start();

    for (int i = 0; i < 5; i++) {
        FadeAnimation* fade = new FadeAnimation(100, 1.0f, false, false, 0.0f, i * 50);
        fade->start();
        clear_separate_fade_in.push_back(fade);

        TextStretchAnimation* stretch = new TextStretchAnimation(200, i * 50);
        stretch->start();
        clear_separate_stretch.push_back(stretch);
    }

    clear_highlight_fade_in = (FadeAnimation*)tex.get_animation(56);
    clear_highlight_fade_in->start();

    fc_highlight_up = (MoveAnimation*)tex.get_animation(57);
    fc_highlight_up->start();

    fc_highlight_fade_out = (FadeAnimation*)tex.get_animation(58);
    bachio_move_out_2 = (MoveAnimation*)tex.get_animation(59);
    bachio_move_up = (MoveAnimation*)tex.get_animation(60);
    fan_fade_in = (FadeAnimation*)tex.get_animation(61);
    fan_texture_change = (TextureChangeAnimation*)tex.get_animation(62);

    audio.play_sound("full_combo", VolumePreset::SOUND);
}

void FCAnimation::update(double current_ms) {
    bachio_fade_in->update(current_ms);
    bachio_texture_change->update(current_ms);
    bachio_out->update(current_ms);
    bachio_move_out->update(current_ms);
    clear_highlight_fade_in->update(current_ms);
    fc_highlight_up->update(current_ms);
    fc_highlight_fade_out->update(current_ms);
    bachio_move_out_2->update(current_ms);
    bachio_move_up->update(current_ms);
    fan_fade_in->update(current_ms);
    fan_texture_change->update(current_ms);

    if (fc_highlight_up->is_finished && !fc_highlight_fade_out->is_started) {
        fc_highlight_fade_out->start();
        bachio_move_out_2->start();
        bachio_move_up->start();
        fan_fade_in->start();
        fan_texture_change->start();
        audio.play_sound("full_combo_voice", VolumePreset::VOICE);
    }

    if (clear_highlight_fade_in->attribute == 1.0f) {
        draw_clear_full = true;
    }

    for (auto fade : clear_separate_fade_in) {
        fade->update(current_ms);
    }
    for (auto stretch : clear_separate_stretch) {
        stretch->update(current_ms);
    }

    if (bachio_texture_change->is_finished) {
        name = "out";
        frame = (int)bachio_out->attribute;
    } else {
        frame = (int)bachio_texture_change->attribute;
    }
}

void FCAnimation::draw() {
}
