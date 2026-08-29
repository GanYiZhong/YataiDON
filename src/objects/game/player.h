#pragma once

#include "../../libs/screen.h"
#include "../../libs/song_parser.h"
#include "../../libs/text.h"
#include "../../libs/network.h"
#include "../global/nameplate.h"
#include "../global/chara_3d.h"
#include "background.h"
#include "balloon_counter.h"
#include "branch_indicator.h"
#include "clear_animation.h"
#include "combo.h"
#include "combo_announce.h"
#include "drum_hit_effect.h"
#include "drumroll_counter.h"
#include "fail_animation.h"
#include "fc_animation.h"
#include "fireworks.h"
#include "gauge_hit_effect.h"
#include "gogo_time.h"
#include "judgment.h"
#include "kusudama_counter.h"
#include "lane_hit_effect.h"
#include "note_arc.h"
#include "gauge.h"
#include "judge_counter.h"
#include "score_counter.h"
#include "score_counter_animation.h"

namespace JudgePos {
    inline float X = 0.0f;
    inline float Y = 0.0f;
}

namespace Timing {
    constexpr float GOOD = 25.0250015258789f;
    constexpr float OK = 75.0750045776367f;
    constexpr float BAD = 108.441665649414f;
    constexpr float GOOD_EASY = 41.7083358764648f;
    constexpr float OK_EASY = 108.441665649414f;
    constexpr float BAD_EASY = 125.125f;
}

class Player {
public:
    double end_time;
    float bpm;
    PlayerNum player_num;
    double last_note_hit;
    std::map<double, InputLogType> input_log;

    Player(std::optional<SongParser>& parser_ref, PlayerNum player_num_param, int difficulty_param,
           bool is_2p_param, const Modifiers& modifiers_param);

    std::optional<JudgeCounter> judge_counter;
    std::optional<Gauge> gauge;
    Gauge* dan_gauge = nullptr;  // non-owning; set by DanGameScreen

    // ROUND 25 (r25-kusudama2p): non-owning; set by Game2PScreen so both seated
    // players' kusudama (段位鼓) hits sum into ONE shared pool/pop instead of two
    // independent per-player counters. Stays nullptr in 1P/DAN, which makes
    // kusudama_owner() below return `this` unconditionally there -- i.e. those
    // modes keep the exact pre-existing single-player behaviour.
    Player* kusudama_partner = nullptr;
    // Valid only on the pool owner (see kusudama_owner()); combined hit count
    // toward the one shared kusudama target.
    int kusudama_shared_hits = 0;
    // Returns the Player instance that owns the shared KusudamaCounter/hit pool
    // for the CURRENT kusudama note. P1 (is_2p == false) is always the owner
    // when a partner exists; a lone player (no partner) is always its own
    // owner.
    Player* kusudama_owner() { return (kusudama_partner && is_2p) ? kusudama_partner : this; }

    std::optional<Note> get_first_note();

    ResultData get_result_score();

    int get_good() const { return good_count; }
    int get_ok()   const { return ok_count; }
    int get_bad()  const { return bad_count; }
    bool is_auto_play() const { return modifiers.auto_play; }
    // 演奏スキップ: this lane's owner turned the arcade option on.
    bool is_skip_enabled() const { return modifiers.skip; }
    // 演奏スキップ trigger: stop the chart dead at `now` and let the normal
    // end-of-song path (ending animation -> result) take over. Every pending
    // note is dropped so nothing else is judged or drawn, mirroring the arcade
    // cutting the enso at the tenth rim hit.
    // 演奏スキップ. `cut_to_end` is the skip path; `was_skipped` lets the record
    // and the ending follow the cabinet (see the comment on cut_to_end).
    // ROUND 20 (r19-danskip): `prev_good`/`prev_ok`/`prev_bad` are the
    // per-song baseline a cumulative multi-song player (段位道場) needs so the
    // recount below only charges THIS song's unreached notes as 不可 instead
    // of corrupting the bad count already banked by earlier songs in the
    // course. Default 0 keeps the normal 1-song GameScreen call site (the
    // whole run IS the song) byte-identical.
    void cut_to_end(double now, int prev_good = 0, int prev_ok = 0, int prev_bad = 0);
    bool was_skipped() const { return skipped_run; }
    // Practice mode toggles auto from its pause menu mid-song.
    void set_auto_play(bool value) { modifiers.auto_play = value; }
    // Practice replays sections over and over; each resume starts the score,
    // combo and judgement tallies fresh so the numbers describe the current
    // attempt only.
    void reset_performance() {
        good_count = ok_count = bad_count = 0;
        combo = max_combo = 0;
        score = 0;
        total_drumroll = 0;
        was_gauge_full = false;
        if (judge_counter) judge_counter = JudgeCounter();
    }
    int get_score() const { return score; }
    int get_max_combo() const { return max_combo; }
    // ROUND 50 (r50-dani-visual-completion): the LIVE combo, read-only. Needed
    // by DanGameScreen to keep the cabinet's per-song g_maxComboNum_[j] (a
    // per-song combo maximum the run-wide max_combo cannot be decomposed into).
    int get_combo() const { return combo; }
    int get_total_drumroll() const { return total_drumroll; }
    int get_scissor_x() const { return virtual_to_screen_x(static_cast<float>(tex.textures[lane_cover_tex_id]->x2[0])); }
    void set_is_dan(bool v) { is_dan = v; }

    void reload_for_dan(std::optional<SongParser>& new_parser, int new_difficulty);

    void spawn_ending_anim();

    void seek_to(double resume_time);

    void update(double ms_from_start, double current_ms, std::optional<Background>& background);

    void draw(double ms_from_start, float x, float y, ray::Shader& mask_shader);

    void draw_practice(double ms_from_start, float x, float y, ray::Shader& mask_shader, bool draw_notes_on);

    void draw_overlays(float y, const ray::Shader& mask_shader);
    void draw_lane_cover(float y);

protected:
    std::optional<LaneHitEffect> lane_hit_effect;
    std::vector<std::unique_ptr<DrumHitEffect>> draw_drum_hit_list;

private:
    bool is_2p;
    // Judgeable (DON..KAT_L) notes in the whole chart, master branch included -
    // the same count the Gauge is built from. The arcade's skip path needs it to
    // recount 不可 as `total - 良 - 可`.
    int  judgeable_note_count = 0;
    bool skipped_run = false;
    bool is_dan;
    int difficulty;
    int visual_offset;
    std::string score_method;
    Modifiers modifiers;
    std::optional<SongParser> parser;
    Nameplate nameplate;
    std::unique_ptr<Chara3D> chara;

    // Score management
    int good_count;
    int ok_count;
    int bad_count;
    int combo;
    int score;
    int max_combo;
    int total_drumroll;

    int arc_points;
    float judge_x;
    float judge_y;

    float scroll_multiplier;
    bool is_gogo_time;
    // ROUND (r-chara3d-fullanim): edge-trigger state for the previously-dead
    // AnimIndex::DON_FULL_GAGE pose. Gauge::get_is_rainbow() already flips
    // true exactly when gauge_length == gauge_max (gauge.cpp); we only want
    // to fire the pose switch on the false->true transition, not every
    // frame the gauge happens to sit at max, so the chara doesn't fight
    // DON_SABI/DON_COMBO for every subsequent frame's set_anim() call.
    bool was_gauge_full = false;
    Side autoplay_hit_side;
    int last_subdivision;

    std::deque<Note> don_notes;
    std::deque<Note> kat_notes;
    std::deque<Note> other_notes;
    std::deque<Note> barlines;

    std::deque<Note> draw_note_list;
    std::vector<Note> draw_note_buffer;

    std::deque<NoteList> branch_m;
    std::deque<NoteList> branch_e;
    std::deque<NoteList> branch_n;

    std::deque<TimelineObject> timeline;
    std::vector<TimelineObject> timeline_buffer;

    int base_score;
    int score_init;
    int score_diff;

    bool is_drumroll;
    int curr_drumroll_count;
    double last_drumroll_color_time;
    bool is_balloon;
    int curr_balloon_count;
    int balloon_index;

    std::optional<OutlinedText> current_lyric;

    bool is_branch;
    std::tuple<float, float, double> curr_branch_reqs;
    float branch_p_count;
    int branch_r_count;
    int branch_note_count;
    std::string branch_condition;

    std::string don_hitsound;
    std::string kat_hitsound;

    TexID lane_cover_tex_id;
    TexID lane_icon_tex_id;
    TexID note_tex_ids[10];

    std::vector<Judgment> draw_judge_list;
    std::vector<GaugeHitEffect> gauge_hit_effect;
    std::vector<NoteArc> draw_arc_list;
    Combo combo_display;
    std::optional<DrumrollCounter> drumroll_counter;
    std::optional<BalloonCounter> balloon_counter;
    std::optional<KusudamaCounter> kusudama_counter;
    ScoreCounter score_counter;
    std::vector<ScoreCounterAnimation> base_score_list;
    std::optional<GogoTime> gogo_time;
    std::optional<Fireworks> fireworks;
    std::optional<double> delay_start;
    std::optional<double> delay_end;
    std::optional<ComboAnnounce> combo_announce;
    std::optional<BranchIndicator> branch_indicator;
    std::optional<std::variant<FailAnimation, ClearAnimation, FCAnimation>> ending_anim;

    void get_load_time(Note& note);

    void reset_chart();

    void handle_timeline(double ms_from_start);

    void autoplay_manager(double ms_from_start, double current_ms, std::optional<Background>& background);

    void evaluate_branch(double current_ms);

    void merge_branch_section(const NoteList& branch_section, double current_ms);

    float get_position_x(const Note& note, double current_ms);

    float get_position_y(const Note& note, double current_ms);

    void handle_scroll_type_commands(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);
    void handle_gogotime(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);
    void handle_judgeposition(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);
    void handle_bpmchange(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);
    void handle_branch_param(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);
    void handle_lyric(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);
    void handle_section(double ms_from_start, const TimelineObject& timeline_object, int buffer_index);

    void play_note_manager(double current_ms, std::optional<Background>& background);

    bool is_balloon_type(int type);

    void draw_note_manager(double current_ms);

    void note_manager(double current_ms, std::optional<Background>& background);

    void note_correct(const Note& note, double current_ms);

    void check_drumroll(double current_ms, DrumType drum_type, std::optional<Background>& background);

    void check_balloon(double current_ms, DrumType drum_type, const Note& balloon, std::optional<Background>& background);

    void check_kusudama(double current_ms, DrumType drum_type, const Note& balloon, std::optional<Background>& background);

    void check_note(double ms_from_start, DrumType drum_type, double current_ms, std::optional<Background>& background);

    void drumroll_counter_manager(double current_ms);

    void balloon_counter_manager(double current_ms);

    void kusudama_counter_manager(double current_ms);

    virtual void spawn_hit_effects(DrumType drum_type, Side side);

protected:
    virtual void handle_input(double ms_from_start, double current_ms, std::optional<Background>& background);

private:

    void draw_bar(double current_ms, float y, const Note& bar);

    // ROUND 55: `note_frame` is the CHN05 combo-state face frame (0/1/2),
    // already computed per frame by draw_notes (was the raw eighth counter).
    void draw_drumroll(double current_ms, float y, const Note& head, int note_frame, bool moji_pass);

    // ROUND 55: balloon takes the face frame plus the CHN05 squash pulse
    // (x-scale 1.0..0.7, left edge anchored -- OnpuDraw.obj.c:5273-5294).
    void draw_balloon(double current_ms, float y, const Note& head, int note_frame, float pulse, bool moji_pass);

    void draw_notes(double current_ms, float y);

    void draw_song_timer(double current_ms, float y);

    void draw_modifiers(float y);
};
