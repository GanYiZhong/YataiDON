#include "attract_camera.h"
#include "../../libs/texture.h"
#include "../../libs/audio.h"
#include "../../libs/global_data.h"

AttractCamera::AttractCamera() {
    start_ms = get_current_ms();
    camera = WebCamera();
    camera.open(global_data.config->general.webcam_number);
    audio.play_sound("camera", VolumePreset::ATTRACT_MODE);

    live_icon_texture_change = (TextureChangeAnimation*)tex.get_animation(36);
    live_icon_texture_change->start();

    // C-ti-1: how long the idle-title scene holds. The arcade value is
    // title_main.lua's totaltime_ = Common.FPS * 6.5 = 6500 ms; PyTaikoGreen's
    // rig has always used a hardcoded 30 s. Rather than baking either number in,
    // read OPTIONAL animation 37 from the screen's animation.json and use its
    // duration+delay as the scene clock. FAIL SOFT: a skin that does not define
    // 37 (the parent does not) keeps the historical 30 s.
    scene_timer = tex.has_animation(37) ? tex.get_animation(37) : nullptr;
    if (scene_timer) scene_timer->start();
}

void AttractCamera::update(double current_ms) {
    camera.update();
    live_icon_texture_change->update(current_ms);
    if (scene_timer) {
        scene_timer->update(current_ms);
        finished = scene_timer->is_finished;
    } else {
        finished = (current_ms - start_ms >= 30000);
    }
}

void AttractCamera::draw() {
    // ROUND 54: play the arcade idle-title loop when the skin ships it
    // (Scripts/anim/title_idle{,_map}.lua + Graphics/title/camera/idle_c*.png);
    // otherwise keep the historical flat frame-539 bake.
    if (title_idle.ok()) {
        title_idle.draw(get_frame_ms());
    } else {
        tex.draw_texture(CAMERA::BACKGROUND);
    }
    if (camera.is_ready()) {
        ray::Rectangle src = {0, 0, (float)camera.width(), (float)camera.height()};
        SkinInfo cam = tex.skin_config[SC::ATTRACT_CAMERA_VIEWPORT];
        ray::Rectangle dst = {cam.x, cam.y, cam.width, cam.height};
        ray::DrawTexturePro(camera.get_texture(), src, dst, ray::Vector2(0, 0), 0, ray::WHITE);
    }
    tex.draw_texture(CAMERA::LIVE_ICON, {.frame=(int)live_icon_texture_change->attribute});
    tex.draw_texture(CAMERA::LIVE_TEXT);
}

bool AttractCamera::is_finished() {
    return finished;
}
