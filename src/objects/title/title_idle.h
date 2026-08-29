#pragma once

// ROUND 54 (r54-anim-engine-referrals): real playback of the arcade idle-title
// loop (attract/title.nulm sprite 84, 540 frames @ 60 fps).  Before this the
// ATTRACT_CAMERA scene drew camera/background.png - a flat bake of frame 539 -
// which ROUND 53's sweep flagged as the largest single visual gap in the skin.
//
// Data-driven and FAIL-SOFT: the clip's per-frame transforms come from the
// generated Scripts/anim/title_idle.lua table and the hand-written companion
// Scripts/anim/title_idle_map.lua names the tracks to draw (in order), the
// baked Graphics/title/camera/idle_c*.png shape texture for each, and the
// shape-space bake origin.  A skin that ships neither file (the parent) keeps
// the static background - ok() returns false and the caller falls back.

#include "../../libs/sample_table.h"
#include <cstdint>
#include <vector>

class TitleIdle {
private:
    struct Item {
        const SampleTrack* trk;
        uint32_t tex_id;
        float ox, oy;
    };

    const SampleTable* table = nullptr;
    const ClipMap* map = nullptr;
    std::vector<Item> items;
    int col_tx = -1, col_ty = -1, col_sx = -1, col_sy = -1, col_a = -1;
    double start_ms = 0.0;

public:
    TitleIdle();

    bool ok() const { return !items.empty(); }

    void draw(double current_ms);
};
