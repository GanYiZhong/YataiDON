#include "../../scenes/sandbox.h"
#include "../entry/costume_menu.h"

struct CostumeMenuFixture : public SandboxScreen::Fixture {
    std::optional<CostumeMenu> active;

    CostumeMenuFixture() { name = "CostumeMenu"; screen = "entry"; }

    TextureObject* anchor_texture_id() override { return tex.get_texture("costume_select/box_center_1p"); }

    PlayerNum fixture_player = PlayerNum::P1;

    void reset(double) override { active.emplace(fixture_player); }
    void on_tab(double) override {
        fixture_player = (fixture_player == PlayerNum::P1) ? PlayerNum::P2 : PlayerNum::P1;
        if (active) active.emplace(fixture_player);
    }
    void update(double ms) override { if (active) { active->handle_input(); active->update(ms); } }
    void draw() override { if (active) active->draw(); }

    std::vector<std::string> debug_lines() override {
        if (!active) return {};
        return {
            std::string("Player:       ") + (active->player_num == PlayerNum::P2 ? "P2" : "P1"),
            std::string("costume_mode: ") + (active->costume_select_mode ? "yes" : "no"),
            std::string("Selected:     ") + std::to_string(active->costume_select_mode ? active->costume_icon_index : active->selected_index),
        };
    }
    std::string controls() override { return "LEFT/RIGHT: cycle  DON on COSTUME: enter icon select  VARIANT: toggle 2P  R/R-CLICK: reset"; }
};
