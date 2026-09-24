#include "global_data.h"
#include <unordered_map>
#include "filesystem.h"
#include "texture.h"
#include "script.h"
#include "text.h"
#include "audio.h"
#include "../objects/global/debug_menu.h"
#include <spdlog/spdlog.h>

GlobalData global_data;

void load_skin() {
    if (!global_data.config) {
        spdlog::error("load_skin() called before config was initialized");
        return;
    }
    unload_skin();
    ensure_skin_extracted(global_data.config->paths.skin.string());
    fs::path root_skin_path = fs::path("Skins") / global_data.config->paths.skin;
    set_skin_graphics_path(root_skin_path / "Graphics");

    tex.init(root_skin_path / "Graphics");
#ifndef YATAIDON_PLATFORM_IOS
    const bool was_fullscreen = ray::IsWindowFullscreen();
    if (was_fullscreen) ray::ToggleFullscreen();
    ray::SetWindowSize(tex.screen_width, tex.screen_height);
    if (was_fullscreen) ray::ToggleFullscreen();
#endif

    global_tex.init(root_skin_path / "Graphics");
    global_tex.load_screen_textures("global");
    script_manager.init(root_skin_path / "Scripts");
    static const std::unordered_map<std::string, std::string> font_family = {
        {"zh", "cn"}, {"ko", "kr"}, {"ja", "jp"}, {"zh_tw", "tw"}, {"zh-tw", "tw"}, {"zh_cn", "cn"}, {"zh-cn", "cn"},
    };
    const std::string& lang = global_data.config->general.language;
    fs::path font_path = resolve_skin_path("Fonts/font_" + lang + ".ttf");
    if (!fs::exists(font_path) && font_family.count(lang))
        font_path = resolve_skin_path("Fonts/font_" + font_family.at(lang) + ".ttf");
    if (!fs::exists(font_path)) font_path = resolve_skin_path("Fonts/font.ttf");
    if (!fs::exists(font_path))
        spdlog::error("No skin font found (tried font_{}.ttf and font.ttf) in {}", lang, root_skin_path.string());
    font_manager.init(font_path);
    audio.init_audio_device(root_skin_path / "Sounds", global_data.config->audio, global_data.config->volume);
    debug_menu.load_fonts();
}

void unload_skin() {
    debug_menu.clear_selection();
    debug_menu.unload_fonts();
    tex.unload_textures();
    global_tex.unload_textures();
    script_manager.shutdown();
    font_manager.unload();
    audio.unload_all_sounds();
    audio.unload_all_music();
    audio.close_audio_device();
}

void reset_session() {
    global_data.session_data[1] = SessionData();
    global_data.session_data[2] = SessionData();
}

int get_player_id(PlayerNum player_num) {
    if (!global_data.config) {
        spdlog::error("get_player_id() called before config was initialized");
        return 0;
    }
    return (player_num == global_data.first_login_player)
        ? global_data.config->general.player_1_id
        : global_data.config->general.player_2_id;
}
