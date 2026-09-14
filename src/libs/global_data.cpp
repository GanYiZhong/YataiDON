#include "global_data.h"
#include <cstdlib>
#include <unordered_map>
#include "filesystem.h"
#include "texture.h"
#include "script.h"
#include "text.h"
#include "audio.h"

GlobalData global_data;

void load_skin() {
    ensure_skin_extracted(global_data.config->paths.skin.string());
    fs::path root_skin_path = fs::path("Skins") / global_data.config->paths.skin;
    set_skin_graphics_path(root_skin_path / "Graphics");

    tex.init(root_skin_path / "Graphics");
    const bool was_fullscreen = ray::IsWindowFullscreen();
    if (was_fullscreen) ray::ToggleFullscreen();
    ray::SetWindowSize(tex.screen_width, tex.screen_height);
    if (was_fullscreen) ray::ToggleFullscreen();

    global_tex.init(root_skin_path / "Graphics");
    global_tex.load_screen_textures("global");
    script_manager.init(root_skin_path / "Scripts");
    // A skin may ship one font per interface language (Graphics/font_<lang>.ttf, e.g.
    // font_zh.ttf drawn from the Simplified Chinese glyph set) and fall back to font.ttf.
    // The settings screen reloads the skin, so a language change picks up the right file.
    // Looked up by the interface language code first, then by the cabinet family name
    // that language draws from (zh -> cn, ko -> kr, ja -> jp), then font.ttf.
    static const std::unordered_map<std::string, std::string> font_family = {
        {"zh", "cn"}, {"ko", "kr"}, {"ja", "jp"}, {"zh_tw", "tw"}, {"zh-tw", "tw"}, {"zh_cn", "cn"}, {"zh-cn", "cn"},
    };
    // YATAIDON_DUMP_LABELS_LANG=ja: dump the labels as that language would see them
    if (const char* dl = std::getenv("YATAIDON_DUMP_LABELS_LANG"); dl && *dl) global_data.config->general.language = dl;
    const std::string& lang = global_data.config->general.language;
    fs::path font_path = resolve_skin_path("Graphics/font_" + lang + ".ttf");
    if (!fs::exists(font_path) && font_family.count(lang))
        font_path = resolve_skin_path("Graphics/font_" + font_family.at(lang) + ".ttf");
    if (!fs::exists(font_path)) font_path = resolve_skin_path("Graphics/font.ttf");
    font_manager.init(font_path);
    {
        // label face: font_label_<lang>.ttf -> font_label_<family>.ttf -> font_label.ttf -> the main font
        fs::path lp = resolve_skin_path("Graphics/font_label_" + lang + ".ttf");
        if (!fs::exists(lp) && font_family.count(lang)) lp = resolve_skin_path("Graphics/font_label_" + font_family.at(lang) + ".ttf");
        if (!fs::exists(lp)) lp = resolve_skin_path("Graphics/font_label.ttf");
        if (!fs::exists(lp)) lp = font_path;
        label_font_manager.init(lp);
    }
    if (const char* dump = std::getenv("YATAIDON_DUMP_LABELS"); dump && *dump) {
        tex.dump_labels(fs::path(dump));
        std::exit(0);
    }
    audio.init_audio_device(root_skin_path / "Sounds", global_data.config->audio, global_data.config->volume);
}

void unload_skin() {
    tex.unload_textures();
    global_tex.unload_textures();
    script_manager.shutdown();
    font_manager.unload();
    label_font_manager.unload();
    audio.unload_all_sounds();
    audio.unload_all_music();
    audio.close_audio_device();
}

void reset_session() {
    global_data.session_data[1] = SessionData();
    global_data.session_data[2] = SessionData();
}

int get_player_id(PlayerNum player_num) {
    return (player_num == global_data.first_login_player)
        ? global_data.config->general.player_1_id
        : global_data.config->general.player_2_id;
}
