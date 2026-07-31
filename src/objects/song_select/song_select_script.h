#pragma once
#include "../../libs/script.h"

class SongSelectScript {
public:
    SongSelectScript();
    void update(double current_ms);
    void restart_text_fade();
    void draw_footer();
    void draw_overlays(int state);
};
