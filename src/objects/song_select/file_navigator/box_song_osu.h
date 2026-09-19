#pragma once

#include "box_song.h"

class SongBoxOsu : public SongBox {
public:
    using SongBox::SongBox;
    SongBoxOsu(const fs::path& path, const BoxDef& box_def, SongParser parser);

protected:
    void draw_closed() override;
    void draw_open() override;
    void load_textures() override;

    TextureObject* t_favorite_1p = nullptr;
    TextureObject* t_favorite_2p = nullptr;
};
