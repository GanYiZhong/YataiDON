#include "camera_cloud.h"
#include "../../libs/texture.h"

CameraCloud::CameraCloud() {
    fade_in = (FadeAnimation*)tex.get_animation(32);
    fade_in->start();
    text_fade = (FadeAnimation*)tex.get_animation(33);
    text_fade->start();
    move_up = (MoveAnimation*)tex.get_animation(34);
    move_up->start();
    breathing = (TextureResizeAnimation*)tex.get_animation(35);
    breathing->start();
}

void CameraCloud::update(double current_ms) {
    fade_in->update(current_ms);
    text_fade->update(current_ms);
    move_up->update(current_ms);
    breathing->update(current_ms);
}

void CameraCloud::draw() {}
