#include "box_lua_bindings.h"
#include "box_song.h"
#include "box_folder.h"
#include "box_back.h"
#include "navigator.h"
#include "../player.h"
#include "../../../libs/script.h"

void register_song_select_lua_bindings(sol::state& lua) {
    lua.new_usertype<BaseBox>("BaseBox",
        "box_x",           &BaseBox::box_x,
        "box_y",           &BaseBox::box_y,
        "fade",            &BaseBox::fade,
        "open_fade",       &BaseBox::open_fade,
        "open_anim",       &BaseBox::open_anim,
        "bar_anime_count", &BaseBox::bar_anime_count,
        "draw_state",      &BaseBox::draw_state,
        "lua_kind",        &BaseBox::lua_kind,
        "text_name",       &BaseBox::text_name,
        "is_new",          &BaseBox::is_new,
        "genre_frame", [](BaseBox& self) { return genre_to_ref_frame(self.genre_index); },
        "fore_color", [](BaseBox& self) -> sol::object {
            if (!self.fore_color.has_value()) return sol::lua_nil;
            sol::table t = script_manager.lua->create_table();
            const ray::Color& c = self.fore_color.value();
            t["r"] = c.r; t["g"] = c.g; t["b"] = c.b; t["a"] = c.a;
            return t;
        },
        "name",       &BaseBox::horizontal_name,
        "name_large", &BaseBox::horizontal_name_large
    );

    lua.new_usertype<SongBox>("SongBox",
        sol::base_classes, sol::bases<BaseBox>(),
        "subtitle",       &SongBox::horizontal_subtitle,
        "subtitle_large", &SongBox::horizontal_subtitle_large,
        "bpm_text",     [](SongBox& self) { return self.bpm_text.get(); },
        "is_ura",       &SongBox::is_ura,
        "is_favorite",  &SongBox::is_favorite,
        "diff_fade_in", &SongBox::diff_fade_in,
        "has_ura",      &SongBox::has_ura,
        "ex_data_flag", &SongBox::ex_data_flag,
        "course_info", [](SongBox& self, int diff) {
            auto info = self.course_info(diff);
            sol::table t = script_manager.lua->create_table();
            t["has_course"]   = info.has_course;
            t["level"]        = info.level;
            t["is_branching"] = info.is_branching;
            t["crown"]        = info.crown;
            t["rank"]         = info.rank;
            return t;
        }
    );

    lua.new_usertype<FolderBox>("FolderBox",
        sol::base_classes, sol::bases<BaseBox>(),
        "tja_count",      &FolderBox::tja_count,
        "has_box_texture", [](FolderBox& self) { return self.box_texture.has_value(); },
        "draw_box_texture", [](FolderBox& self, sol::table params) {
            if (!self.box_texture.has_value()) return;
            float s      = tex.screen_scale;
            float scale  = params.get_or("scale", 1.0f);
            float x      = params.get_or("x", 0.0f);
            float y      = params.get_or("y", 0.0f);
            float fade   = params.get_or("fade", 1.0f);
            float w      = (float)self.box_texture->width;
            float h      = (float)self.box_texture->height;
            ray::Rectangle src{0, 0, w, h};
            ray::Rectangle dest{x, y, w * s * scale, h * s * scale};
            ray::DrawTexturePro(self.box_texture.value(), src, dest, ray::Vector2{0, 0}, 0, ray::Fade(ray::WHITE, fade));
        }
    );

    lua.new_usertype<BackBox>("BackBox", sol::base_classes, sol::bases<BaseBox>());

    lua.new_usertype<SongSelectPlayer>("SongSelectPlayer",
        "selected_difficulty", [](SongSelectPlayer& self) { return (int)self.selected_difficulty; },
        "player_num",           [](SongSelectPlayer& self) { return (int)self.player_num; },
        "neiro_active",         [](SongSelectPlayer& self) { return self.neiro_selector.has_value(); },
        "modifier_active",      [](SongSelectPlayer& self) { return self.modifier_selector.has_value(); }
    );

    lua.new_usertype<Navigator>("Navigator",
        "background_move",        &Navigator::background_move_anim,
        "background_fade_change", &Navigator::background_fade_anim,
        "bg_genre_frame",         &Navigator::bg_genre_frame,
        "last_bg_genre_frame",    &Navigator::last_bg_genre_frame
    );
}
