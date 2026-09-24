#pragma once

#include "box_base.h"

class BackBox : public BaseBox {
public:
    static constexpr ray::Color COLOR = ray::Color(170, 115, 35);

    BackBox(const fs::path& path, const BoxDef& box_def);

    const char* lua_kind() const override { return "back"; }

    void load_text() override;

protected:
    TextureObject* t_back_icon = nullptr;
    TextureObject* t_back_icon_highlight = nullptr;
    TextureObject* t_back_graphic = nullptr;

    std::unique_ptr<OutlinedText> back_text;
    std::unique_ptr<OutlinedText> back_text_highlight;

    void draw_closed() override;
    void draw_open() override;
    void load_textures() override;
};
