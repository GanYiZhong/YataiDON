#include "../../scenes/sandbox.h"
#include "../game/judgment.h"
#include "../game/drum_hit_effect.h"
#include "../game/combo.h"
#include "../game/lane_hit_effect.h"
#include "../game/gauge_hit_effect.h"
#include "../game/gogo_time.h"
#include "../game/score_counter.h"
#include "../game/combo_announce.h"
#include "../game/drumroll_counter.h"
#include "../game/balloon_counter.h"
#include "../game/fireworks.h"
#include "../game/fc_animation.h"
#include "../game/clear_animation.h"
#include "../game/fail_animation.h"
#include "../game/branch_indicator.h"
#include "../game/gauge.h"
#include "../game/judge_counter.h"
#include "../game/kusudama_counter.h"
#include "../game/note_arc.h"
#include "../game/result_transition.h"
#include "../game/transition.h"
#include "../game/score_counter_animation.h"
#include "../game/song_info.h"

// ─── Existing fixtures ────────────────────────────────────────────────────────

struct JudgmentFixture : public SandboxScreen::Fixture {
    int  type_idx = 0;
    bool big      = false;
    std::optional<Judgment> active;

    JudgmentFixture() { name = "Judgment"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("hit_effect/outer_good"); }

    void reset(double) override { active.reset(); type_idx = 0; big = false; }

    void on_space(double) override {
        static const Judgments types[] = { Judgments::GOOD, Judgments::OK, Judgments::BAD };
        active.emplace(types[type_idx], big);
    }
    void on_tab(double) override { big = !big; }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override {
        if (!active) return;
        active->draw_effect(0, 0);
        active->draw_text(0, 0);
    }

    std::vector<std::string> type_names() override { return {"GOOD", "OK", "BAD"}; }
    int  get_type()           override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return {
            std::string("Big    : ") + (big ? "yes" : "no"),
            std::string("Active : ") + (active.has_value() ? "yes" : "no"),
        };
    }
    std::string controls() override {
        return "SPACE/L-CLICK: trigger  R/R-CLICK: reset  VARIANT: toggle big";
    }
};

struct DrumHitFixture : public SandboxScreen::Fixture {
    int variant = 0;
    std::optional<DrumHitEffect> active;

    DrumHitFixture() { name = "DrumHitEffect"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("lane/drum_don_l"); }

    void reset(double) override { active.reset(); variant = 0; }

    void on_space(double) override {
        static const DrumType dtype[] = { DrumType::DON, DrumType::DON, DrumType::KAT, DrumType::KAT };
        static const Side     dside[] = { Side::LEFT,    Side::RIGHT,   Side::LEFT,    Side::RIGHT   };
        active.emplace(dtype[variant], dside[variant]);
    }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override { return {"DON L", "DON R", "KAT L", "KAT R"}; }
    int  get_type()           override { return variant; }
    void set_type(int idx, double) override { variant = idx; }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override {
        return "SPACE/L-CLICK: trigger  R/R-CLICK: reset";
    }
};

struct ComboFixture : public SandboxScreen::Fixture {
    Combo combo;
    int   count       = 0;
    bool  initialized = false;

    ComboFixture() { name = "Combo"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("combo/combo_ja"); }

    void reset(double ms) override {
        count = 0;
        combo = Combo(0, ms);
        initialized = true;
    }
    void on_space(double ms) override {
        combo.update(ms, ++count);
        initialized = true;
    }

    void update(double ms) override { if (initialized) combo.update(ms, count); }
    void draw()             override { if (initialized) combo.draw(0); }

    std::vector<std::string> debug_lines() override {
        return { "Combo : " + std::to_string(count) };
    }
    std::string controls() override {
        return "SPACE/L-CLICK: increment combo  R/R-CLICK: reset";
    }
};

struct LaneHitEffectFixture : public SandboxScreen::Fixture {
    int type_idx = 0;
    std::optional<LaneHitEffect> active;

    static constexpr DrumType  dtypes[] = { DrumType::DON, DrumType::DON, DrumType::KAT, DrumType::KAT };
    static constexpr Judgments judgs[]  = { Judgments::GOOD, Judgments::BAD, Judgments::GOOD, Judgments::BAD };

    LaneHitEffectFixture() { name = "LaneHitEffect"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("lane/lane_hit_effect"); }

    void reset(double) override { active.reset(); type_idx = 0; }
    void on_space(double) override { active.emplace(dtypes[type_idx], judgs[type_idx]); }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override { return {"DON GOOD", "DON", "KAT GOOD", "KAT"}; }
    int  get_type()           override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

struct GaugeHitEffectFixture : public SandboxScreen::Fixture {
    int  type_idx = 0;
    bool big      = false;
    std::optional<GaugeHitEffect> active;

    GaugeHitEffectFixture() { name = "GaugeHitEffect"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("gauge/hit_effect"); }

    void reset(double) override { active.reset(); type_idx = 0; big = false; }
    void on_space(double) override {
        static const NoteType types[] = { NoteType::DON, NoteType::KAT_L };
        active.emplace(types[type_idx], big, types[type_idx] == NoteType::DON ? false : true);
    }
    void on_tab(double) override { big = !big; }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override { return {"DON", "KAT"}; }
    int  get_type()           override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return {
            std::string("Big    : ") + (big ? "yes" : "no"),
            std::string("Active : ") + (active.has_value() ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset  VARIANT: toggle big"; }
};

// ─── Persistent / looping objects ────────────────────────────────────────────

struct GogoTimeFixture : public SandboxScreen::Fixture {
    std::optional<GogoTime> gogo;

    GogoTimeFixture() { name = "GogoTime"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("gogo_time/fire"); }

    void reset(double) override { gogo.emplace(); }
    void on_space(double) override { gogo.emplace(); }

    void update(double ms) override { if (gogo) gogo->update(ms); }
    void draw()            override { if (gogo) gogo->draw(0, 0); }

    std::string controls() override { return "SPACE/L-CLICK: restart  R/R-CLICK: restart"; }
};

struct ScoreCounterFixture : public SandboxScreen::Fixture {
    std::optional<ScoreCounter> counter;
    int score = 0;

    ScoreCounterFixture() { name = "ScoreCounter"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("lane/lane_score_cover"); }

    void reset(double ms) override {
        score = 0;
        counter.emplace(0, false);
    }
    void on_space(double ms) override {
        score += 1000;
        if (counter) counter->update_count(score);
    }

    void update(double ms) override { if (counter) counter->update(ms); }
    void draw()            override { if (counter) counter->draw(0); }

    std::vector<std::string> debug_lines() override {
        return { "Score : " + std::to_string(score) };
    }
    std::string controls() override { return "SPACE/L-CLICK: +1000 pts  R/R-CLICK: reset"; }
};

// ─── Roll / balloon counters ──────────────────────────────────────────────────

struct DrumrollCounterFixture : public SandboxScreen::Fixture {
    std::optional<DrumrollCounter> counter;
    int count = 0;

    DrumrollCounterFixture() { name = "DrumrollCounter"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("drumroll_counter/bubble"); }

    void reset(double ms) override {
        count = 0;
        counter.emplace();
        counter->update(ms, 0);
    }
    void on_space(double ms) override {
        if (counter) counter->update(ms, ++count);
    }

    void update(double ms) override { if (counter) counter->update(ms, count); }
    void draw()            override { if (counter) counter->draw(0); }

    std::vector<std::string> debug_lines() override {
        return {
            "Count    : " + std::to_string(count),
            std::string("Finished : ") + (counter && counter->is_finished() ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: increment  R/R-CLICK: reset"; }
};

struct BalloonCounterFixture : public SandboxScreen::Fixture {
    int  preset_idx = 0;
    int  hit_count  = 0;  // counts up from 0; pops when == total
    std::optional<BalloonCounter> counter;

    static constexpr int totals[] = { 5, 10, 20, 50 };

    BalloonCounterFixture() { name = "BalloonCounter"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("balloon/bubble"); }

    void reset(double ms) override {
        hit_count = 0;
        counter.reset();
        counter.emplace(totals[preset_idx], false);
        counter->update(ms, 0);
    }
    void on_space(double ms) override {
        if (!counter || counter->is_finished() || hit_count >= totals[preset_idx]) return;
        counter->update(ms, ++hit_count);
    }

    void update(double ms) override { if (counter) counter->update(ms, hit_count); }
    void draw()            override { if (counter) counter->draw(0); }

    std::vector<std::string> type_names() override { return {"5 hits", "10 hits", "20 hits", "50 hits"}; }
    int  get_type()           override { return preset_idx; }
    void set_type(int idx, double ms) override { preset_idx = idx; reset(ms); }

    std::vector<std::string> debug_lines() override {
        int total = totals[preset_idx];
        return {
            "Hits      : " + std::to_string(hit_count) + " / " + std::to_string(total),
            "Popped    : " + std::string(counter && counter->is_finished() ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: hit  R/R-CLICK: reset"; }
};

// ─── Announcements ────────────────────────────────────────────────────────────

struct ComboAnnounceFixture : public SandboxScreen::Fixture {
    int type_idx = 0;
    PlayerNum player_num = PlayerNum::P1;
    std::optional<ComboAnnounce> active;

    static constexpr int combos[] = { 100, 200, 500, 700, 1000, 1800, 2200 };

    ComboAnnounceFixture() { name = "ComboAnnounce"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("combo/announce_bg_1p"); }

    void reset(double) override { active.reset(); type_idx = 0; }
    void on_space(double ms) override {
        active.emplace(combos[type_idx], ms, player_num);
    }
    void on_tab(double) override { player_num = player_num == PlayerNum::P1 ? PlayerNum::P2 : PlayerNum::P1; }

    void update(double ms) override {
        if (active && active->is_finished) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override {
        return {"100", "200", "500", "700", "1000", "1800", "2200"};
    }
    int  get_type()           override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

// ─── Ending animations ────────────────────────────────────────────────────────

struct FCAnimationFixture : public SandboxScreen::Fixture {
    std::optional<FCAnimation> active;

    FCAnimationFixture() { name = "FCAnimation"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("ending_anim/full_combo"); }

    void reset(double) override { active.reset(); }
    void on_space(double) override { active.emplace(false); }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

struct ClearAnimationFixture : public SandboxScreen::Fixture {
    std::optional<ClearAnimation> active;

    ClearAnimationFixture() { name = "ClearAnimation"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("ending_anim/clear"); }

    void reset(double) override { active.reset(); }
    void on_space(double) override { active.emplace(false); }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

struct FailAnimationFixture : public SandboxScreen::Fixture {
    std::optional<FailAnimation> active;

    FailAnimationFixture() { name = "FailAnimation"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("ending_anim/fail"); }

    void reset(double) override { active.reset(); }
    void on_space(double) override { active.emplace(false); }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

struct FireworksFixture : public SandboxScreen::Fixture {
    std::optional<Fireworks> active;

    FireworksFixture() { name = "Fireworks"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("gogo_time/explosion"); }

    void reset(double) override { active.reset(); }
    void on_space(double) override { active.emplace(); }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

// ─── Additional fixtures ──────────────────────────────────────────────────────

struct BranchIndicatorFixture : public SandboxScreen::Fixture {
    std::optional<BranchIndicator> active;
    int type_idx = 0;
    bool last_was_up = false;

    static constexpr BranchDifficulty diffs[] = {
        BranchDifficulty::NORMAL, BranchDifficulty::EXPERT, BranchDifficulty::MASTER
    };

    BranchIndicatorFixture() { name = "BranchIndicator"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("branch/expert_bg"); }

    void reset(double) override { active.emplace(); last_was_up = false; }
    void on_space(double) override {
        if (!active) active.emplace();
        active->level_up(diffs[type_idx]);
        last_was_up = true;
    }
    void on_tab(double) override {
        if (!active) active.emplace();
        active->level_down(diffs[type_idx]);
        last_was_up = false;
    }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override { return {"NORMAL", "EXPERT", "MASTER"}; }
    int  get_type() override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return { std::string("Last  : ") + (last_was_up ? "level_up" : "level_down") };
    }
    std::string controls() override { return "SPACE/L-CLICK: level up  VARIANT: level down  R/R-CLICK: reset"; }
};

struct GaugeFixture : public SandboxScreen::Fixture {
    std::optional<Gauge> active;
    PlayerNum player_num = PlayerNum::P1;
    int type_idx = 0;

    GaugeFixture() { name = "Gauge"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("gauge/overlay_hard"); }

    void reset(double) override {
        active.emplace(100, 2, 5, player_num);
        type_idx = 0;
    }
    void on_space(double) override {
        if (!active) return;
        if (type_idx == 0)      active->add_good();
        else if (type_idx == 1) active->add_ok();
        else                    active->add_bad();
    }
    void on_tab(double) override { player_num = player_num == PlayerNum::P1 ? PlayerNum::P2 : PlayerNum::P1; }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override { return {"GOOD", "OK", "BAD"}; }
    int  get_type() override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        if (!active) return {};
        return {
            "Progress : " + std::to_string((float)(active->get_length())) + "%",
            std::string("Clear    : ") + (active->get_is_clear()   ? "yes" : "no"),
            std::string("Rainbow  : ") + (active->get_is_rainbow() ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: add hit  R/R-CLICK: reset"; }
};

struct JudgeCounterFixture : public SandboxScreen::Fixture {
    std::optional<JudgeCounter> counter;
    int good = 0, ok = 0, bad = 0, rolls = 0;
    int type_idx = 0;

    JudgeCounterFixture() { name = "JudgeCounter"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("judge_counter/bg"); }

    void reset(double) override {
        good = ok = bad = rolls = 0;
        counter.emplace();
        counter->update(good, ok, bad, rolls);
        type_idx = 0;
    }
    void on_space(double) override {
        if (!counter) return;
        if (type_idx == 0)      good++;
        else if (type_idx == 1) ok++;
        else if (type_idx == 2) bad++;
        else                    rolls++;
        counter->update(good, ok, bad, rolls);
    }

    void update(double) override {}
    void draw()         override { if (counter) counter->draw(); }

    std::vector<std::string> type_names() override { return {"GOOD", "OK", "BAD", "DRUMROLL"}; }
    int  get_type() override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return {
            "Good     : " + std::to_string(good),
            "OK       : " + std::to_string(ok),
            "Bad      : " + std::to_string(bad),
            "Drumroll : " + std::to_string(rolls),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: increment selected  R/R-CLICK: reset"; }
};

struct KusudamaCounterFixture : public SandboxScreen::Fixture {
    std::optional<KusudamaCounter> counter;
    int hit_count  = 0;
    int preset_idx = 0;

    static constexpr int totals[] = { 5, 10, 20, 50 };

    KusudamaCounterFixture() { name = "KusudamaCounter"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("kusudama/renda"); }

    void reset(double ms) override {
        hit_count = 0;
        counter.emplace(totals[preset_idx]);
        counter->update(ms, 0);
    }
    void on_space(double ms) override {
        if (!counter || counter->is_finished() || hit_count >= totals[preset_idx]) return;
        counter->update(ms, ++hit_count);
    }

    void update(double ms) override { if (counter) counter->update(ms, hit_count); }
    void draw()            override { if (counter) counter->draw(); }

    std::vector<std::string> type_names() override { return {"5 hits", "10 hits", "20 hits", "50 hits"}; }
    int  get_type() override { return preset_idx; }
    void set_type(int idx, double ms) override { preset_idx = idx; reset(ms); }

    std::vector<std::string> debug_lines() override {
        return {
            "Hits : " + std::to_string(hit_count) + " / " + std::to_string(totals[preset_idx]),
            std::string("Done : ") + (counter && counter->is_finished() ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: hit  R/R-CLICK: reset"; }
};

struct NoteArcFixture : public SandboxScreen::Fixture {
    std::optional<NoteArc> active;
    ray::Shader mask_shader{};
    int type_idx = 0;

    static constexpr NoteType arc_note_types[] = { NoteType::DON, NoteType::DON_L };
    static constexpr bool     arc_is_balloon[]  = { false,                  true              };

    NoteArcFixture() {
        name = "NoteArc";
        mask_shader = load_shader("shader/dummy.vs", "shader/mask.fs");
        auto rm = std::dynamic_pointer_cast<SingleTexture>(tex.textures["balloon/rainbow_mask"]);
        auto r  = std::dynamic_pointer_cast<SingleTexture>(tex.textures["balloon/rainbow"]);
        if (rm && r) {
            SetShaderValueTexture(mask_shader, GetShaderLocation(mask_shader, "texture0"), rm->texture);
            SetShaderValueTexture(mask_shader, GetShaderLocation(mask_shader, "texture1"), r->texture);
        }
    }
    ~NoteArcFixture() { ray::UnloadShader(mask_shader); }

    TextureObject* anchor_texture_id() override { return tex.get_texture("balloon/rainbow"); }

    void reset(double) override { active.reset(); }
    void on_space(double ms) override {
        active.emplace(arc_note_types[type_idx], ms, PlayerNum::P1, false, arc_is_balloon[type_idx]);
    }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(0, mask_shader); }

    std::vector<std::string> type_names() override { return {"Normal", "Balloon"}; }
    int  get_type() override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

struct ResultTransitionFixture : public SandboxScreen::Fixture {
    std::optional<ResultTransition> active;

    ResultTransitionFixture() { name = "ResultTransition"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("result_transition/1p_shutter"); }

    void reset(double) override { active.emplace(PlayerNum::P1); }
    void on_space(double) override {
        if (!active) active.emplace(PlayerNum::P1);
        active->start();
    }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return {
            std::string("Started  : ") + (active && active->is_started  ? "yes" : "no"),
            std::string("Finished : ") + (active && active->is_finished ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: start  R/R-CLICK: reset"; }
};

struct TransitionFixture : public SandboxScreen::Fixture {
    std::optional<Transition> active;

    TransitionFixture() { name = "Transition"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("rainbow_transition/text_bg"); }

    void reset(double) override { active.emplace("Test Song", "Test Subtitle", false); }
    void on_space(double) override {
        if (!active) active.emplace("Test Song", "Test Subtitle", false);
        active->start();
    }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { std::string("Finished : ") + (active && active->is_finished() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: start  R/R-CLICK: reset"; }
};

struct ScoreCounterAnimFixture : public SandboxScreen::Fixture {
    std::optional<ScoreCounterAnimation> active;
    int type_idx = 0;

    static constexpr int scores[] = { 500, 1000, 5000, 10000 };

    ScoreCounterAnimFixture() { name = "ScoreCounterAnim"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("lane/score_number"); }

    void reset(double) override { active.reset(); type_idx = 0; }
    void on_space(double) override { active.emplace(PlayerNum::P1, scores[type_idx], false); }

    void update(double ms) override {
        if (active && active->is_finished()) active.reset();
        if (active) active->update(ms);
    }
    void draw() override { if (active) active->draw(0); }

    std::vector<std::string> type_names() override { return {"500", "1000", "5000", "10000"}; }
    int  get_type() override { return type_idx; }
    void set_type(int idx, double) override { type_idx = idx; }

    std::vector<std::string> debug_lines() override {
        return { std::string("Active : ") + (active.has_value() ? "yes" : "no") };
    }
    std::string controls() override { return "SPACE/L-CLICK: trigger  R/R-CLICK: reset"; }
};

struct SongInfoFixture : public SandboxScreen::Fixture {
    std::optional<SongInfo> active;
    int genre = 0;

    SongInfoFixture() { name = "SongInfo"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("song_info/genre"); }

    void reset(double) override { active.emplace("Test Song", "Test Subtitle", true, genre, 1); }
    void on_tab(double) override {
        genre = (genre + 1) % 9;
        active.emplace("Test Song", "Test Subtitle", true, genre, 1);
    }

    void update(double ms) override { if (active) active->update(ms); }
    void draw()            override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { "Genre : " + std::to_string(genre) };
    }
    std::string controls() override { return "VARIANT: cycle genre  R/R-CLICK: reset"; }
};
