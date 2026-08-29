#include "dan_select.h"
#include "../libs/gen4.h"
#include "../libs/green.h"
#include "../libs/song_parser.h"
#include "../libs/input.h"
#include "../libs/script.h"
#include "../objects/song_select/file_navigator/navigator.h"
#include "../libs/filesystem.h"
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <climits>

int DanNavigator::total_notes_for(const std::vector<DanSongEntry>& songs) {
    int total = 0;
    for (const auto& entry : songs) {
        try {
            SongParser sp(entry.song_path);
            auto [notes, bm, be, bn] = sp.notes_to_position(entry.difficulty);
            for (const Note& n : notes.notes)
                if (n.type >= NoteType::DON && n.type <= NoteType::KAT_L) total++;
            for (auto& sec : bm)
                for (const Note& n : sec.notes)
                    if (n.type >= NoteType::DON && n.type <= NoteType::KAT_L) total++;
        } catch (...) {}
    }
    return total;
}

Exam DanNavigator::parse_exam(const rapidjson::Value& e) {
    Exam exam;
    exam.type  = e["type"].GetString();
    exam.range = e["range"].GetString();
    if (e.HasMember("value") && e["value"].IsArray() && e["value"].Size() >= 2) {
        exam.red  = e["value"][0].GetInt();
        exam.gold = e["value"][1].GetInt();
    }
    // ROUND 19 -- the cabinet's `is_gothrough` (see Exam in global_data.h).
    // Absent -> true, i.e. a whole-run total, which is what this engine measures.
    if (e.HasMember("gothrough") && e["gothrough"].IsBool())
        exam.gothrough = e["gothrough"].GetBool();
    return exam;
}

std::optional<DanSongEntry> DanNavigator::load_song_entry(const rapidjson::Value& chart) {
    try {
        std::string chart_title    = chart["title"].GetString();
        std::string chart_subtitle = chart.HasMember("subtitle") ? chart["subtitle"].GetString() : "";
        int diff = chart["difficulty"].GetInt();

        auto path_opt = navigator.find_song_by_title(chart_title, chart_subtitle);
        if (!path_opt) {
            spdlog::warn("DanNavigator: song '{}' not found", chart_title);
            return std::nullopt;
        }

        SongParser sp(*path_opt);
        int level = sp.metadata.course_data.count(diff)
            ? sp.metadata.course_data.at(diff).level : 10;

        int genre = (int)GenreIndex::NAMCO;
        fs::path box_def_dir = path_opt->parent_path().parent_path();
        if (fs::exists(box_def_dir / "box.def"))
            genre = (int)navigator.parse_box_def(box_def_dir).genre_index;

        // ROUND 57 — optional per-song `"hidden": true` (the arcade's
        // is_hiddens[idx], see DanSongEntry in global_data.h).
        bool hidden = chart.HasMember("hidden") && chart["hidden"].IsBool() &&
                      chart["hidden"].GetBool();

        return DanSongEntry{*path_opt, genre, diff, level, hidden};
    } catch (...) {
        spdlog::warn("DanNavigator: failed to parse song entry");
        return std::nullopt;
    }
}

std::unique_ptr<DanBox> DanNavigator::load_dan_box(const fs::path& json_path) {
    auto doc = read_json_file(json_path);
    std::string title = doc["title"].GetString();
    int color = doc["color"].GetInt();
    // ROUND 19 -- which arcade rank plaque this course wears (frame index into
    // `rank_plate`, baked by scratchpad/r19dn/bake_plaque.py).  Absent -> -1 ->
    // the old colour plate + the text title, so PyTaikoGreen is unchanged.
    int rank = doc.HasMember("rank_art") && doc["rank_art"].IsInt() ? doc["rank_art"].GetInt() : -1;
    // ROUND 50 (r50-dani-visual-completion) -- which nameplate dan-chip frame
    // (0..24, `global/nameplate/dan_emblem`) a pass of this course awards
    // (DaniResultReward.ChangeNamePlateDani: now_dani = g_odaiDani_ +
    // normal_rank[now_dani] = g_odaiResult_+1). dan.json's new optional
    // "dan_index" field carries it; absent, the numeric prefix of the course's
    // own directory name ("0 Shokyuu" .. "24 Tatsujin") is used so the shipped
    // 25-course library maps with no dan.json edits. Out-of-range -> -1 (no
    // nameplate update, no celebration glyphs).
    int dan_index = -1;
    if (doc.HasMember("dan_index") && doc["dan_index"].IsInt()) {
        dan_index = doc["dan_index"].GetInt();
    } else {
        try {
            std::string dir = json_path.parent_path().filename().string();
            size_t pos = 0;
            int v = std::stoi(dir, &pos);
            if (pos > 0) dan_index = v;
        } catch (...) {}
    }
    if (dan_index < 0 || dan_index > 24) dan_index = -1;

    std::vector<DanSongEntry> songs;
    if (doc.HasMember("charts")) {
        for (auto& chart : doc["charts"].GetArray()) {
            if (auto entry = load_song_entry(chart))
                songs.push_back(*entry);
        }
    }
    if (songs.empty()) return nullptr;

    std::vector<Exam> exams;
    if (doc.HasMember("exams")) {
        for (auto& e : doc["exams"].GetArray())
            exams.push_back(parse_exam(e));
    }

    auto box = std::make_unique<DanBox>(json_path, title, color, songs, exams, total_notes_for(songs));
    box->dan_rank = rank;
    box->dan_index = dan_index;   // ROUND 50
    // ROUND 57 — optional course-level `"gaiden": true` (Cabinet.GaidenDaniInfo
    // semantics: no 昇段/nameplate/congrats on a pass; see DanResultData).
    box->gaiden = doc.HasMember("gaiden") && doc["gaiden"].IsBool() &&
                  doc["gaiden"].GetBool();
    return box;
}

void DanNavigator::init(const std::vector<fs::path>& song_paths) {
    boxes.clear();
    selected_index = 0;

    for (const fs::path& root_path : song_paths) {
        // A path at or inside a game's data holds no dan.json, only tens of
        // thousands of chart files this walk would crawl through.
        if (!gen4::find_data_root(root_path).empty() ||
            !green::find_data_root(root_path).empty()) continue;
        try {
            auto it = fs::recursive_directory_iterator(
                root_path, fs::directory_options::skip_permission_denied);
            for (; it != fs::end(it); ++it) {
                const auto& entry = *it;
                if (entry.is_directory() &&
                    (gen4::find_data_root(entry.path()) == entry.path() ||
                     green::find_data_root(entry.path()) == entry.path())) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (entry.path().filename() == "dan.json") {
                    if (auto box = load_dan_box(entry.path())) {
                        boxes.push_back(std::move(box));
                    }
                }
            }
        } catch (const std::exception& ex) {
            spdlog::warn("DanNavigator: error loading {}: {}", root_path.string(), ex.what());
            }
    };

    if (boxes.empty()) { spdlog::warn("DanNavigator: no dan courses found"); return; }

    // ROUND 17 -- the ribbon must be in RANK order, and the directory walk is not.
    //
    // `fs::recursive_directory_iterator` yields names LEXICOGRAPHICALLY, so a
    // library whose courses are `0 Shokyuu` .. `24 Tatsujin` came out as
    // 0, 1, 10, 11, ... 19, 2, 20, 21, ... 3, 4 ... and the ribbon read
    // "... 六段 八段 九段 九級 十段 玄人 ..." with every 級 course interleaved into
    // the 段 courses. The cabinet's strip is a fixed weakest-to-strongest run
    // (`dani_select.nulm` board_1..board_20 at a fixed 68 px pitch), so the order
    // is not cosmetic.
    //
    // The course folder's own numeric prefix is the intended order. A library
    // without prefixes keeps its previous, stable alphabetical order because the
    // fallback key is the path itself.
    auto order_key = [](const DanBox* b) -> std::pair<int, std::string> {
        const std::string name = b->path.parent_path().filename().string();
        size_t i = 0;
        while (i < name.size() && (unsigned char)name[i] >= '0' && (unsigned char)name[i] <= '9') i++;
        if (i == 0) return {INT_MAX, name};
        try { return {std::stoi(name.substr(0, i)), name}; }
        catch (...) { return {INT_MAX, name}; }
    };
    std::stable_sort(boxes.begin(), boxes.end(),
                     [&](const std::unique_ptr<DanBox>& a, const std::unique_ptr<DanBox>& b) {
                         return order_key(a.get()) < order_key(b.get());
                     });

    set_positions(true, 0);
    boxes[selected_index]->expand_box();
    for (auto& b : boxes) b->fade_in(100);
}

// Reads the ribbon geometry a skin declared, falling back to the values that
// were compile-time constants before this existed. Missing key -> old layout.
DanNavigator::RibbonLayout DanNavigator::ribbon_layout() const {
    RibbonLayout r;
    if (const SkinInfo* e = tex.skin_entry("dan_ribbon")) {
        r.legacy = false;
        if (e->x != 0)     r.center  = e->x;
        if (e->width != 0) r.spacing = e->width;
        r.side_l = 0;
        r.side_r = 0;
    }
    if (const SkinInfo* e = tex.skin_entry("dan_ribbon_side")) {
        r.side_l = e->x;
        r.side_r = e->y;
    }
    return r;
}

void DanNavigator::set_positions(bool init, float duration) {
    int n = (int)boxes.size();
    if (n == 0) return;
    const RibbonLayout lay = ribbon_layout();
    for (int i = 0; i < n; i++) {
        float offset = i - selected_index;
        if (offset > n / 2.0f)  offset -= n;
        else if (offset < -n / 2.0f) offset += n;

        // The legacy constants were authored in PARENT (720p) space, so they
        // keep their * screen_scale. A skin's own skin_config values are in the
        // skin's own pixel space -- the rule every other skin_config key
        // follows -- so the ribbon path must NOT scale them again.
        const float k = lay.legacy ? tex.screen_scale : 1.0f;
        float base    = lay.center * k;
        float spacing = lay.spacing * k;
        float side_l  = lay.side_l * k;
        float side_r  = lay.side_r * k;

        // Two layouts, and which one runs is decided by whether the skin said
        // anything at all.
        //
        //   legacy (no "dan_ribbon" key, i.e. PyTaikoGreen): reproduced verbatim.
        //     Its anchor sits one box-width left of the centre and BOTH offset 0
        //     and offset +1 resolve to `base`, so offset +1 is what actually gets
        //     pushed right by side_r and offset -1 left by side_l. Only three
        //     courses are ever on screen; that asymmetry is the reason.
        //   ribbon (skin declared "dan_ribbon"): a plain, continuous
        //     `centre + offset * spacing` strip -- the arcade's 20 chips at a
        //     fixed pitch -- with the side gaps added on top only when non-zero.
        float pos;
        if (lay.legacy) {
            float anchor = base - spacing;
            pos = anchor + offset * spacing;
            if (std::abs(pos - anchor) < 1.0f)      pos = base;
            else if (pos > anchor)                  pos += side_r;
            else                                    pos -= side_l;
        } else {
            pos = base + offset * spacing;
            if (offset > 0)      pos += side_r;
            else if (offset < 0) pos -= side_l;
        }

        if (init || std::abs(pos - boxes[i]->position) >= tex.screen_width)
            boxes[i]->set_position(pos);
        else
            boxes[i]->move_box(pos, duration);
    }
}

void DanNavigator::move_left() {
    last_moved = get_current_ms();
    if (boxes.empty()) return;
    boxes[selected_index]->close_box();
    selected_index = (selected_index - 1 + (int)boxes.size()) % (int)boxes.size();
    set_positions(false, 166);
    boxes[selected_index]->expand_box();
}

void DanNavigator::move_right() {
    last_moved = get_current_ms();
    if (boxes.empty()) return;
    boxes[selected_index]->close_box();
    selected_index = (selected_index + 1) % (int)boxes.size();
    set_positions(false, 166);
    boxes[selected_index]->expand_box();
}

void DanNavigator::skip(int delta) {
    last_moved = get_current_ms();
    if (boxes.empty()) return;
    boxes[selected_index]->close_box();
    selected_index = ((selected_index + delta) % (int)boxes.size() + (int)boxes.size()) % (int)boxes.size();
    set_positions(true, 0);
    boxes[selected_index]->expand_box();
}

DanBox* DanNavigator::get_current() {
    if (boxes.empty()) return nullptr;
    return boxes[selected_index].get();
}

void DanNavigator::update(double current_ms) {
    for (auto& b : boxes) {
        bool on_screen = b->position > -156 * tex.screen_scale && b->position < tex.screen_width + 144 * tex.screen_scale;
        if (on_screen && !b->text_loaded)
            b->load_text();
        b->update(current_ms);
    }
}

// ROUND 17 -- the arcade's selection marks (dan_select/MAPPING D3 + D4).
//
// `cursor_big` (char 267) is an 88x184 white INSET glow frame drawn on the
// selected chip, on a 60-frame loop. Sampled alphas, straight off
// `lumen_anim_dump --sprite 267 --range 0,59 --fields a`:
//   f0 1.000  f5 0.977  f10 0.910  f15 0.801  f20 0.645  f25 0.441  f30 0.301
//   f35 0.520 f40 0.699 f45 0.836  f50 0.934  f55 0.988  -> loops
// Kept as the cabinet's own sample table with a plain lerp between rows, which is
// the ANIM_PIPELINE contract; a sine would be the eyeballed closed form this
// pipeline exists to stop us writing (the curve is not symmetric -- 30 frames
// down against 29 up).
static float dan_cursor_alpha(double ms) {
    static const float A[12] = {1.000f, 0.977f, 0.910f, 0.801f, 0.645f, 0.441f,
                                0.301f, 0.520f, 0.699f, 0.836f, 0.934f, 0.988f};
    double f = std::fmod(ms * 0.06, 60.0);        // ms -> arcade frames, one loop
    if (f < 0) f += 60.0;
    double t = f / 5.0;                            // the table is every 5th frame
    int i = (int)t;
    float u = (float)(t - i);
    return A[i % 12] * (1.0f - u) + A[(i + 1) % 12] * u;
}

// ROUND 57 — load Scripts/dan_select/dan_select.lua once (see dan_select.h).
void DanNavigator::load_paint_surface() {
    paint_tried = true;
    paint_ok    = false;
    if (!script_manager.lua || !script_manager.has_lua_script("dan_select")) return;
    sol::state& lua = *script_manager.lua;
    if (!lua["DanSelect"].valid()) {
        auto r = lua.script_file(script_manager.get_lua_script_path("dan_select"));
        if (!r.valid()) {
            sol::error err = r;
            spdlog::error("dan_select.lua load error: {}", err.what());
            return;
        }
    }
    sol::optional<sol::table> cls = lua["DanSelect"];
    if (!cls) return;
    sol::protected_function ctor = (*cls)["new"];
    if (!ctor.valid()) return;
    auto res = ctor();
    if (!res.valid()) {
        sol::error err = res;
        spdlog::error("DanSelect.new error: {}", err.what());
        return;
    }
    lua_paint      = res;
    fn_draw_cursor = lua_paint["draw_cursor"];
    paint_ok       = fn_draw_cursor.valid();
}

void DanNavigator::draw() {
    for (auto& b : boxes) {
        float pos = b->position;
        if (pos >= -156 * tex.screen_scale && pos <= tex.screen_width + 144 * tex.screen_scale) {
            b->draw();
        }
    }

    // The cursor and the two arrow chips ride the SELECTED chip, so they are
    // drawn after the whole strip rather than from inside a box.
    if (boxes.empty()) return;
    DanBox* cur = boxes[selected_index].get();
    if (!cur) return;
    const float pos = cur->position;
    const double now = get_current_ms();

    // ROUND 57 — hand the selection marks to the skin's paint surface when it
    // has one; the inline tables below stay as the fail-soft fallback.
    if (!paint_tried) load_paint_surface();
    if (paint_ok) {
        auto r = fn_draw_cursor(lua_paint, pos, now,
                                last_moved > 0 ? now - last_moved : -1.0);
        if (r.valid()) return;
        sol::error err = r;
        spdlog::error("DanSelect:draw_cursor error ({}), reverting to inline tables", err.what());
        paint_ok = false;
    }

    if (tex.has_texture("box/cursor"))
        tex.draw_texture(tex.get_enum("box/cursor"),
                         {.x = pos, .fade = dan_cursor_alpha(now)});

    // arrow_right / arrrow_left (chars 263 / 266) are 66-frame NON-LOOPING clips
    // with labels arrow_on=0 / arrow_off=61: they play on a cursor MOVE and park
    // invisible. The shape drifts OUTWARD 0 -> +10 px over f0..60 and the alpha
    // holds 1 to f30 then falls to 0 at f60, so they are a ~1 s flourish after
    // each step, not permanent chrome.
    if (last_moved <= 0) return;
    const double af = (now - last_moved) * 0.06;
    if (af < 0.0 || af > 60.0) return;
    const float drift = (float)(af / 60.0 * 10.0);
    const float aa = (af <= 30.0) ? 1.0f : (float)(1.0 - (af - 30.0) / 30.0);
    if (tex.has_texture("box/arrow_r"))
        tex.draw_texture(tex.get_enum("box/arrow_r"), {.x = pos + drift, .fade = aa});
    if (tex.has_texture("box/arrow_l"))
        tex.draw_texture(tex.get_enum("box/arrow_l"), {.x = pos - drift, .fade = aa});
}

// ─── DanSelectScreen ─────────────────────────────────────────────────────────

void DanSelectScreen::on_screen_start() {
    Screen::on_screen_start();
    audio.play_sound("bgm", VolumePreset::MUSIC);
    audio.play_sound("dan_select", VolumePreset::VOICE);

    indicator     = std::make_unique<Indicator>(Indicator::State::SELECT);
    confirm_fade  = (FadeAnimation*)tex.get_animation(8);
    state         = SongSelectState::BROWSING;
    confirm_index = CONFIRM_NO;                 // cabinet default: selectIndex = 3
    last_moved    = 0;
    modifier_selector.reset();

    // ROUND 32 -- see dan_select.h: anchors the cabinet's free-running
    // 100 ms input-enable pulse to when the wheel becomes interactive.
    wheel_locked     = false;
    wheel_tick_epoch = get_current_ms();
    wheel_tick_seen  = 0;

    // ROUND 17 -- the 演奏オプション panel the confirmation dialog's option chip
    // opens. Two things have to be made resident on THIS screen first, because
    // both are normally song-select-only:
    //   * the `modifier` texture subset. Per-screen loading keys off the screen
    //     name, so `MODIFIER::*` is not resident on DAN_SELECT; load_folder takes
    //     an explicit (screen, subset) pair, so no art has to be duplicated.
    //   * animations 28-32 (+ optional 39), which ModifierSelector's constructor
    //     reads. `TextureWrapper::get_animation` THROWS on a missing id, so the
    //     child skin's dan_select/animation.json now carries them (a child
    //     animation.json fully replaces the parent's -- it is not merged).
    // `Screen::on_screen_end` unloads every texture for the screen, so nothing
    // leaks into the next one.
    tex.load_folder("song_select", "modifier");
    if (auto pd = scores_manager.get_player_data(get_player_id(global_data.player_num)))
        dan_player_data = *pd;

    fs::path dan_folder = global_data.session_data[(int)global_data.player_num].selected_dan_folder;
    dan_navigator.init({dan_folder});
}

Screens DanSelectScreen::on_screen_end(Screens next_screen) {
    DanBox* current = dan_navigator.get_current();
    if (current && next_screen == Screens::GAME_DAN) {
        SessionData& sd = global_data.session_data[(int)global_data.player_num];
        sd.selected_dan      = current->songs;
        sd.selected_dan_exam = current->exams;
        sd.song_title        = current->dan_title;
        sd.dan_color         = current->dan_color;
        sd.dan_rank          = current->dan_rank;
        sd.dan_index         = current->dan_index;   // ROUND 50
        // ROUND 57 — the arcade's g_maxDaniNum_: the top of the loaded ladder,
        // so DAN_RESULT can fire the congrats popup only on the first pass of
        // the library's own highest course (not a hardcoded 24).
        sd.dan_index_max = -1;
        for (const auto& b : dan_navigator.boxes)
            sd.dan_index_max = std::max(sd.dan_index_max, b->dan_index);
        sd.dan_gaiden = current->gaiden;             // ROUND 57
        if (!current->songs.empty())
            sd.selected_song = current->songs[0].song_path;
    }
    return Screen::on_screen_end(next_screen);
}

void DanSelectScreen::handle_input_browsing(double current_ms) {
    if (dan_navigator.boxes.empty()) return;

    // ROUND 32 (r32-audit-songselect) -- two real divergences from
    // `script_lua/dani_select/dani_select_dani_main.lua`, found while auditing
    // SONG_SELECT/DAN_SELECT input handling:
    //
    // 1. There is no "hold or double-tap to fast-jump" mechanic anywhere in the
    //    real script. `MoveCursor` (PlayerInput, lines 267-277) is ALWAYS
    //    called with a fixed +-1; the only 10-song-at-a-time stepping in this
    //    file (`skipCount`, lines 326-361) is an internal search helper for
    //    hopping over locked/disabled dani while looking for the next
    //    selectable one, not a player input path. `git log -S"last_moved + 50"
    //    -- dan_select.cpp` => commit "add dans": the kat-double-tap ->
    //    `skip(+-10)` trigger below predates this fidelity project (it is base
    //    PyTaikoGreen behaviour, never arcade-sourced) and ROUND 21 only noted
    //    it existed without checking it against the real Lua. Removed for real
    //    (drum) player input; the Ctrl-arrow keys stay as a keyboard-only QA
    //    shortcut, unreachable by a real player.
    // 2. `CheckInput()` (lines 699-728) is wrapped in `if this:GetInputEnable()
    //    == true then ... end` for BOTH the decide and the left/right branches
    //    -- `MoveCursor()` clears that flag the instant a move starts (line
    //    280) and it is only restored by the free-running 100 ms pulse
    //    documented on `wheel_locked` in dan_select.h. `wheel_locked` below
    //    reproduces that: a move (or a decide) is dropped until the next
    //    100 ms boundary since the wheel became interactive.
    if (wheel_locked) {
        long long tick = (long long)((current_ms - wheel_tick_epoch) / 100.0);
        if (tick > wheel_tick_seen) {
            wheel_locked    = false;
            wheel_tick_seen = tick;
        }
    }

    bool skip_left  = check_key_pressed(ray::KEY_LEFT_CONTROL);
    bool skip_right = check_key_pressed(ray::KEY_RIGHT_CONTROL);
    bool nav_left   = is_l_kat_pressed(global_data.player_num);
    bool nav_right  = is_r_kat_pressed(global_data.player_num);
    bool confirm    = is_l_don_pressed(global_data.player_num) || is_r_don_pressed(global_data.player_num);

    if (skip_left) {
        audio.play_sound("skip", VolumePreset::SOUND);
        dan_navigator.skip(-10);
        last_moved = current_ms;
    } else if (skip_right) {
        audio.play_sound("skip", VolumePreset::SOUND);
        dan_navigator.skip(10);
        last_moved = current_ms;
    } else if (!wheel_locked && nav_left) {
        audio.play_sound("kat", VolumePreset::SOUND);
        dan_navigator.move_left();
        last_moved   = current_ms;
        wheel_locked = true;
    } else if (!wheel_locked && nav_right) {
        audio.play_sound("kat", VolumePreset::SOUND);
        dan_navigator.move_right();
        last_moved   = current_ms;
        wheel_locked = true;
    } else if (!wheel_locked && confirm) {
        audio.play_sound("don", VolumePreset::SOUND);
        audio.play_sound("confirm_box", VolumePreset::SOUND);
        audio.play_sound("dan_confirm", VolumePreset::VOICE);
        confirm_fade->start();
        state = SongSelectState::SONG_SELECTED;
        // Every visit to the dialog starts on the cabinet's own default,
        // 挑戦しない (`selectIndex = 3`), not on whatever the last visit left.
        confirm_index = CONFIRM_NO;
        modifier_selector.reset();
        // ROUND 21 -- StartWait's 500 ms input-dead window, see dan_select.h.
        confirm_opened_at = current_ms;
    }
}

std::optional<Screens> DanSelectScreen::handle_input_selected() {
    // ROUND 21 -- cabinet StartWait: PlayerInput() is unreachable for the
    // first 500 ms after the dialog opens (Loading -> StartWait -> Main,
    // `wait_input_cnt = 0.5 * Common.FPS`, gated on `mc_main`'s own
    // `in_challenge` entrance clip). A don thrown at the moment the player
    // decides a course must not also land on the confirmation dialog.
    constexpr double CONFIRM_INPUT_LOCK_MS = 500.0;
    if (get_current_ms() < confirm_opened_at + CONFIRM_INPUT_LOCK_MS) return std::nullopt;

    const PlayerNum pn = global_data.player_num;
    const bool l_kat = is_l_kat_pressed(pn), r_kat = is_r_kat_pressed(pn);
    const bool don   = is_l_don_pressed(pn) || is_r_don_pressed(pn);

    // While the option panel is open it owns the whole input plane, exactly as
    // `PlayerInput` early-returns on `GetOpenOptionFlag()` (line 250).
    if (modifier_selector.has_value()) {
        if (l_kat) { audio.play_sound("kat", VolumePreset::SOUND); modifier_selector->left();  }
        if (r_kat) { audio.play_sound("kat", VolumePreset::SOUND); modifier_selector->right(); }
        if (don)   { audio.play_sound("don", VolumePreset::SOUND); modifier_selector->confirm(); }
        return std::nullopt;
    }

    // MoveCursor: LEFT is -1, RIGHT is +1, CLAMPED (not wrapped) to the three
    // entries -- dani_select_confirmation_main.lua:283-301.
    if (l_kat) { audio.play_sound("kat", VolumePreset::SOUND); confirm_index = std::max(confirm_index - 1, (int)CONFIRM_OPTION); }
    if (r_kat) { audio.play_sound("kat", VolumePreset::SOUND); confirm_index = std::min(confirm_index + 1, (int)CONFIRM_NO); }

    if (!don) return std::nullopt;

    if (confirm_index == CONFIRM_OPTION) {
        // `DecidePlayer` plays don for entries 1 and 2 and the cancel cue only
        // for 挑戦しない (lines 329-347).
        audio.play_sound("don", VolumePreset::SOUND);
        modifier_selector.emplace(pn, &dan_player_data);
        return std::nullopt;
    }
    if (confirm_index == CONFIRM_YES) {
        audio.play_sound("don", VolumePreset::SOUND);
        return on_screen_end(Screens::GAME_DAN);
    }
    audio.play_sound("cancel", VolumePreset::SOUND);
    state = SongSelectState::BROWSING;
    return std::nullopt;
}

std::optional<Screens> DanSelectScreen::update() {
    Screen::update();
    double current_ms = get_current_ms();
    allnet_indicator.update(current_ms);
    dan_navigator.update(current_ms);
    indicator->update(current_ms);
    confirm_fade->update(current_ms);

    if (state == SongSelectState::BROWSING) {
        handle_input_browsing(current_ms);
        if (is_r_don_pressed(global_data.player_num) || is_l_don_pressed(global_data.player_num)) {
            // handled in browsing
        }
    } else if (state == SongSelectState::SONG_SELECTED) {
        // The panel closes itself once the cursor walks off its last row
        // (ModifierSelector::confirm -> is_confirmed -> the slide-out finishes).
        // Song select persists the same way: mutate the PlayerData, save it, and
        // let GameScreen::get_player_modifiers read it back out of the DB.
        if (modifier_selector.has_value()) {
            modifier_selector->update(current_ms);
            if (modifier_selector->is_finished) {
                scores_manager.save_player_data(dan_player_data);
                modifier_selector.reset();
                // The cabinet returns the cursor to the dialog with the option
                // chip still selected (`GotoAndPlay("out_option")` leaves
                // selectIndex where it was, line 107).
            }
        }
        if (auto next = handle_input_selected()) return next;
    }

    // Back: left kat in browsing with no songs or specific back logic
    if (check_key_pressed(global_data.config->keys.back_key) && state == SongSelectState::BROWSING) {
        return on_screen_end(Screens::SONG_SELECT);
    }
    return std::nullopt;
}

void DanSelectScreen::draw_confirm_overlay() {
    float f = confirm_fade->attribute;
    if (f <= 0) return;
    ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height,
                       ray::Fade(ray::BLACK, std::min(0.5f, (float)f)));
    tex.draw_texture(CONFIRM_BOX::BG,               {.fade=f});
    tex.draw_texture(CONFIRM_BOX::CONFIRMATION_TEXT,{.fade=f});
    for (int i = 0; i < 2; i++)
        tex.draw_texture(CONFIRM_BOX::SELECTION_BOX,{.fade=f, .index=i});
    // Slot 0 is the LEFT pill and slot 1 the RIGHT one (confirm_box/texture.json).
    // The cabinet puts 挑戦する on the left and 挑戦しない on the right, so YES
    // owns slot 0 -- see the ConfirmEntry comment in dan_select.h for the two
    // pieces of arcade evidence.
    if (confirm_index != CONFIRM_OPTION) {
        const int side = (confirm_index == CONFIRM_YES) ? 0 : 1;
        tex.draw_texture(CONFIRM_BOX::SELECTION_BOX_HIGHLIGHT,{.fade=f, .index=side});
        tex.draw_texture(CONFIRM_BOX::SELECTION_BOX_OUTLINE,  {.fade=f, .index=side});
    }
    // ROUND 17 -- put 挑戦する on the LEFT and 挑戦しない on the RIGHT.
    //
    // This has to be a DRAW-TIME swap, not a texture.json one. `yes.png` and
    // `no.png` are the PARENT skin's files (this skin ships neither), and
    // `TextureWrapper::load_folder` only lets a child entry override a parent one
    // for a name the child actually ships a FILE for (`overridden_names`) -- a
    // child texture.json key with no PNG behind it is skipped and the parent's
    // position stands. Re-baking the parent's 720p art up to 1080p just to move
    // it would lose quality for nothing.
    //
    // The offset is the skin's OWN geometry rather than a magic number: the two
    // `selection_box` slots are the two button positions, so their delta is
    // exactly how far each label has to travel to land in the other slot.
    float swap_dx = 0.0f;
    if (tex.skin_flag("dan_confirm_yes_left")) {
        auto it = tex.textures.find((uint32_t)CONFIRM_BOX::SELECTION_BOX);
        if (it != tex.textures.end() && it->second->x.size() >= 2)
            swap_dx = (float)(it->second->x[1] - it->second->x[0]);
    }
    tex.draw_texture(CONFIRM_BOX::YES, {.x=-swap_dx, .fade=f});
    tex.draw_texture(CONFIRM_BOX::NO,  {.x= swap_dx, .fade=f});

    // The option chip. Cabinet art, verbatim: the 98x96 wrench plate is
    // `window/button_option/#35@1` and the 142x142 yellow ring is its
    // `cursor_frame/frame` pair (#27@0 + #29@1), both rendered out of
    // dani_select_window.nulm/.nutexb. The chip sits LEFT of the two pills
    // (world tx -346 against -105 / +233).
    if (tex.has_texture("confirm_box/option")) {
        if (confirm_index == CONFIRM_OPTION && tex.has_texture("confirm_box/option_highlight"))
            tex.draw_texture(tex.get_enum("confirm_box/option_highlight"), {.fade=f});
        tex.draw_texture(tex.get_enum("confirm_box/option"), {.fade=f});
    }
}

void DanSelectScreen::draw() {
    tex.draw_texture(GLOBAL::BG,        {});
    tex.draw_texture(GLOBAL::BG_HEADER, {});
    tex.draw_texture(GLOBAL::BG_FOOTER, {});
    tex.draw_texture(GLOBAL::FOOTER,    {});

    // ROUND 25 (r25-ngtile): coin_overlay's fixed-position card/QR status chip
    // (`banapass_osaifu_keitai`, 106x106 @ 1570,38 -- ROUND 17's "OK/NG card
    // chip") now draws BEFORE the rank ribbon, not after. The arcade's own
    // ribbon geometry (ROUND 14's `centres x = 221 + 68n`) puts its rightmost
    // chips at almost exactly this same x, so on the cabinet this always-on
    // HUD chip sits UNDER the opaque 88x184 rank tile at that screen position
    // and is invisible there; drawing it afterward (the old order) let it
    // paint its "NG" QR glyph on top of whichever tile currently lands under
    // x~1570-1676, which is what ROUND 16/17/22/23 documented as the
    // "broken/NG rank tile". No geometry changed -- only which of the two
    // opaque layers occludes the other.
    coin_overlay.draw();

    dan_navigator.draw();

    if (state == SongSelectState::SONG_SELECTED) {
        draw_confirm_overlay();
        // Over the dialog, as the cabinet's `option_instance` (root depth 13)
        // sits above `window_instance` (depth 12).
        if (modifier_selector.has_value()) modifier_selector->draw();
    }

    indicator->draw(tex.skin_config[SC::DAN_SELECT_INDICATOR].x, tex.skin_config[SC::DAN_SELECT_INDICATOR].y);
    tex.draw_texture(GLOBAL::DAN_SELECT, {});
    allnet_indicator.draw();

    // ROUND 17: the skin's top-most slot on this screen, above the coin overlay,
    // the DAN_SELECT plate and the allnet chip. The 段位道場 shutter's SECOND half
    // (the doors opening to reveal this screen) is drawn from here; see
    // Indicator::draw_top and Scripts/song_select/dan_doors.lua.
    indicator->draw_top();
}
