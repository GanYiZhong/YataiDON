#include "../../scenes/sandbox.h"
#include "../song_select/dan_transition.h"
#include "../song_select/ura_switch.h"

struct DanTransitionFixture : public SandboxScreen::Fixture {
    std::optional<DanTransition> active;

    DanTransitionFixture() { name = "DanTransition"; screen = "song_select"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("dan_transition/background"); }

    void reset(double) override { active.emplace(); }
    void on_space(double) override {
        if (!active) active.emplace();
        active->start();
    }

    void update(double ms) override { if (active && active->is_started()) active->update(ms); }
    void draw() override { if (active && active->is_started()) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return {
            std::string("Started  : ") + (active && active->is_started()  ? "yes" : "no"),
            std::string("Finished : ") + (active && active->is_finished() ? "yes" : "no"),
        };
    }
    std::string controls() override { return "SPACE/L-CLICK: start  R/R-CLICK: reset"; }
};

struct UraSwitchFixture : public SandboxScreen::Fixture {
    std::optional<UraSwitchAnimation> active;
    bool backwards = false;

    UraSwitchFixture() { name = "UraSwitch"; screen = "song_select"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("diff_select/ura_switch"); }

    void reset(double) override { active.emplace(); backwards = false; }
    void on_space(double) override {
        active.emplace();
        active->start(backwards);
    }
    void on_tab(double) override { backwards = !backwards; }

    void update(double ms) override { if (active) active->update(ms); }
    void draw() override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        return { std::string("Backwards : ") + (backwards ? "yes" : "no") };
    }
    std::string controls() override {
        return "SPACE/L-CLICK: trigger  VARIANT: toggle backwards  R/R-CLICK: reset";
    }
};
