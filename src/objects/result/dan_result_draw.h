#pragma once

#include "../../libs/script.h"
#include "../../libs/global_data.h"
#include <vector>

struct DanResultRowSchedule {
    double land = 0, fill0 = 0, filld = 0, numin = 0;
};

// Owns every draw() call of the dan result screen (all texture/text drawing,
// state-branch dispatch between page1+page2 / celebration / congrats). Timeline
// building, input handling, sound cues and score persistence stay native in
// DanResultScreen; this only ever reads state, it never mutates it.
class DanResultDraw : public LuaScript {
    sol::protected_function fn_draw;
    sol::protected_function fn_chara_pos;
    sol::protected_function fn_nameplate_pos;
public:
    DanResultDraw() = default;
    DanResultDraw(const DanResultData& rd, int prev_arrival, int prev_best_score, bool best_score_show,
                  int gauge_exam, int gauge_value, int gauge_border);

    struct FrameState {
        double now = 0;
        double fade_out_attr = 0, page2_fade_attr = 0;
        double page_start_ms = 0, page1_start_ms = 0;
        bool page2_skipped = false;
        double totals_start = 0, totals_end = 0;
        std::vector<DanResultRowSchedule> rows;
        double stamp_at = 0, voice_at = 0;
        bool celebrating = false;
        double celebrate_start_ms = 0;
        bool congrats_showing = false;
        double congrats_start_ms = 0;
    };

    void draw(const FrameState& s);

    // Overrides the caller's default chara/nameplate position when the active
    // draw branch (page2 / celebration / congrats) wants something else;
    // returns false (leaving x/y/etc untouched) when lua has nothing to say.
    bool chara_pos(float& x, float& y, float& scale);
    bool nameplate_pos(float& x, float& y, float& fade);
};
