#include "box_lua_bindings.h"
#include "box_song.h"
#include "box_folder.h"
#include "box_back.h"
#include "box_dan.h"
#include "genre_bg.h"
#include "navigator.h"
#include "../player.h"
#include "../diff_sort.h"
#include "../../../libs/script.h"
#include "../../game/exam_caption.h"
#include "text_layout.h"

#include <algorithm>

void register_song_select_lua_bindings(sol::state& lua) {
    lua.new_usertype<BaseBox>("BaseBox",
        "box_x",           &BaseBox::box_x,
        "box_y",           &BaseBox::box_y,
        "left_bound",      &BaseBox::left_bound,
        "right_bound",     &BaseBox::right_bound,
        // read-only properties: box.fade / box.open_fade / box.open_anim stay the animation
        // objects Lua skins index (box.fade.attribute) now that the box owns them by unique_ptr
        "fade",            sol::property([](BaseBox& self) { return self.fade.get(); }),
        "open_fade",       sol::property([](BaseBox& self) { return self.open_fade.get(); }),
        "open_anim",       sol::property([](BaseBox& self) { return self.open_anim.get(); }),
        "draw_state",      &BaseBox::draw_state,
        "lua_kind",        &BaseBox::lua_kind,
        "text_name",       &BaseBox::text_name,
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
        "collection", &BaseBox::collection,
        "genre_index", [](BaseBox& self) { return (int)self.genre_index; },
        "texture_index", [](BaseBox& self) { return (int)self.texture_index; },
        "has_recolor",   &BaseBox::has_recolor,
        "begin_recolor", &BaseBox::begin_recolor,
        "end_recolor",   &BaseBox::end_recolor
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
        "course_info", [](SongBox& self, double diff) {
            auto info = self.course_info(static_cast<int>(diff));
            sol::table t = script_manager.lua->create_table(0, 6);
            t["has_course"]   = info.has_course;
            t["level"]        = info.level;
            t["is_branching"] = info.is_branching;
            t["crown"]        = info.crown;
            t["rank"]         = info.rank;
            t["has_score"]    = (diff >= 0 && diff < (int)self.scores.size() && self.scores[diff].has_value());
            return t;
        },
        "course_info_p2", [](SongBox& self, double diff) {
            auto info = self.course_info(static_cast<int>(diff));
            info.crown = 0;   // Crown::NONE
            info.rank  = 0;   // Rank::_NONE
            if (diff >= 0 && diff < (int)self.scores_p2.size() && self.scores_p2[diff].has_value()) {
                info.crown = (int)self.scores_p2[diff]->crown;
                info.rank  = (int)self.scores_p2[diff]->rank;
            }
            sol::table t = script_manager.lua->create_table(0, 6);
            t["has_course"]   = info.has_course;
            t["level"]        = info.level;
            t["is_branching"] = info.is_branching;
            t["crown"]        = info.crown;
            t["rank"]         = info.rank;
            t["has_score"]    = (diff >= 0 && diff < (int)self.scores_p2.size() && self.scores_p2[diff].has_value());
            return t;
        },
        "has_preimage", [](SongBox& self) { return self.preimage.has_value(); },
        "draw_preimage", [](SongBox& self, float bx, float by, float fade) {
            if (!self.preimage.has_value()) return;
            const SkinInfo& cfg = tex.skin_config[SC::PREIMAGE];
            ray::Rectangle src{0, 0, (float)self.preimage->width, (float)self.preimage->height};
            ray::Rectangle dest{bx + cfg.x, cfg.y + by, cfg.width, cfg.height};
            ray::DrawTexturePro(self.preimage.value(), src, dest, ray::Vector2{0, 0}, 0, ray::Fade(ray::WHITE, fade));
        }
    );

    lua.new_usertype<FolderBox>("FolderBox",
        sol::base_classes, sol::bases<BaseBox>(),
        "tja_count",      sol::readonly(&FolderBox::tja_count),
        "entered",        sol::readonly(&FolderBox::entered),
        "enter_fade", [](FolderBox& self) { return self.enter_fade.get(); },
        "highest_crown", [](FolderBox& self) -> sol::object {
            if (self.crown.empty()) return sol::lua_nil;
            auto it = self.crown.rbegin();   // std::map is sorted -> highest diff
            sol::table t = script_manager.lua->create_table(0, 2);
            t["crown"] = (int)it->second;
            t["frame"] = std::min((int)Difficulty::URA, it->first);
            return t;
        },
        "highest_crown_p2", [](FolderBox& self) -> sol::object {
            if (self.crown_p2.empty()) return sol::lua_nil;
            auto it = self.crown_p2.rbegin();
            sol::table t = script_manager.lua->create_table(0, 2);
            t["crown"] = (int)it->second;
            t["frame"] = std::min((int)Difficulty::URA, it->first);
            return t;
        },
        "explanation", [](FolderBox& self) {
            sol::table t = script_manager.lua->create_table(3, 0);
            for (int i = 0; i < 3; i++) t[i + 1] = self.explanation[i];
            return t;
        },
        "kind", [](FolderBox& self) -> std::string {
            if (self.genre_index == GenreIndex::DAN) return "dan";
            if (!self.collection.empty())            return "sort";
            return "genre";
        },
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

    lua.new_usertype<DanBox>("DanBox",
        sol::base_classes, sol::bases<BaseBox>(),
        "dan_title",      sol::readonly(&DanBox::dan_title),
        "dan_color",      sol::readonly(&DanBox::dan_color),
        "dan_rank",       sol::readonly(&DanBox::dan_rank),
        "clear_mark",     sol::readonly(&DanBox::clear_mark),
        "record_rank",    sol::readonly(&DanBox::record_rank),
        // the recorded best for exam i (course-wide) / for song j of a per-song exam;
        // nil when there is none, or when it was recorded for a different exam type
        "exam_best",      [](DanBox& self, double i) -> sol::optional<int> {
            auto k = static_cast<size_t>(i);
            if (k >= self.exam_bests.size() || k >= self.exams.size()) return sol::nullopt;
            const DanExamBest& b = self.exam_bests[k];
            if (b.type != self.exams[k].type || b.value < 0) return sol::nullopt;
            return b.value;
        },
        "exam_song_best", [](DanBox& self, double i, double j) -> sol::optional<int> {
            auto k = static_cast<size_t>(i);
            int s = static_cast<int>(j);
            if (k >= self.exam_bests.size() || k >= self.exams.size() || s < 0 || s > 2) return sol::nullopt;
            const DanExamBest& b = self.exam_bests[k];
            if (b.type != self.exams[k].type || b.song[s] < 0) return sol::nullopt;
            return b.song[s];
        },
        "dan_index",      sol::readonly(&DanBox::dan_index),
        "gaiden",         sol::readonly(&DanBox::gaiden),
        "is_back",        sol::readonly(&DanBox::is_back),
        "total_notes",    sol::readonly(&DanBox::total_notes),
        "song_masked",    [](DanBox& self, double i) { auto k = static_cast<size_t>(i); return k < self.song_masked.size() && self.song_masked[k]; },
        "song_count",     [](DanBox& self) { return (int)self.songs.size(); },
        "song_genre",     [](DanBox& self, double i) { return self.songs[static_cast<int>(i)].genre_index; },
        "song_difficulty",[](DanBox& self, double i) { return self.songs[static_cast<int>(i)].difficulty; },
        "song_level",     [](DanBox& self, double i) { return self.songs[static_cast<int>(i)].level; },
        "chip_name",      [](DanBox& self) { return self.name_text(); },
        "hori_name",      [](DanBox& self) { return self.hori_name.get(); },
        "song_title_text", [](DanBox& self, double i) {
            int idx = static_cast<int>(i);
            return (idx >= 0 && idx < (int)self.song_texts.size()) ? self.song_texts[idx].first.get() : nullptr;
        },
        "song_subtitle_text", [](DanBox& self, double i) {
            int idx = static_cast<int>(i);
            return (idx >= 0 && idx < (int)self.song_texts.size()) ? self.song_texts[idx].second.get() : nullptr;
        },
        "exam_count",     [](DanBox& self) { return (int)self.exams.size(); },
        "exam_type",      [](DanBox& self, double i) { return self.exams[static_cast<int>(i)].type; },
        "exam_red",       [](DanBox& self, double i) { return self.exams[static_cast<int>(i)].red; },
        "exam_gold",      [](DanBox& self, double i) { return self.exams[static_cast<int>(i)].gold; },
        "exam_range",     [](DanBox& self, double i) { return self.exams[static_cast<int>(i)].range; },
        "exam_gothrough", [](DanBox& self, double i) { return self.exams[static_cast<int>(i)].gothrough; },
        "exam_per_song",  [](DanBox& self, double i) { return self.exams[static_cast<int>(i)].per_song(); },
        "exam_song_count",[](DanBox& self, double i) { return (int)self.exams[static_cast<int>(i)].song_red.size(); },
        "exam_song_red",  [](DanBox& self, double i, double j) { return self.exams[static_cast<int>(i)].song_red[static_cast<int>(j)]; },
        "exam_song_gold", [](DanBox& self, double i, double j) {
            const Exam& e = self.exams[static_cast<int>(i)];
            int idx = static_cast<int>(j);
            return idx < (int)e.song_gold.size() ? e.song_gold[idx] : e.song_red[idx];
        },
        "exam_caption", [](DanBox& self, double i) {
            return exam_border_text(tex, self.exams[static_cast<int>(i)], global_data.config->general.language);
        },
        "exam_song_caption", [](DanBox& self, double i, double j) {
            return exam_border_text(tex, self.exams[static_cast<int>(i)].for_song(static_cast<int>(j)), global_data.config->general.language);
        }
    );

    lua.new_usertype<DiffSortSelect>("SortWindow",
        "session",      [](DiffSortSelect& s) { return s.lua_session(); },
        "diff",         [](DiffSortSelect& s) { return s.lua_diff(); },
        "star",         [](DiffSortSelect& s) { return s.lua_star(); },
        "order",        [](DiffSortSelect& s) { return s.lua_order(); },
        "song_num",     [](DiffSortSelect& s) { return s.lua_song_num(); },
        "phase",        [](DiffSortSelect& s) { return s.lua_phase(); },
        "alpha",        [](DiffSortSelect& s) { return s.lua_alpha(); },
        "arrow_offset", [](DiffSortSelect& s, double row) { return s.lua_arrow_offset(static_cast<int>(row)); }
    );

    lua.new_usertype<SongSelectPlayer>("SongSelectPlayer",
        "selected_difficulty", [](SongSelectPlayer& self) { return (int)self.selected_difficulty; },
        "player_num",           [](SongSelectPlayer& self) { return (int)self.player_num; },
        "difficulty_decided",   [](SongSelectPlayer& self) { return self.difficulty_decided(); },
        "neiro_active",         [](SongSelectPlayer& self) { return self.neiro_selector.has_value(); },
        "modifier_active",      [](SongSelectPlayer& self) { return self.modifier_selector.has_value(); },
        "modifier_offset",      [](SongSelectPlayer& self) -> sol::optional<float> {
            if (!self.modifier_selector.has_value()) return sol::nullopt;
            const auto& m = self.modifier_selector.value();
            float v = (float)m.move->attribute;
            return m.is_confirmed ? v + tex.skin_config[SC::SONG_SELECT_OFFSET].x : -v;
        },
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
        "modifier_index", [](SongSelectPlayer& self) -> sol::optional<int> {
            if (!self.modifier_selector.has_value()) return sol::nullopt;
            return self.modifier_selector.value().lua_index() + 1;
        },
        "modifier_confirmed", [](SongSelectPlayer& self) -> sol::optional<bool> {
            if (!self.modifier_selector.has_value()) return sol::nullopt;
            return self.modifier_selector.value().is_confirmed;
        },
        "modifier_change", [](SongSelectPlayer& self) -> sol::object {
            if (!self.modifier_selector.has_value()) return sol::lua_nil;
            const auto& m = self.modifier_selector.value();
            sol::table t = script_manager.lua->create_table();
            t["dir"]    = m.lua_change_dir();
            t["fade"]   = m.lua_change_fade();
            t["active"] = m.lua_change_active();
            return t;
        },
        "neiro_offset", [](SongSelectPlayer& self) -> sol::optional<float> {
            if (!self.neiro_selector.has_value()) return sol::nullopt;
            const auto& n = self.neiro_selector.value();
            float v = (float)n.move->attribute;
            return n.is_confirmed ? v + tex.skin_config[SC::SONG_SELECT_OFFSET].x : -v;
        },
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
        "current_folder", &Navigator::lua_current_folder,
        // reading either marks the skin as one that animates the folder legs (see swap_is_early)
        "wheel_event",     sol::property([](Navigator& n) { n.wheel_events_read = true; return n.wheel_event; }),
        "wheel_event_seq", sol::property([](Navigator& n) { n.wheel_events_read = true; return n.wheel_event_seq; })
    );

    lua.new_usertype<GenreBG>("GenreBG",
        "texture_frame", &GenreBG::texture_frame,
        "name",          &GenreBG::name_text,
        "has_recolor",   &GenreBG::has_recolor,
        "begin_recolor", &GenreBG::begin_recolor,
        "end_recolor",   &GenreBG::end_recolor,
        "stretch",       sol::property([](GenreBG& self) { return self.stretch.get(); }),
        "scale",         sol::property([](GenreBG& self) { return self.scale.get(); }),
        "move",          sol::property([](GenreBG& self) { return self.move.get(); }),
        "fade",          sol::property([](GenreBG& self) { return self.fade.get(); }),
        "move_left",     sol::property([](GenreBG& self) { return self.move_left.get(); }),
        "move_right",    sol::property([](GenreBG& self) { return self.move_right.get(); }),
        "is_finished",   &GenreBG::is_finished,
        "is_complete",   &GenreBG::is_complete
    );

    // Text-measurement helpers the Lua port needs to lay out folder explanations the way
    // the C++ FolderBox does (word_wrap needs ray::MeasureTextEx, unavailable in Lua).
    lua["text"]["word_wrap"] = [](const std::string& s, double font_size, float spacing, float max_width) {
        return word_wrap(s, static_cast<int>(font_size), spacing, max_width);
    };
    lua["text"]["language_is_cjk"] = [](const std::string& lang) {
        return language_is_cjk(lang);
    };
}
