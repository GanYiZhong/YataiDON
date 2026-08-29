#pragma once

#include "../global/nameplate.h"
#include "../global/chara_3d.h"
#include "../enums.h"
#include "result_crown.h"
#include "score_animator.h"
#include "../../libs/script.h"

class ResultPlayer : public LuaScript {
    sol::protected_function fn_update, fn_draw, fn_draw_gauge, fn_chara_pos, fn_nameplate_pos;
    PlayerNum player_num;
    bool has_2p = false;
    bool is_2p  = false;
    Nameplate nameplate;
    std::unique_ptr<Chara3D> chara;
    std::optional<double> score_delay;
    int update_index = 0;
    std::vector<std::tuple<std::string, int>> update_list;
    std::string score = "", good = "", ok = "", bad = "", max_combo = "", total_drumroll = "";
    std::optional<ScoreAnimator> score_animator;
    bool high_score_sound_played = false;
    // ROUND 15 (result audit): opt-in arcade count-up.  The cabinet's ScoreCounter
    // never rolls on this screen -- ScoreBase.lua only ever calls SkipCounter(), so
    // each row LANDS on its final value at a fixed cadence.  A skin asks for that by
    // setting `count_up_instant = true` on its result_player object (with optional
    // `count_up_row_ms` / `count_up_score_ms`); a skin that does not stays on the
    // per-digit ScoreAnimator roll.
    bool  count_up_instant  = false;
    // ROUND 19 (result press semantics): the engine-clock ms at which every row of
    // the board had landed.  Used as the fallback "the reveal is over" beat for a
    // skin whose result_player script does not publish `reveal_end_ms` itself.
    double rows_done_ms     = 0;
    // ROUND 24 (r24-fpsaudit): Common.FPS is the cabinet's 120 fps script-logic clock, not 60 --
    // these are fallback defaults for a script that doesn't set count_up_row_ms/count_up_score_ms
    // itself; YataiDON-HSS-Zhong's result_player.lua does set them (see its own F = 1000/120).
    double count_up_row_ms   = 416.667;   // ScoreBase.lua UpdateScoreDisplay: timer_ > 50 frames
    double count_up_score_ms = 833.333;   // ...and timer_ > 100 frames for the score itself
    // ROUND 52 (r52-lua-divergence-fixes): the count-up SE, also overridable
    // from the script.  The arcade rows play se_common count_stop_c and the
    // total plays don_big, both 1P-only (ScoreBase.lua:174/:191/:198
    // `this.player_ == 1`); the engine defaults stay num_up/don for parent
    // skins.  The child sets count_up_row_sound="count_stop",
    // count_up_score_sound="don_big", count_up_sound_1p_only=true.
    std::string count_up_row_sound    = "num_up";
    std::string count_up_score_sound  = "don";
    bool        count_up_sound_1p_only = false;
    void assign_field(const std::string& field_name, const std::string& value);
    void update_score_animation(double current_ms, bool is_skipped);
public:
    ResultPlayer() = default;
    ResultPlayer(PlayerNum player_num, bool has_2p, bool is_2p);
    void update(double current_ms, bool fade_in_finished, bool is_skipped);
    void draw();
    // ROUND 19: the engine-clock ms at which this board's reveal sequence finished,
    // or 0 while it is still running.  The cabinet's own beat is MoveResultAction --
    // the END of ResultMain's `Crown` state, after which nothing else is revealed and
    // the machine only runs its two wait states.  A skin publishes that instant as
    // `reveal_end_ms` on its result_player object (YataiDON-HSS-Zhong does); a skin
    // that does not falls back to the moment the last score row landed.
    double reveal_end_ms();
};
