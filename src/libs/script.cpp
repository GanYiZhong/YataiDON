#include "script.h"

#include <string_view>
#include <cstring>
#include "perf.h"
#include "global_data.h"
#include "text.h"
#include "audio.h"
#include "input.h"
#include "../objects/song_select/file_navigator/box_lua_bindings.h"
#include "../objects/enums.h"
#include <spdlog/spdlog.h>
#include <cstdlib>   // r56: std::getenv (tex.get_env)

// The session slot the running screen is about to play / has just played, i.e.
// exactly what game.cpp and result.cpp index. Clamped so an out-of-range
// PlayerNum (TWO_PLAYER and up) can never read past the vector.
static SessionData& current_session() {
    int idx = (int)global_data.player_num;
    if (idx < 0 || idx >= (int)global_data.session_data.size()) idx = 0;
    return global_data.session_data[idx];
}

// Round-14 perf: this used to do one sol `t["key"]` lookup for every one of
// the fifteen parameters, whether the table held them or not. On song select
// that was ~8% of the whole frame (profile: 143/1501 samples inside the
// `tex.draw_texture` binding, of which only 15% was the actual draw), because a
// typical skin call passes two or three keys and paid for fifteen hash lookups
// plus fifteen sol optional-traversals.
//
// Now the table is walked once and each key dispatched by name, so the cost is
// proportional to what the skin actually passed. Every value still goes through
// the same `sol::optional<T>` conversion `get_or` used, so an unconvertible
// value is still ignored and leaves the default in place: identical behaviour,
// identical pixels.
//
// `allow_blend` exists because `text.draw` never honoured `blend`; making it do
// so would be a behaviour change, not an optimisation.
// Read helpers over the raw Lua stack. They mirror what sol's `get_or<T>` does
// (a number check that also accepts a numeric string, a strict boolean check, a
// strict string check) without building a sol::object -- which is the whole
// point, because sol::object means a registry ref per value.
static inline bool lua_num_at(lua_State* L, int idx, double& out) {
    int isnum = 0;
    double d = lua_tonumberx(L, idx, &isnum);
    if (!isnum) return false;
    out = d;
    return true;
}

static inline bool lua_field_num(lua_State* L, int tbl, const char* key, double& out) {
    lua_getfield(L, tbl, key);
    bool ok = lua_num_at(L, -1, out);
    lua_pop(L, 1);
    return ok;
}

static inline bool lua_index_num(lua_State* L, int tbl, int i, double& out) {
    lua_geti(L, tbl, i);
    bool ok = lua_num_at(L, -1, out);
    lua_pop(L, 1);
    return ok;
}

// One key of a draw-params table. `vi` is the value's absolute stack index.
static void apply_draw_param(lua_State* L, DrawTextureParams& params,
                             const char* k, size_t klen, int vi, bool allow_blend) {
    double d = 0.0;
    switch (klen) {
    case 1:
        if (k[0] == 'x') { if (lua_num_at(L, vi, d)) params.x = (float)d; }
        else if (k[0] == 'y') { if (lua_num_at(L, vi, d)) params.y = (float)d; }
        return;
    case 2:
        if (k[0] == 'x' && k[1] == '2') { if (lua_num_at(L, vi, d)) params.x2 = (float)d; }
        else if (k[0] == 'y' && k[1] == '2') { if (lua_num_at(L, vi, d)) params.y2 = (float)d; }
        return;
    case 3:
        if (std::memcmp(k, "src", 3) == 0 && lua_type(L, vi) == LUA_TTABLE) {
            ray::Rectangle rect{0, 0, 0, 0};
            if (lua_field_num(L, vi, "x", d))      rect.x      = (float)d;
            if (lua_field_num(L, vi, "y", d))      rect.y      = (float)d;
            if (lua_field_num(L, vi, "width", d))  rect.width  = (float)d;
            if (lua_field_num(L, vi, "height", d)) rect.height = (float)d;
            params.src = rect;
        }
        return;
    case 4:
        if (std::memcmp(k, "fade", 4) == 0) { if (lua_num_at(L, vi, d)) params.fade = d; }
        return;
    case 5:
        if (std::memcmp(k, "frame", 5) == 0) { if (lua_num_at(L, vi, d)) params.frame = (int)d; }
        else if (std::memcmp(k, "scale", 5) == 0) { if (lua_num_at(L, vi, d)) params.scale = (float)d; }
        else if (std::memcmp(k, "index", 5) == 0) { if (lua_num_at(L, vi, d)) params.index = (int)d; }
        else if (std::memcmp(k, "color", 5) == 0) {
            if (lua_type(L, vi) == LUA_TTABLE) {
                if (lua_index_num(L, vi, 1, d)) params.color.r = (unsigned char)d;
                if (lua_index_num(L, vi, 2, d)) params.color.g = (unsigned char)d;
                if (lua_index_num(L, vi, 3, d)) params.color.b = (unsigned char)d;
                if (lua_index_num(L, vi, 4, d)) params.color.a = (unsigned char)d;
            }
        }
        else if (std::memcmp(k, "blend", 5) == 0 && allow_blend) {
            // blend = "additive" | "multiplied" | "add_colors" |
            //         "subtract_colors" | "alpha_premultiply" | "alpha".
            // Omitted = unchanged (default).
            if (lua_type(L, vi) == LUA_TSTRING)
                params.blend = blend_from_string(lua_tostring(L, vi));
        }
        return;
    case 6:
        if (std::memcmp(k, "center", 6) == 0) {
            if (lua_type(L, vi) == LUA_TBOOLEAN) params.center = lua_toboolean(L, vi) != 0;
        }
        else if (std::memcmp(k, "mirror", 6) == 0) {
            if (lua_type(L, vi) == LUA_TSTRING)
                params.mirror = mirror_from_string(lua_tostring(L, vi));
        }
        else if (std::memcmp(k, "origin", 6) == 0) {
            if (lua_type(L, vi) == LUA_TTABLE) {
                if (lua_index_num(L, vi, 1, d)) params.origin.x = (float)d;
                if (lua_index_num(L, vi, 2, d)) params.origin.y = (float)d;
            }
        }
        return;
    case 8:
        if (std::memcmp(k, "rotation", 8) == 0) { if (lua_num_at(L, vi, d)) params.rotation = (float)d; }
        return;
    default:
        return;
    }
}

// Round-14 perf: the original parser did one sol `t["key"]` lookup for every
// one of the fifteen parameters whether the table held it or not. A typical
// skin call passes two or three keys, so twelve of the fifteen lookups were
// pure overhead, and every one of them went through sol's optional machinery.
//
// This walks the table once with lua_next and dispatches on the key, so the
// cost is proportional to what the skin actually passed. The values are read
// straight off the Lua stack: no sol::object, hence no registry ref per value
// (an earlier sol-iterator version of this was a wash for exactly that reason).
static DrawTextureParams parse_draw_params_fast(sol::optional<sol::table> params_table,
                                                bool allow_blend) {
    DrawTextureParams params;
    if (!params_table) return params;

    lua_State* L = params_table->lua_state();
    params_table->push();
    const int ti = lua_gettop(L);

    lua_pushnil(L);
    while (lua_next(L, ti)) {
        // key at ti+1, value at ti+2. Only string keys mean anything here, and
        // lua_tolstring must never be called on a non-string key: it would
        // rewrite the key in place and break lua_next.
        if (lua_type(L, ti + 1) == LUA_TSTRING) {
            size_t klen = 0;
            const char* k = lua_tolstring(L, ti + 1, &klen);
            apply_draw_param(L, params, k, klen, ti + 2, allow_blend);
        }
        lua_pop(L, 1);          // drop the value, keep the key for lua_next
    }

    lua_settop(L, ti - 1);      // drop the table
    return params;
}

// The original implementation, kept so the two can be compared at runtime:
// `paramsmode legacy` restores the old behaviour exactly and `paramsmode check`
// runs both on every call and counts differences. See scratchpad/r14pf.
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

static DrawTextureParams parse_draw_params(sol::optional<sol::table> params_table,
                                           bool allow_blend = true) {
    switch (perf::params_mode()) {
    case perf::PARAMS_LEGACY:
        return parse_draw_params_legacy(params_table, allow_blend);
    case perf::PARAMS_CHECK: {
        DrawTextureParams fast   = parse_draw_params_fast(params_table, allow_blend);
        DrawTextureParams legacy = parse_draw_params_legacy(params_table, allow_blend);
        perf::params_checked();
        if (!same_params(fast, legacy)) perf::params_mismatch();
        return legacy;
    }
    default:
        return parse_draw_params_fast(params_table, allow_blend);
    }
}

// Index every script under one Scripts folder. Names already present are
// kept: the skin's own scripts are indexed first, and a parent skin only
// fills the gaps, the same way its graphics do.
void ScriptManager::index_scripts(const fs::path& script_path) {
    std::error_code ec;
    for (const auto& script : fs::directory_iterator(script_path, ec)) {
        fs::path p = script.path();
        if (fs::is_directory(p)) {
            fs::path lua_file = p / (p.stem().string() + ".lua");
            if (fs::exists(lua_file) && !scripts.count(p.stem().string())) {
                scripts[p.stem().string()] = lua_file.string();
            }
            for (const auto& sub : fs::directory_iterator(p)) {
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

    // A partial skin scripts only some screens and leans on its parent for
    // the rest, exactly like its graphics.
    fs::path parent_scripts;
    if (global_tex.has_parent_skin())
        parent_scripts = global_tex.parent_root() / "Scripts";

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
    script_manager.tex.unload_textures();
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
        std::optional<std::string> ease_in = std::nullopt;
        std::optional<std::string> ease_out = std::nullopt;
        std::optional<double> reverse_delay = std::nullopt;

        if (params) {
            sol::table t = params.value();
            initial_opacity = t["initial_opacity"].get_or(initial_opacity);
            final_opacity = t["final_opacity"].get_or(final_opacity);
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);

            sol::optional<std::string> ease_in_opt = t["ease_in"];
            if (ease_in_opt) ease_in = ease_in_opt.value();

            sol::optional<std::string> ease_out_opt = t["ease_out"];
            if (ease_out_opt) ease_out = ease_out_opt.value();

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
        std::optional<std::string> ease_in = std::nullopt;
        std::optional<std::string> ease_out = std::nullopt;

        if (params) {
            sol::table t = params.value();
            total_distance = t["total_distance"].get_or(total_distance);
            start_position = t["start_position"].get_or(start_position);
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);

            sol::optional<double> reverse_delay_opt = t["reverse_delay"];
            if (reverse_delay_opt) reverse_delay = reverse_delay_opt.value();

            sol::optional<std::string> ease_in_opt = t["ease_in"];
            if (ease_in_opt) ease_in = ease_in_opt.value();

            sol::optional<std::string> ease_out_opt = t["ease_out"];
            if (ease_out_opt) ease_out = ease_out_opt.value();
        }

        return std::make_unique<MoveAnimation>(duration, total_distance, loop, lock_input, start_position, delay, reverse_delay, ease_in, ease_out);
    });

    anim.set_function("texture_change", [](double duration, sol::table textures_table, sol::optional<sol::table> params) -> std::unique_ptr<TextureChangeAnimation> {
        std::vector<std::tuple<double, double, int>> keyframes;

        for (size_t i = 1; i <= textures_table.size(); ++i) {
            sol::table tex_entry = textures_table[i];
            double start = tex_entry[1].get<double>();
            double end = tex_entry[2].get<double>();
            int index = tex_entry[3].get<int>();
            keyframes.emplace_back(start, end, index);
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
        std::optional<std::string> ease_in = std::nullopt;
        std::optional<std::string> ease_out = std::nullopt;

        if (params) {
            sol::table t = params.value();
            initial_size = t["initial_size"].get_or(initial_size);
            final_size = t["final_size"].get_or(final_size);
            delay = t["delay"].get_or(delay);
            loop = t["loop"].get_or(loop);
            lock_input = t["lock_input"].get_or(lock_input);

            sol::optional<double> reverse_delay_opt = t["reverse_delay"];
            if (reverse_delay_opt) reverse_delay = reverse_delay_opt.value();

            sol::optional<std::string> ease_in_opt = t["ease_in"];
            if (ease_in_opt) ease_in = ease_in_opt.value();

            sol::optional<std::string> ease_out_opt = t["ease_out"];
            if (ease_out_opt) ease_out = ease_out_opt.value();
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

        return info;
    });

    tex.set_function("get_texture_keys", [](const std::string& subset) -> sol::optional<sol::table> {
        std::string prefix = subset + "/";
        sol::table keys = script_manager.lua->create_table();
        int index = 1;
        for (const auto& [path, id] : tex_id_map) {
            if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix) {
                if (script_manager.tex.textures.find(id) != script_manager.tex.textures.end()) {
                    keys[index] = path.substr(prefix.size());
                    ++index;
                }
            }
        }
        if (index == 1) return sol::nullopt;
        return keys;
    });

    tex.set_function("get_texture_info", [](const std::string& subset, const std::string& texture_name) -> sol::optional<sol::table> {
        auto it = tex_id_map.find(subset + "/" + texture_name);
        if (it == tex_id_map.end()) return sol::nullopt;

        auto tex_it = script_manager.tex.textures.find(it->second);
        if (tex_it == script_manager.tex.textures.end()) return sol::nullopt;

        const auto& tex_obj = tex_it->second;

        sol::table info = script_manager.lua->create_table();
        info["name"] = tex_obj->name;
        info["x"] = sol::as_table(tex_obj->x);
        info["y"] = sol::as_table(tex_obj->y);
        // Drawn size per position, which is what draw_texture uses. This
        // differs from width/height (the source image size) whenever
        // texture.json stretches the texture, e.g. a nine-slice centre
        // piece - scripts need it to lay out against what is on screen.
        info["x2"] = sol::as_table(tex_obj->x2);
        info["y2"] = sol::as_table(tex_obj->y2);
        info["width"] = tex_obj->width;
        info["height"] = tex_obj->height;

        // ROUND 62 (r62-nameplate-fix): a texture is multi-frame in TWO ways and
        // this only ever reported one of them.
        //   * `FramedTexture` - one image FILE per frame (`0.png`, `1.png`, ...),
        //   * `crop_data`     - one image file cut into frames by texture.json's
        //                       `crop` rectangle list (`read_tex_obj_data`).
        // `draw_texture{frame = N}` indexes EITHER (`texture.cpp` draw path reads
        // `crop_data->at(params.frame)` when it is present), but `frame_count`
        // only ever asked the `FramedTexture` branch, so every crop sheet reported
        // **1** - and a skin that sized a loop or a bounds check off this value
        // silently drew nothing.
        // That is not hypothetical: `Scripts/global/nameplate.lua` clamped the
        // 段位 chip with `dan < frame_count` against `nameplate/dan_emblem`, a
        // 25-rect crop sheet, so `has_dan` was false for every rank except 初級
        // and the dan chip never appeared on ANY screen (measured live this round
        // on a dan=24 player: `frames=1 has_dan=false`).
        // `Scripts/entry/entry.lua:129-134` documents the same trap for
        // `indicator/background` (a 325-rect sheet) as a "do NOT gate on this" note.
        // Reporting the true count fixes the class rather than the instance.
        // Blast radius, enumerated: the only `frame_count` consumers in either
        // shipped skin are `background/bg_objects/renda.lua`, `.../dancer.lua`,
        // `.../chibi/base.lua` (all real `FramedTexture`s - unaffected),
        // `global/nameplate.lua` (the bug above) and `global/indicator.lua`, which
        // stores the value in `self.frame_count` and never reads it.
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

    tex.set_function("get_id", [](const std::string& subset, const std::string& texture_name) -> sol::optional<uint32_t> {
        auto it = tex_id_map.find(subset + "/" + texture_name);
        if (it != tex_id_map.end()) return it->second;
        it = tex_id_map.find(subset + "/" + texture_name + "_" + global_data.config->general.language);
        if (it != tex_id_map.end()) return it->second;
        return std::nullopt;
    });

    tex.set_function("draw_texture", [](uint32_t id, sol::optional<sol::table> params_table) {
        script_manager.tex.draw_texture(id, parse_draw_params(params_table));
    });

    tex.set_function("load_texture", [](const std::string& path) -> sol::optional<uint32_t> {
        // path format: "screen_name/subset/texture_name", e.g. "global/indicator/drum_face"
        auto first_slash = path.find('/');
        if (first_slash == std::string::npos) return sol::nullopt;
        auto second_slash = path.find('/', first_slash + 1);
        if (second_slash == std::string::npos) return sol::nullopt;

        std::string screen_name  = path.substr(0, first_slash);
        std::string subset       = path.substr(first_slash + 1, second_slash - first_slash - 1);
        std::string texture_name = path.substr(second_slash + 1);

        script_manager.tex.load_folder(screen_name, subset);

        // Look for the localised variant first. Texture ids are keyed by
        // subset, not by screen, so an unrelated screen using the same
        // subset name can own the unsuffixed id - result/background asking
        // for "result_text" resolved to dan_result's, which isn't loaded
        // here, and the header silently didn't draw.
        auto it = tex_id_map.find(subset + "/" + texture_name + "_" + global_data.config->general.language);
        if (it != tex_id_map.end()) return static_cast<uint32_t>(it->second);
        it = tex_id_map.find(subset + "/" + texture_name);
        if (it != tex_id_map.end()) return static_cast<uint32_t>(it->second);
        return sol::nullopt;
    });

    // ROUND 15: the configured UI language ("ja" / "en" / "zh" / ...).  Texture
    // lookups already fall back to a `_<language>` variant, but a skin that draws
    // text the arcade keeps in its own word table (the sort window headers) had no
    // way to know which column to read.
    tex.set_function("language", []() { return global_data.config->general.language; });

    // 演奏スキップ: true while the feature is ARMED for the enso that is loading
    // or running -- i.e. the skin declares the option row, a player has it on,
    // and there is a free drum (so it is false for a 2P enso, matching the
    // arcade's greyed row). GameScreen::init_skip publishes it; -1 = not armed.
    // The cabinet shows a 「演奏スキップON」 badge on the song-loading screen off
    // exactly this state (39.06 loading/loading_song.nulm, mc `enso_skip_on`).
    tex.set_function("skip_enabled", []() { return global_data.live_skip_count >= 0; });

    lua["tex"] = tex;

    lua.new_usertype<OutlinedText>("OutlinedText",
        "width",          &OutlinedText::width,
        "height",         &OutlinedText::height,
        "is_ready",       &OutlinedText::is_ready,
        "upload_pending", &OutlinedText::upload_pending,
        "finish",         &OutlinedText::finish,
        "draw",           [](OutlinedText& self, sol::optional<sol::table> params_table) {
            // Same single-pass parse as tex.draw_texture. `blend` stays
            // unsupported here because it always was.
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
                return nullptr;
            }
            int font_size = config_it->second.font_size;
            std::string text = config_it->second.text[global_data.config->general.language];
            ray::Color color_val;
            color_val.r = color[0];
            color_val.g = color[1];
            color_val.b = color[2];
            color_val.a = color[3];
            ray::Color outline_color_val;
            outline_color_val.r = outline_color[0];
            outline_color_val.g = outline_color[1];
            outline_color_val.b = outline_color[2];
            outline_color_val.a = outline_color[3];
            std::unique_ptr<OutlinedText> ptr = std::make_unique<OutlinedText>(text, font_size, color_val, outline_color_val, is_vertical, outline_thickness, spacing);
            ptr->x_offset = config_it->second.x;
            ptr->y_offset = config_it->second.y;
            return ptr;
    });

    text.set_function("create_raw_text", [](const std::string& content, int font_size,
        std::array<int, 4> color, std::array<int, 4> outline_color,
        bool is_vertical, sol::optional<float> thickness, sol::optional<float> spacing)
        -> std::unique_ptr<OutlinedText> {
            ray::Color c  = { (uint8_t)color[0],         (uint8_t)color[1],         (uint8_t)color[2],         (uint8_t)color[3] };
            ray::Color oc = { (uint8_t)outline_color[0], (uint8_t)outline_color[1], (uint8_t)outline_color[2], (uint8_t)outline_color[3] };
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

    // --- Song info of the session slot the current screen belongs to ---------
    // Same data SongInfo (GAME) and result.cpp already draw, handed to Lua as
    // plain values so a skin can lay the title out itself (centred boxes, its
    // own font, its own plate) on screens where no box object exists.
    tex.set_function("song_title", []() -> std::string {
        return current_session().song_title;
    });

    tex.set_function("song_subtitle", []() -> std::string {
        return current_session().song_subtitle;
    });

    // Raw GenreIndex of the song (0..14). tex.genre_frame(g) maps it to the
    // skin's genre reference frame, the same numbering BaseBox:genre_frame()
    // already returns.
    tex.set_function("song_genre", []() -> int {
        return current_session().genre_index;
    });

    tex.set_function("genre_frame", [](int genre_index) -> int {
        return genre_to_ref_frame((GenreIndex)genre_index);
    });

    // 1-based counter the arcade prints as 「N曲目」; matches the value
    // game.cpp hands SongInfo.
    tex.set_function("song_number", []() -> int {
        return global_data.songs_played + 1;
    });

    tex.set_function("songs_played", []() -> int {
        return global_data.songs_played;
    });

    // general.song_limit — the M in 「N曲目 / M曲」.
    tex.set_function("song_limit", []() -> int {
        return global_data.config->general.song_limit;
    });

    // Engine clock (same base as tex.get_current_ms()) of the most recent
    // key / pad / drum press, or 0 if nothing has been pressed yet. Lets a
    // skin implement idle behaviour (attract-style control guides) without an
    // input binding that would let Lua consume events.
    tex.set_function("last_input_ms", []() -> double {
        return get_last_input_ms();
    });

    // ROUND 56 (r56-result-songselect-polish): skin Lua runs without sol::lib::os,
    // so the YATAIDON_* A/B env gates the engine already honours were unreadable
    // from scripts.  get_env exposes exactly that (read-only), log routes a skin
    // message into the engine log (spdlog), which live verification probes read.
    tex.set_function("get_env", [](const std::string& name) -> sol::optional<std::string> {
        const char* v = std::getenv(name.c_str());
        if (!v) return sol::nullopt;
        return std::string(v);
    });
    tex.set_function("log", [](const std::string& msg) {
        spdlog::info("[lua] {}", msg);
    });

    sol::table audio_tbl = lua.create_table();

    // ROUND 52 (r52-lua-divergence-fixes): optional 3rd arg `loop` -- the
    // arcade result count_up_loop_* SE loops until StopSlot.  The flag is set
    // (or cleared) on every play so a sample never inherits a stale loop.
    audio_tbl.set_function("play_sound", [](const std::string& name, sol::optional<std::string> preset_str,
                                            sol::optional<bool> loop) {
        VolumePreset preset = VolumePreset::NONE;
        if (preset_str) {
            const std::string& p = preset_str.value();
            if      (p == "sound")    preset = VolumePreset::SOUND;
            else if (p == "music")    preset = VolumePreset::MUSIC;
            else if (p == "voice")    preset = VolumePreset::VOICE;
            else if (p == "hitsound") preset = VolumePreset::HITSOUND;
        }
        audio.set_sound_loop(name, loop.value_or(false));
        audio.play_sound(name, preset);
    });

    // ROUND 52: the arcade's TSound:StopSlot / voice pre-empt equivalent
    // (result count-up loop cut, common/Timer.lua's stop-previous-voice).
    audio_tbl.set_function("stop_sound", [](const std::string& name) {
        audio.set_sound_loop(name, false);
        audio.stop_sound(name);
    });

    audio_tbl.set_function("is_sound_playing", [](const std::string& name) -> bool {
        return audio.is_sound_playing(name);
    });

    lua["audio"] = audio_tbl;

    register_song_select_lua_bindings(lua);
}

ScriptManager script_manager;
