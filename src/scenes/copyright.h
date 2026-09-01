#pragma once

// ROUND 79 (r79-boot-copyright-screen) -- the cabinet's boot-time COPYRIGHT scene.
//
// Decoded boot order (39.06 is native-only, so the chain comes from the CHN05
// decompile of the same engine, D:\tlb_test_harness\decompiled\src):
//
//   main.obj.c:66..91   SceneBoot is constructed, a SceneDeviceCheck is built,
//                       `SceneBase::CreateNextScece<SceneCopyRight>` is called
//                       on it, and that DeviceCheck is handed to
//                       SceneBoot::NextScene.  So: Boot -> DeviceCheck -> CopyRight.
//   SceneCopyRight.obj.c:265   loads `lua/attract/copyright.lua`
//   SceneCopyRight.obj.c:478/483  Main() -> SceneTournamentScore (tournament) or
//                       **SceneBNLogo** (normal).
//   SceneBNLogo.obj.c:349/355, SceneAttractMovie.obj.c:1940, SceneCaution.obj.c:360/366
//                       BNLogo -> AttractMovie -> Caution -> Title -> ...
//
// i.e. the cabinet shows COPYRIGHT once, on a cold boot, right after the boot /
// notice progress screen -- and BEFORE caution, which is a member of the attract
// LOOP, not of the boot chain.  Our engine's LOADING screen is the boot/notice
// screen (Graphics/loading/kidou/warning = attract/notice's NOTICE board, and its
// closing white flash is `notice_instance_0` depth 6), so COPYRIGHT slots between
// LOADING and TITLE, which is exactly where the user reported it missing.
//
// The clip: `attract/copyright.nulm`, sprite 15 (`copyright_instance_0`), 240
// frames @ 60 fps = 4000 ms; `copyright.lua` is a plain State scene that ends on
// `main_mc_:IsPlay() == false`, so the scene length IS the clip length.
// `lumen_anim_dump --clip copyright_instance_0 --all --leaves --range 0,239`
// shows **not one** tx/ty/sx/sy/rot varying on any of its 25 nodes: the only
// timeline events are at f120, where text_instance_0 alpha steps 1->0,
// text_instance_1 steps 0->1 and the `piapro` shape (char 10) is REMOVEd (raw
// record @5444).  So the whole scene is two flat pages of 2000 ms each, and a
// per-frame `Scripts/anim/` table would carry nothing but constants.
//
// Fail-soft: the screen only runs when the skin declares `copyright_page_ms`
// (this child does; PyTaikoGreen does not), and each page is skipped if its
// texture is missing, so a skin without the art never sits on a black screen.

#include "../libs/screen.h"

class CopyrightScreen : public Screen {
private:
    double start_ms  = 0.0;
    double page_ms   = 2000.0;   // f0..f119 / f120..f239 at 60 fps
    int    pages     = 0;

public:
    CopyrightScreen() : Screen("copyright") {}

    // The cabinet's page duration, stated by the skin.  x = ms per page.
    static double skin_page_ms() {
        if (const SkinInfo* s = tex.skin_entry("copyright_page_ms"))
            if (s->x > 0) return s->x;
        return 0.0;   // 0 = the skin does not have this scene
    }

    void on_screen_start() override {
        Screen::on_screen_start();
        start_ms = get_current_ms();
        double d = skin_page_ms();
        if (d > 0) page_ms = d;
        pages = 0;
        if (tex.has_texture("kenri/page1")) pages++;
        if (tex.has_texture("kenri/page2")) pages++;
        if (pages == 0)
            spdlog::warn("COPYRIGHT: no kenri/page* textures - skipping the scene");
    }

    std::optional<Screens> update() override {
        auto ret = Screen::update();
        if (ret.has_value()) return ret;
        if (pages == 0) return on_screen_end(Screens::TITLE);
        if (get_current_ms() - start_ms >= page_ms * pages)
            return on_screen_end(Screens::TITLE);
        return std::nullopt;
    }

    void draw() override {
        ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::BLACK);
        if (pages == 0) return;
        int page = static_cast<int>((get_current_ms() - start_ms) / page_ms);
        if (page < 0) page = 0;
        if (page >= pages) page = pages - 1;
        tex.draw_texture(page == 0 ? KENRI::PAGE1 : KENRI::PAGE2);
    }
};
