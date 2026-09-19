#include "script.h"

#include <string_view>
#include <cstring>
#include <algorithm>
#include "global_data.h"
#include "text.h"
#include "audio.h"
#include "input.h"
#include "filesystem.h"
#include "webcam.h"
#include "../objects/song_select/file_navigator/box_lua_bindings.h"
#include "../objects/enums.h"
#include <spdlog/spdlog.h>

static std::optional<EaseType> parse_ease_type(const sol::optional<std::string>& ease_str) {
    if (!ease_str) return std::nullopt;
    if (ease_str == "quadratic") return EaseType::Quadratic;
    if (ease_str == "cubic") return EaseType::Cubic;
    if (ease_str == "exponential") return EaseType::Exponential;
    spdlog::error("Unknown ease type: {}", ease_str.value());
    return std::nullopt;
}

static SessionData& current_session() {
    static SessionData fallback{};
    if (global_data.session_data.empty()) return fallback;
    int idx = (int)global_data.player_num;
    if (idx < 0 || idx >= (int)global_data.session_data.size()) idx = 0;
    return global_data.session_data[idx];
}

static DrawTextureParams parse_draw_params_legacy(sol::optional<sol::table> params_table,
                                                  bool allow_blend) {
    DrawTextureParams params;
    if (!params_table) return params;
    sol::table t = params_table.value();

    sol::optional<sol::table> color = t["color"];
    if (color) {
        params.color.r = color.value()[1].get_or(params.color.r);
        params.color.g = color.value()[2].get_or(params.color.g);
        params.color.b = color.value()[3].get_or(params.color.b);
        params.color.a = color.value()[4].get_or(params.color.a);
    }

    params.frame    = t["frame"].get_or(params.frame);
    params.scale    = t["scale"].get_or(params.scale);
    params.center   = t["center"].get_or(params.center);
    params.x        = t["x"].get_or(params.x);
    params.y        = t["y"].get_or(params.y);
    params.x2       = t["x2"].get_or(params.x2);
    params.y2       = t["y2"].get_or(params.y2);
    params.rotation = t["rotation"].get_or(params.rotation);
    params.fade     = t["fade"].get_or(params.fade);
    params.index    = t["index"].get_or(params.index);

    sol::optional<std::string> mirror = t["mirror"];
    if (mirror) params.mirror = mirror_from_string(mirror.value());

    if (allow_blend) {
        sol::optional<std::string> blend = t["blend"];
        if (blend) params.blend = blend_from_string(blend.value());
    }

    sol::optional<sol::table> origin = t["origin"];
    if (origin) {
        params.origin.x = origin.value()[1].get_or(params.origin.x);
        params.origin.y = origin.value()[2].get_or(params.origin.y);
    }

    sol::optional<sol::table> src = t["src"];
    if (src) {
        ray::Rectangle rect;
        rect.x      = src.value()["x"].get_or(0.0f);
        rect.y      = src.value()["y"].get_or(0.0f);
        rect.width  = src.value()["width"].get_or(0.0f);
        rect.height = src.value()["height"].get_or(0.0f);
        params.src  = rect;
    }

    return params;
}

static DrawTextureParams parse_draw_params(sol::optional<sol::table> params_table,
                                            bool allow_blend = true) {
    return parse_draw_params_legacy(params_table, allow_blend);
}

static bool same_params(const DrawTextureParams& a, const DrawTextureParams& b) {
    if (a.color.r != b.color.r || a.color.g != b.color.g ||
        a.color.b != b.color.b || a.color.a != b.color.a) return false;
    if (a.frame != b.frame || a.scale != b.scale || a.center != b.center) return false;
    if (a.mirror != b.mirror) return false;
    if (a.x != b.x || a.y != b.y || a.x2 != b.x2 || a.y2 != b.y2) return false;
    if (a.origin.x != b.origin.x || a.origin.y != b.origin.y) return false;
    if (a.rotation != b.rotation || a.fade != b.fade || a.index != b.index) return false;
    if (a.src.has_value() != b.src.has_value()) return false;
    if (a.src && (a.src->x != b.src->x || a.src->y != b.src->y ||
                  a.src->width != b.src->width || a.src->height != b.src->height))
        return false;
    if (a.blend != b.blend) return false;
    return true;
}

// Index every script under one Scripts folder. Names already present are
// kept: the skin's own scripts are indexed first, and a parent skin only
// fills the gaps, the same way its graphics do.
void ScriptManager::index_scripts(const fs::path& script_path) {
    std::error_code ec;
    fs::directory_iterator dir(script_path, ec);
    if (ec) {
        spdlog::warn("Unable to index scripts in {}: {}", script_path.string(), ec.message());
        return;
    }
    for (const auto& script : dir) {
        fs::path p = script.path();
        if (fs::is_directory(p)) {
            fs::path lua_file = p / (p.stem().string() + ".lua");
            if (fs::exists(lua_file) && !scripts.count(p.stem().string())) {
                scripts[p.stem().string()] = lua_file.string();
            }
            std::error_code sub_ec;
            for (const auto& sub : fs::directory_iterator(p, sub_ec)) {
                fs::path sub_p = sub.path();
                if (!fs::is_directory(sub_p) && sub_p.extension() == ".lua" && sub_p.stem() != p.stem() &&
                    !scripts.count(sub_p.stem().string())) {
                    scripts[sub_p.stem().string()] = sub_p.string();
                }
            }
        } else if (p.extension() == ".lua" && !scripts.count(p.stem().string())) {
            scripts[p.stem().string()] = p.string();
        }
    }
}

void ScriptManager::init(fs::path script_path) {
    lua = std::make_unique<sol::state>();
    lua->open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                        sol::lib::math, sol::lib::table);

    // index_scripts() only inserts a name that isn't already indexed, so a
    // stale entry from the previous skin (or its parent) would otherwise
    // permanently shadow this skin's own script of the same name.
    scripts.clear();

    // A partial skin scripts only some screens and leans on its parent for
    // the rest, exactly like its graphics.
    fs::path parent_scripts;
    if (skin_has_parent())
        parent_scripts = parent_skin_root() / "Scripts";

    std::string skin_scripts_dir = script_path.string();
    std::string package_path = skin_scripts_dir + "/?.lua;" +
                               skin_scripts_dir + "/?/init.lua";
    if (!parent_scripts.empty()) {
        package_path += ";" + parent_scripts.string() + "/?.lua;" +
                        parent_scripts.string() + "/?/init.lua";
    }
    (*lua)["package"]["path"] = package_path;

    index_scripts(script_path);
    if (!parent_scripts.empty()) index_scripts(parent_scripts);

    spdlog::debug("Loaded scripts:");
    for (const auto& [name, path] : scripts) {
        spdlog::debug("  {} -> {}", name, path);
    }
    spdlog::debug("Total scripts: {}", scripts.size());

    tex.init(script_path.parent_path() / "Graphics");

    register_lua_bindings();
}

bool ScriptManager::has_lua_script(const std::string& script_name) const {
    return scripts.count(script_name) != 0;
}

std::string ScriptManager::get_lua_script_path(const std::string& script_name) {
    if (scripts.find(script_name) == scripts.end()) {
        throw std::runtime_error("Unable to find lua script: " + script_name);
    }
    return scripts[script_name];
}

void ScriptManager::shutdown() {
    tex.unload_textures();
    scripts.clear();
    lua.reset();
}

void ScriptManager::register_lua_bindings() {
    sol::state& lua = *this->lua;
    lua.new_usertype<BaseAnimation>("BaseAnimation",
        "update", [](BaseAnimation& self, double t) { self.update(t); return self.attribute; },
        "restart", &BaseAnimation::restart,
        "start", &BaseAnimation::start,
        "pause", &BaseAnimation::pause,
        "unpause", &BaseAnimation::unpause,
        "reset", &BaseAnimation::reset,
        "attribute", &BaseAnimation::attribute,
        "duration", &BaseAnimation::duration,
        "is_finished", &BaseAnimation::is_finished,
        "is_started", &BaseAnimation::is_started,
        "isFinished", &BaseAnimation::isFinished,
        "isStarted", &BaseAnimation::isStarted
    );

    // Fade animation bindings
    lua.new_usertype<FadeAnimation>("FadeAnimation",
        sol::base_classes, sol::bases<BaseAnimation>(),
        "update", [](FadeAnimation& self, double t) { self.update(t); return self.attribute; },
        "restart", &FadeAnimation::restart
    );

    // Move animation bindings
    lua.new_usertype<MoveAnimation>("MoveAnimation",
        sol::base_classes, sol::bases<BaseAnimation>(),
        "update", [](MoveAnimation& self, double t) { self.update(t); return self.attribute; },
        "restart", &MoveAnimation::restart
    );

    // Texture change animation bindings
    lua.new_usertype<TextureChangeAnimation>("TextureChangeAnimation",
        sol::base_classes, sol::bases<BaseAnimation>(),
        "update", [](TextureChangeAnimation& self, double t) { self.update(t); return self.attribute; },
        "reset", &TextureChangeAnimation::reset
    );

    // Text stretch animation bindings
    lua.new_usertype<TextStretchAnimation>("TextStretchAnimation",
        sol::base_classes, sol::bases<BaseAnimation>(),
        "update", [](TextStretchAnimation& self, double t) { self.update(t); return self.attribute; }
    );

    // Texture resize animation bindings
    lua.new_usertype<TextureResizeAnimation>("TextureResizeAnimation",
        sol::base_classes, sol::bases<BaseAnimation>(),
        "update", [](TextureResizeAnimation& self, double t) { self.update(t); return self.attribute; },
        "restart", &TextureResizeAnimation::restart
    );

    // Animation creation helper functions
    sol::table anim = lua.create_table();

    anim.set_function("fade", [](double duration, sol::optional<sol::table> params) -> std::unique_ptr<FadeAnimation> {
        double initial_opacity = 1.0;
        double final_opacity = 0.0;
        double delay = 0.0;
        bool loop = false;
        bool lock_input = false;
        std::optional<EaseType> ease_in = std::nullopt;
        std::optional<EaseType> ease_out = std::nullopt;
        std::optional<double> reverse_delay = std::nullopt;

        if (params) {
            sol::table t = params.value();
            initial_opacity = t["initial_opacity"].get_or(initial_opacity);
            final_opacity = t["final_opacity"].get_or(final_opacity);
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);

            ease_in = parse_ease_type(t["ease_in"]);
            ease_out = parse_ease_type(t["ease_out"]);

            sol::optional<double> reverse_delay_opt = t["reverse_delay"];
            if (reverse_delay_opt) reverse_delay = reverse_delay_opt.value();
        }

        return std::make_unique<FadeAnimation>(duration, initial_opacity, loop, lock_input, final_opacity, delay, ease_in, ease_out, reverse_delay);
    });

    anim.set_function("move", [](double duration, sol::optional<sol::table> params) -> std::unique_ptr<MoveAnimation> {
        int total_distance = 0;
        int start_position = 0;
        double delay = 0.0;
        bool loop = false;
        bool lock_input = false;
        std::optional<double> reverse_delay = std::nullopt;
        std::optional<EaseType> ease_in = std::nullopt;
        std::optional<EaseType> ease_out = std::nullopt;

        if (params) {
            sol::table t = params.value();
            total_distance = t["total_distance"].get_or(total_distance);
            start_position = t["start_position"].get_or(start_position);
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);

            sol::optional<double> reverse_delay_opt = t["reverse_delay"];
            if (reverse_delay_opt) reverse_delay = reverse_delay_opt.value();

            ease_in = parse_ease_type(t["ease_in"]);
            ease_out = parse_ease_type(t["ease_out"]);
        }

        return std::make_unique<MoveAnimation>(duration, total_distance, loop, lock_input, start_position, delay, reverse_delay, ease_in, ease_out);
    });

    anim.set_function("texture_change", [](double duration, sol::table textures_table, sol::optional<sol::table> params) -> std::unique_ptr<TextureChangeAnimation> {
        std::vector<std::tuple<double, double, int>> keyframes;

        for (size_t i = 1; i <= textures_table.size(); ++i) {
            sol::optional<sol::table> entry_opt = textures_table[i];
            if (!entry_opt) continue;
            sol::table tex_entry = entry_opt.value();
            sol::optional<double> start = tex_entry[1];
            sol::optional<double> end   = tex_entry[2];
            sol::optional<int>    index = tex_entry[3];
            if (!start || !end || !index) {
                spdlog::error("anim.texture_change: malformed keyframe at index {}", i);
                continue;
            }
            keyframes.emplace_back(start.value(), end.value(), index.value());
        }

        double delay = 0.0;
        bool loop = false;
        bool lock_input = false;

        if (params) {
            sol::table t = params.value();
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);
        }

        return std::make_unique<TextureChangeAnimation>(duration, keyframes, loop, lock_input, delay);
    });

    anim.set_function("text_stretch", [](double duration, sol::optional<sol::table> params) -> std::unique_ptr<TextStretchAnimation> {
        double delay = 0.0;
        bool loop = false;
        bool lock_input = false;

        if (params) {
            sol::table t = params.value();
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);
        }

        return std::make_unique<TextStretchAnimation>(duration, delay, loop, lock_input);
    });

    anim.set_function("texture_resize", [](double duration, sol::optional<sol::table> params) -> std::unique_ptr<TextureResizeAnimation> {
        double initial_size = 1.0;
        double final_size = 0.0;
        double delay = 0.0;
        bool loop = false;
        bool lock_input = false;
        std::optional<double> reverse_delay = std::nullopt;
        std::optional<EaseType> ease_in = std::nullopt;
        std::optional<EaseType> ease_out = std::nullopt;

        if (params) {
            sol::table t = params.value();
            initial_size = t["initial_size"].get_or(initial_size);
            final_size = t["final_size"].get_or(final_size);
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);

            sol::optional<double> reverse_delay_opt = t["reverse_delay"];
            if (reverse_delay_opt) reverse_delay = reverse_delay_opt.value();

            ease_in = parse_ease_type(t["ease_in"]);
            ease_out = parse_ease_type(t["ease_out"]);
        }

        return std::make_unique<TextureResizeAnimation>(duration, initial_size, loop, lock_input, final_size, delay, reverse_delay, ease_in, ease_out);
    });

    lua["anim"] = anim;

    sol::table tex = lua.create_table();

    tex.set_function("load_animations", [](const std::string& screen_name) {
        script_manager.tex.load_animations(screen_name);
    });

    tex.set_function("get_animation", [](int anim_id, sol::object second_arg) -> BaseAnimation* {
        if (second_arg.get_type() == sol::type::string)
            return script_manager.tex.get_animation(anim_id, second_arg.as<std::string>());
        return script_manager.tex.get_animation(anim_id, false);
    });

    tex.set_function("load_folder", [](const std::string& screen_name, const std::string& subset) {
        script_manager.tex.load_folder(screen_name, subset);
    });

    tex.set_function("unload_folder", [](const std::string& screen_name, const std::string& subset) {
        script_manager.tex.unload_folder(screen_name, subset);
    });

    tex.set_function("current_screen", []() -> std::string {
        return global_data.current_screen;
    });

    tex.set_function("in_transition", []() -> bool {
        return global_data.in_transition;
    });

    tex.set_function("get_screen_width", []() -> float {
        return script_manager.tex.screen_width;
    });

    tex.set_function("get_screen_height", []() -> float {
        return script_manager.tex.screen_height;
    });

    tex.set_function("get_screen_scale", []() -> float {
        return script_manager.tex.screen_scale;
    });

    tex.set_function("get_draw_offset", []() -> std::pair<float, float> {
        return {script_manager.tex.draw_offset_x, script_manager.tex.draw_offset_y};
    });

    tex.set_function("draw_rect", [](float x, float y, float w, float h, int r, int g, int b, int a) {
        auto to_u8 = [](int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); };
        ray::DrawRectangle((int)x, (int)y, (int)w, (int)h, ray::Color{to_u8(r), to_u8(g), to_u8(b), to_u8(a)});
    });

tex.set_function("begin_scissor", [](float x, float y, float w, float h) {
        ray::BeginScissorMode((int)x, (int)y, (int)w, (int)h);
    });

    tex.set_function("end_scissor", []() {
        ray::EndScissorMode();
    });

    tex.set_function("get_skin_config", [](const std::string& config_key) -> sol::optional<sol::table> {
        auto config_it = script_manager.tex.skin_config_by_name.find(config_key);
        if (config_it == script_manager.tex.skin_config_by_name.end()) {
            return sol::nullopt;
        }

        const auto& skin_info = config_it->second;

        sol::table info = script_manager.lua->create_table();
        info["x"] = skin_info.x;
        info["y"] = skin_info.y;
        info["font_size"] = skin_info.font_size;
        info["width"] = skin_info.width;
        info["height"] = skin_info.height;
        info["outline"] = skin_info.outline;

        return info;
    });

    tex.set_function("get_option", [](const std::string& name) -> bool {
        auto it = screen_options_map.find(name);
        if (it == screen_options_map.end()) return false;
        auto o = script_manager.tex.options.find(it->second);
        return o != script_manager.tex.options.end() && o->second;
    });

    tex.set_function("get_texture_keys", [](const std::string& subset) -> sol::optional<sol::table> {
        std::string prefix = subset + "/";
        sol::table keys = script_manager.lua->create_table();
        int index = 1;
        for (const auto& [path, obj] : script_manager.tex.textures) {
            if (path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0) {
                keys[index] = path.substr(prefix.size());
                ++index;
            }
        }
        if (index == 1) return sol::nullopt;
        return keys;
    });

    tex.set_function("get_texture_info", [](const std::string& subset, const std::string& texture_name) -> sol::optional<sol::table> {
        auto tex_it = script_manager.tex.textures.find(subset + "/" + texture_name);
        if (tex_it == script_manager.tex.textures.end()) return sol::nullopt;

        const auto& tex_obj = tex_it->second;

        sol::table info = script_manager.lua->create_table();
        info["name"] = tex_obj->name;
        info["x"] = sol::as_table(tex_obj->x);
        info["y"] = sol::as_table(tex_obj->y);
        info["x2"] = sol::as_table(tex_obj->x2);
        info["y2"] = sol::as_table(tex_obj->y2);
        info["width"] = tex_obj->width;
        info["height"] = tex_obj->height;

        int frame_count = 1;
        if (auto framed = dynamic_cast<FramedTexture*>(tex_obj.get())) {
            frame_count = static_cast<int>(framed->textures.size());
        }
        if (tex_obj->crop_data.has_value()) {
            frame_count = std::max(frame_count, static_cast<int>(tex_obj->crop_data->size()));
        }
        info["frame_count"] = frame_count;

        return info;
    });

    tex.set_function("get_id", [](const std::string& subset, const std::string& texture_name) -> sol::optional<TextureObject*> {
        std::string base = subset + "/" + texture_name;
        if (script_manager.tex.has_texture(base)) return script_manager.tex.get_texture(base);
        for (const auto& v : script_manager.tex.language_variants(base + "_" + global_data.config->general.language)) {
            if (script_manager.tex.has_texture(v)) return script_manager.tex.get_texture(v);
        }
        return sol::nullopt;
    });

    tex.set_function("draw_texture", [](TextureObject* id, sol::optional<sol::table> params_table) {
        script_manager.tex.draw_texture(id, parse_draw_params(params_table));
    });

    tex.set_function("get_texture", [](const std::string& path) -> sol::optional<TextureObject*> {
        // path format: "screen_name/subset/texture_name", e.g. "global/indicator/drum_face"
        auto first_slash = path.find('/');
        if (first_slash == std::string::npos) return sol::nullopt;
        auto second_slash = path.find('/', first_slash + 1);
        if (second_slash == std::string::npos) return sol::nullopt;

        std::string screen_name  = path.substr(0, first_slash);
        std::string subset       = path.substr(first_slash + 1, second_slash - first_slash - 1);
        std::string texture_name = path.substr(second_slash + 1);

        script_manager.tex.load_folder(screen_name, subset);

        // the current language's variant, then _en / _ja, then the plain name
        std::string base = subset + "/" + texture_name;
        for (const auto& v : script_manager.tex.language_variants(base + "_" + global_data.config->general.language)) {
            if (script_manager.tex.has_texture(v)) return script_manager.tex.get_texture(v);
        }
        if (script_manager.tex.has_texture(base)) return script_manager.tex.get_texture(base);
        return sol::nullopt;
    });

    tex.set_function("language", []() { return global_data.config->general.language; });
    // skin_config text for the current interface language: tex.skin_text("entry_game")
    // -> the "text" map's entry for the language, else ja, else en, else the first entry,
    // else "" (a Lua script should never need to carry a string of its own).
    tex.set_function("skin_text", [](const std::string& key, sol::optional<std::string> lang_opt) -> std::string {
        auto it = script_manager.tex.skin_config_by_name.find(key);
        if (it == script_manager.tex.skin_config_by_name.end()) return "";
        const auto& m = it->second.text;
        for (const std::string& l : {lang_opt.value_or(global_data.config->general.language), std::string("ja"), std::string("en")}) {
            auto t = m.find(l);
            if (t != m.end() && !t->second.empty()) return t->second;
        }
        return m.empty() ? std::string() : m.begin()->second;
    });

    tex.set_function("skip_enabled", []() { return global_data.live_skip_count >= 0; });

    lua["tex"] = tex;

    lua.new_usertype<OutlinedText>("OutlinedText",
        "width",          &OutlinedText::width,
        "height",         &OutlinedText::height,
        "is_ready",       &OutlinedText::is_ready,
        "upload_pending", &OutlinedText::upload_pending,
        "finish",         &OutlinedText::finish,
        "draw",           [](OutlinedText& self, sol::optional<sol::table> params_table) {
            DrawTextureParams params = parse_draw_params(params_table, false);
            self.draw(params);
        }
    );

    sol::table text = lua.create_table();
    text.set_function("create_text", [](const std::string& skin_config_key, std::array<int, 4> color,
        std::array<int, 4> outline_color, bool is_vertical, float outline_thickness, float spacing) -> std::unique_ptr<OutlinedText> {
            auto config_it = script_manager.tex.skin_config_by_name.find(skin_config_key);
            if (config_it == script_manager.tex.skin_config_by_name.end()) {
                spdlog::error("Skin config key not found: {}", skin_config_key);
                throw sol::error("Skin config key not found: " + skin_config_key);
            }
            int font_size = config_it->second.font_size;
            std::string text;
            const auto& text_map = config_it->second.text;
            for (const std::string& l : {global_data.config->general.language, std::string("ja"), std::string("en")}) {
                auto t = text_map.find(l);
                if (t != text_map.end() && !t->second.empty()) { text = t->second; break; }
            }
            auto to_u8 = [](int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); };
            ray::Color color_val;
            color_val.r = to_u8(color[0]);
            color_val.g = to_u8(color[1]);
            color_val.b = to_u8(color[2]);
            color_val.a = to_u8(color[3]);
            ray::Color outline_color_val;
            outline_color_val.r = to_u8(outline_color[0]);
            outline_color_val.g = to_u8(outline_color[1]);
            outline_color_val.b = to_u8(outline_color[2]);
            outline_color_val.a = to_u8(outline_color[3]);
            std::unique_ptr<OutlinedText> ptr = std::make_unique<OutlinedText>(text, font_size, color_val, outline_color_val, is_vertical, outline_thickness, spacing);
            ptr->x_offset = config_it->second.x;
            ptr->y_offset = config_it->second.y;
            return ptr;
    });

    text.set_function("create_raw_text", [](const std::string& content, int font_size,
        std::array<int, 4> color, std::array<int, 4> outline_color,
        bool is_vertical, sol::optional<float> thickness, sol::optional<float> spacing)
        -> std::unique_ptr<OutlinedText> {
            auto to_u8 = [](int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); };
            ray::Color c  = { to_u8(color[0]),         to_u8(color[1]),         to_u8(color[2]),         to_u8(color[3]) };
            ray::Color oc = { to_u8(outline_color[0]), to_u8(outline_color[1]), to_u8(outline_color[2]), to_u8(outline_color[3]) };
            return std::make_unique<OutlinedText>(content, font_size, c, oc, is_vertical,
                thickness.value_or(5.0f), spacing.value_or(2.0f));
    });

    lua["text"] = text;

    tex.set_function("get_current_ms", []() -> double {
        return get_current_ms();
    });

    tex.set_function("player_num", []() -> int {
        return (int)global_data.player_num;
    });

    tex.set_function("display_bpm", []() -> bool {
        return global_data.config->general.display_bpm;
    });

    tex.set_function("song_title", []() -> std::string {
        return current_session().song_title;
    });

    tex.set_function("song_subtitle", []() -> std::string {
        return current_session().song_subtitle;
    });

    tex.set_function("song_genre", []() -> int {
        return current_session().genre_index;
    });

    tex.set_function("genre_frame", [](int genre_index) -> int {
        return genre_to_ref_frame((GenreIndex)genre_index);
    });

    tex.set_function("song_number", []() -> int {
        return global_data.songs_played + 1;
    });

    tex.set_function("songs_played", []() -> int {
        return global_data.songs_played;
    });

    tex.set_function("song_limit", []() -> int {
        return global_data.config->general.song_limit;
    });

    tex.set_function("last_input_ms", []() -> double {
        return get_last_input_ms();
    });

    tex.set_function("log", [](const std::string& msg) {
        spdlog::info("[lua] {}", msg);
    });

    sol::table audio_tbl = lua.create_table();

    audio_tbl.set_function("play_sound", [](const std::string& name, sol::optional<std::string> preset_str,
                                            sol::optional<bool> loop) {
        VolumePreset preset = VolumePreset::NONE;
        if (preset_str) {
            const std::string& p = preset_str.value();
            if      (p == "sound")    preset = VolumePreset::SOUND;
            else if (p == "music")    preset = VolumePreset::MUSIC;
            else if (p == "voice")    preset = VolumePreset::VOICE;
            else if (p == "hitsound") preset = VolumePreset::HITSOUND;
        else if (p == "attract_mode") preset = VolumePreset::ATTRACT_MODE;
        }
        audio.set_sound_loop(name, loop.value_or(false));
        audio.play_sound(name, preset);
    });

    audio_tbl.set_function("stop_sound", [](const std::string& name) {
        audio.set_sound_loop(name, false);
        audio.stop_sound(name);
    });

    audio_tbl.set_function("is_sound_playing", [](const std::string& name) -> bool {
        return audio.is_sound_playing(name);
    });

    lua["audio"] = audio_tbl;

    sol::table camera_tbl = lua.create_table();

    camera_tbl.set_function("open", []() -> bool {
        return webcam.open(global_data.config->general.webcam_number);
    });

    camera_tbl.set_function("close", []() {
        webcam.close();
    });

    camera_tbl.set_function("update", []() {
        webcam.update();
    });

    camera_tbl.set_function("is_ready", []() -> bool {
        return webcam.is_ready();
    });

    camera_tbl.set_function("draw", [](float x, float y, float w, float h) {
        if (!webcam.is_ready()) return;
        ray::Rectangle src{0, 0, (float)webcam.width(), (float)webcam.height()};
        ray::Rectangle dst{x, y, w, h};
        ray::DrawTexturePro(webcam.get_texture(), src, dst, ray::Vector2{0, 0}, 0, ray::WHITE);
    });

    lua["camera"] = camera_tbl;

    register_song_select_lua_bindings(lua);
}

ScriptManager script_manager;
