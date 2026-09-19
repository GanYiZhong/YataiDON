#pragma once

#include "box_base.h"

class BackBox : public BaseBox {
public:
    static constexpr ray::Color COLOR = ray::Color(170, 115, 35);

    BackBox(const fs::path& path, const BoxDef& box_def);

    const char* lua_kind() const override { return "back"; }

protected:
    TextureObject* t_back_text = nullptr;
    TextureObject* t_back_text_highlight = nullptr;
    TextureObject* t_back_graphic = nullptr;

    void draw_closed() override;
    void draw_open() override;
    void load_textures() override;
};
