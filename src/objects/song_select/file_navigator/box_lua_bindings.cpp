#include "box_lua_bindings.h"
#include "box_song.h"
#include "box_folder.h"
#include "box_back.h"
#include "navigator.h"
#include "../player.h"
#include "../diff_sort.h"
#include "../../../libs/script.h"

#include <algorithm>

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
        // ROUND 63 (r63-duplicate-song-box): the box's own file path, as a
        // string.  A skin that keeps per-box wall-clock state (the wheel's
        // open/close animation clocks) needs a key that is UNIQUE PER BOX;
        // `text_name` is not one -- TJAParser strips "-New Audio-" /
        // "-Old Audio-" (tja.cpp, ex_data.new_audio/old_audio) so the two
        // charts of one song carry the SAME display title, and any
        // title-keyed table then makes the unfocused twin share the focused
        // box's clock (it was drawn permanently open).  This is the only
        // per-box identity the engine can hand out that survives a frame.
        "path", [](BaseBox& self) { return self.path.string(); },
        "is_new",          &BaseBox::is_new,
        "genre_frame", [](BaseBox& self) { return genre_to_ref_frame(self.genre_index); },
        "fore_color", [](BaseBox& self) -> sol::object {
            if (!self.fore_color.has_value()) return sol::lua_nil;
            sol::table t = script_manager.lua->create_table(0, 4);
            const ray::Color& c = self.fore_color.value();
            t["r"] = c.r; t["g"] = c.g; t["b"] = c.b; t["a"] = c.a;
            return t;
        },
        "name",       &BaseBox::horizontal_name,
        "name_large", &BaseBox::horizontal_name_large,
        // Raw #COLLECTION: value from the owning box.def ("" for a plain genre
        // folder, else "RECENT" / "FAVORITE" / "RECOMMENDED" / "NEW" /
        // "SEARCH" / "DIFFICULTY"). This is what a skin needs to tell a sort
        // folder from a genre folder without matching display names.
        "collection", &BaseBox::collection,
        // Raw GenreIndex (0..14), next to the existing genre_frame().
        "genre_index", [](BaseBox& self) { return (int)self.genre_index; }
    );

    lua.new_usertype<SongBox>("SongBox",
        sol::base_classes, sol::bases<BaseBox>(),
        "text_subtitle",  &SongBox::text_subtitle,
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
            sol::table t = script_manager.lua->create_table(0, 5);
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
        // What kind of folder this is, from engine data only:
        //   "dan"    — GenreIndex::DAN (段位道場)
        //   "sort"   — a #COLLECTION: box (search / difficulty / new / recent /
        //              favorite / recommended), i.e. a listing, not a genre
        //   "genre"  — everything else
        // There is deliberately no "event" here: an event folder is not an
        // engine concept (no marker in box.def), so a skin still has to
        // recognise those itself. See ENGINE_BINDINGS.md.
        "kind", [](FolderBox& self) -> std::string {
            if (self.genre_index == GenreIndex::DAN) return "dan";
            if (!self.collection.empty())            return "sort";
            return "genre";
        },
        "has_box_texture", [](FolderBox& self) { return self.box_texture.has_value(); },
        // ROUND 82 (r82-songselect-overflow-and-searchbox): `box.png` is a
        // per-folder banner the SONG LIBRARY supplies (FolderBox::load_text,
        // box_folder.cpp:188) - arbitrary dimensions, authored by whoever made
        // the folder, with no cabinet counterpart of a known size. Drawn 1:1 it
        // escapes the board completely: Songs/17 New's 344x424 banner came out
        // 516x636 (x screen_scale 1.5) on a board whose own texture is 960x352,
        // spilling ~359 px below the board's measured frame and over the 曲検索
        // bar beneath it (scratchpad/r82/shots/before_10_new_folder.png).
        //
        // The cabinet does NOT solve this by scissoring. Every one of the 22
        // `common_songselect` movies was walked this round for ClipDepth masks
        // (scratchpad/r82/walk_*, the r75 walker): 0 clip masks across ~8,900
        // PlaceObject records - the genre board has no clip region at all. What
        // it has instead is art AUTHORED to fit: all 16 frames of
        // `common_song_select_main_genre_board_character_l/r`, including the one
        // labelled `custom`, are exactly 480x480 with bounds (-240,-240)..(240,240).
        //
        // So containment here is by FIT, not by clip - the same rule the engine's
        // own C++ banner path already uses (box_folder.cpp:353-369, fit inside
        // `box_folder_max_size`, shrink only, never upscale). This binding never
        // had it. `max_w`/`max_h` (destination px, i.e. the same space as x/y)
        // switch it on and `cx`/`cy` centre the fitted result; all four absent =
        // exactly the old behaviour, so no other skin changes.
        "draw_box_texture", [](FolderBox& self, sol::table params) {
            if (!self.box_texture.has_value()) return;
            float s      = tex.screen_scale;
            float scale  = params.get_or("scale", 1.0f);
            float x      = params.get_or("x", 0.0f);
            float y      = params.get_or("y", 0.0f);
            float fade   = params.get_or("fade", 1.0f);
            float w      = (float)self.box_texture->width;
            float h      = (float)self.box_texture->height;
            float dw     = w * s * scale;
            float dh     = h * s * scale;

            float max_w = params.get_or("max_w", 0.0f);
            float max_h = params.get_or("max_h", 0.0f);
            if (dw > 0.0f && dh > 0.0f && (max_w > 0.0f || max_h > 0.0f)) {
                float fw = (max_w > 0.0f) ? max_w / dw : 1.0f;
                float fh = (max_h > 0.0f) ? max_h / dh : 1.0f;
                float f  = std::min(fw, fh);
                if (f < 1.0f) { dw *= f; dh *= f; }   // shrink only, aspect kept
            }
            sol::optional<float> cx = params["cx"];
            sol::optional<float> cy = params["cy"];
            if (cx) x = *cx - dw / 2.0f;
            if (cy) y = *cy - dh / 2.0f;

            ray::Rectangle src{0, 0, w, h};
            ray::Rectangle dest{x, y, dw, dh};
            ray::DrawTexturePro(self.box_texture.value(), src, dest, ray::Vector2{0, 0}, 0, ray::Fade(ray::WHITE, fade));
        }
    );

    lua.new_usertype<BackBox>("BackBox", sol::base_classes, sol::bases<BaseBox>());

    // ROUND 15 - the arcade sort window, read-only for the skin.  Every value is the
    // cabinet's own: `session` is which of the three rows is being decided,
    // diff/star/order are song_select_select_sort_window.lua 1-based sortParam,
    // `song_num` is GetDiffcultySongNum for the current (diff, star), `alpha` is the
    // root MC in/out fade and `arrow_offset(row)` is that row's `select` bounce.
    lua.new_usertype<DiffSortSelect>("SortWindow",
        "session",      [](DiffSortSelect& s) { return s.lua_session(); },
        "diff",         [](DiffSortSelect& s) { return s.lua_diff(); },
        "star",         [](DiffSortSelect& s) { return s.lua_star(); },
        "order",        [](DiffSortSelect& s) { return s.lua_order(); },
        "diff_count",   [](DiffSortSelect& s) { return s.lua_diff_count(); },
        "order_count",  [](DiffSortSelect& s) { return s.lua_order_count(); },
        "song_num",     [](DiffSortSelect& s) { return s.lua_song_num(); },
        "phase",        [](DiffSortSelect& s) { return s.lua_phase(); },
        "alpha",        [](DiffSortSelect& s) { return s.lua_alpha(); },
        "arrow_offset", [](DiffSortSelect& s, int row) { return s.lua_arrow_offset(row); }
    );

    lua.new_usertype<SongSelectPlayer>("SongSelectPlayer",
        "selected_difficulty", [](SongSelectPlayer& self) { return (int)self.selected_difficulty; },
        "player_num",           [](SongSelectPlayer& self) { return (int)self.player_num; },
        "neiro_active",         [](SongSelectPlayer& self) { return self.neiro_selector.has_value(); },
        "modifier_active",      [](SongSelectPlayer& self) { return self.modifier_selector.has_value(); },
        // Current y offset of the C++ option panel (exactly what ModifierSelector::draw adds to the
        // modifier/* texture.json y: -move while sliding in / holding, move + song_select_offset.x while
        // sliding out), or nil when no panel exists. Lets the skin draw panel-attached overlays (title text)
        // that ride the panel both ways without re-timing the animation.
        "modifier_offset",      [](SongSelectPlayer& self) -> sol::optional<float> {
            if (!self.modifier_selector.has_value()) return sol::nullopt;
            const auto& m = self.modifier_selector.value();
            float v = (float)m.move->attribute;
            return m.is_confirmed ? v + tex.skin_config[SC::SONG_SELECT_OFFSET].x : -v;
        },
        // Every row of the option panel as
        // {name, label, value, state, changed, enabled, greyed},
        // in draw order, or nil when no panel exists. Together with
        // modifier_index() and modifier_offset() this is everything the C++
        // panel knows, so a skin can draw the whole board itself.
        "modifier_rows", [](SongSelectPlayer& self) -> sol::object {
            if (!self.modifier_selector.has_value()) return sol::lua_nil;
            sol::table out = script_manager.lua->create_table();
            auto rows = self.modifier_selector.value().lua_rows();
            for (int i = 0; i < (int)rows.size(); i++) {
                sol::table r = script_manager.lua->create_table();
                r["name"]    = rows[i].name;
                r["value"]   = rows[i].value;
                r["label"]   = rows[i].label;
                r["state"]   = rows[i].state;
                r["changed"] = rows[i].changed;
                r["enabled"] = rows[i].enabled;
                r["greyed"]  = rows[i].greyed;
                out[i + 1]   = r;
            }
            return out;
        },
        // 1-based index of the row the cursor is on, or nil when no panel
        // exists. Equals row_count + 1 for the frame the panel is confirmed on.
        "modifier_index", [](SongSelectPlayer& self) -> sol::optional<int> {
            if (!self.modifier_selector.has_value()) return sol::nullopt;
            return self.modifier_selector.value().lua_index() + 1;
        },
        // True once every row has been confirmed and the board is sliding back
        // out; nil when there is no panel. The arcade stops accepting input and
        // hides the row cursor at that point.
        "modifier_confirmed", [](SongSelectPlayer& self) -> sol::optional<bool> {
            if (!self.modifier_selector.has_value()) return sol::nullopt;
            return self.modifier_selector.value().is_confirmed;
        },
        // The left/right value-change tween: {dir = +1 / -1, fade = 0..1,
        // active = bool}, or nil when no panel exists. `dir` is the side the
        // press came from, which is the arrow the arcade nudges 5 px outward.
        "modifier_change", [](SongSelectPlayer& self) -> sol::object {
            if (!self.modifier_selector.has_value()) return sol::lua_nil;
            const auto& m = self.modifier_selector.value();
            sol::table t = script_manager.lua->create_table();
            t["dir"]    = m.lua_change_dir();
            t["fade"]   = m.lua_change_fade();
            t["active"] = m.lua_change_active();
            return t;
        },
        // Current y offset of the standalone NEIRO panel, same meaning as
        // modifier_offset() (the value NeiroSelector::draw adds to neiro/*).
        "neiro_offset", [](SongSelectPlayer& self) -> sol::optional<float> {
            if (!self.neiro_selector.has_value()) return sol::nullopt;
            const auto& n = self.neiro_selector.value();
            float v = (float)n.move->attribute;
            return n.is_confirmed ? v + tex.skin_config[SC::SONG_SELECT_OFFSET].x : -v;
        },
        // Same shape for the standalone NEIRO panel: the hit-sound set names
        // and the 1-based selected index, or nil when it is not open.
        "neiro_names", [](SongSelectPlayer& self) -> sol::object {
            if (!self.neiro_selector.has_value()) return sol::lua_nil;
            sol::table out = script_manager.lua->create_table();
            const auto& names = self.neiro_selector.value().lua_names();
            for (int i = 0; i < (int)names.size(); i++) out[i + 1] = names[i];
            return out;
        },
        "neiro_index", [](SongSelectPlayer& self) -> sol::optional<int> {
            if (!self.neiro_selector.has_value()) return sol::nullopt;
            return self.neiro_selector.value().lua_index() + 1;
        }
    );

    lua.new_usertype<Navigator>("Navigator",
        "background_move",        &Navigator::background_move_anim,
        "background_fade_change", &Navigator::background_fade_anim,
        "bg_genre_frame",         &Navigator::bg_genre_frame,
        "last_bg_genre_frame",    &Navigator::last_bg_genre_frame,
        // The FolderBox currently opened inline (the folder whose songs are on
        // screen), or nil at the root. A song box keeps its own genre, so this
        // is the only way a skin can tell "which folder am I inside" — needed
        // for the arcade rule that song boards inside an event folder take the
        // folder's colour, not the song's.
        "current_folder", &Navigator::lua_current_folder,
        // ROUND 85: the real state-machine transitions (see Navigator::WheelEvent).
        // Poll `wheel_event_seq` once a frame; when it changes, `wheel_event`
        // carries the id.  Read-only from Lua — only the navigator emits.
        "wheel_event",     sol::readonly(&Navigator::wheel_event),
        "wheel_event_seq", sol::readonly(&Navigator::wheel_event_seq)
    );
}
