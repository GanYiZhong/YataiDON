#pragma once

#include "box_base.h"
#include "../../../libs/global_data.h"

class FolderBox : public BaseBox {
public:
    int tja_count;
    bool is_osu_folder = false;
    std::map<int, Crown> crown;
    std::map<int, Crown> crown_p2;
    bool entered = false;
    bool genre_voice_started = false;
    std::unique_ptr<FadeAnimation> enter_fade;
    std::optional<ray::Texture> box_texture;

    std::unique_ptr<OutlinedText> hori_name;
    std::unique_ptr<OutlinedText> tja_count_text;

    FolderBox(const fs::path& path, const BoxDef& box_def, std::map<std::pair<std::string, std::string>, fs::path>& song_files);
    ~FolderBox() override;

    void load_text() override;
    void update(double current_time) override;

    void enter_box() override;
    void exit_box() override;

    void refresh_scores(std::map<std::pair<std::string, std::string>, fs::path>& song_files);
    static void run_deferred_scans(std::atomic<bool>& abort_flag);
    bool scan_pending = false;
    static void invalidate_scan_cache();

    const char* lua_kind() const override { return "folder"; }

protected:
    // Textures resolved once in the constructor, after the song-select screen's
    // load_screen_textures() has already run, instead of calling tex.get_texture()
    // every frame from draw_closed()/draw_open_bg()/draw_open_fg().
    // t_shadow_*/t_folder_texture*/t_genre_overlay/t_diff_overlay come from BaseBox -
    // same texture names, already resolved there, no need to shadow them here.
    TextureObject* t_folder_clip = nullptr;
    TextureObject* t_crown_dfc = nullptr;
    TextureObject* t_crown_fc = nullptr;
    TextureObject* t_crown_clear = nullptr;
    TextureObject* t_folder_top_edge = nullptr;
    TextureObject* t_folder_top = nullptr;
    TextureObject* t_genre_overlay_large = nullptr;
    TextureObject* t_diff_overlay_large = nullptr;
    TextureObject* t_song_count_back = nullptr;
    TextureObject* t_song_count_num = nullptr;
    TextureObject* t_song_count_songs = nullptr;
    TextureObject* t_folder_graphic = nullptr;
    TextureObject* t_folder_text = nullptr;
    void load_textures() override;
    void draw_open_bg(float fade);
    void draw_open_fg(float fade);
    void draw_closed() override;
    void draw_open() override;
};
