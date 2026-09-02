#pragma once

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
