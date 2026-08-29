#pragma once

#include "../../libs/webcam.h"
#include "../../libs/animation.h"
#include "title_idle.h"

class AttractCamera {
private:
    double start_ms = 0.0;
    bool finished = false;
    WebCamera camera;
    // ROUND 54: real 540-frame idle-title playback; falls back to the static
    // CAMERA::BACKGROUND bake when the skin ships no title_idle data.
    TitleIdle title_idle;

    TextureChangeAnimation* live_icon_texture_change;
    // Optional per-skin clock for this scene; nullptr => the historical 30 s.
    BaseAnimation* scene_timer = nullptr;
public:
    AttractCamera();

    void update(double current_ms);

    void draw();

    bool is_finished();
};
