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
#include <chrono>   // ROUND 95 -- the course-scan worker's wait/poll
#include <cstdlib>

// ROUND 64 — the cabinet's IntroductionMain -> DaniSelectMain hand-off.
//
// `loading_dani_intro`'s DAN_SELECT leg is arcade frames 182..262 (the kanji fade, the
// shutter opening, the scrim clearing and the background's push-in cross-fade — see
// Scripts/song_select/dan_doors.lua), 80 frames at the cabinet's 60 fps CLIP rate (not
// Common.FPS = 120, which is the script tick). `timer_:StartCount()` runs on the first
// frame of DaniSelectMain, i.e. immediately after that leg.
static constexpr double DAN_INTRO_MS      =  80.0 * 1000.0 / 60.0;   // 1333.3, f182..262
// ROUND 86 — entered from the ENTRY mode board there is no song-select half, so the whole
// movie plays here: label `in` (f5) .. `end` (f262) = 257 frames.
static constexpr double DAN_INTRO_FULL_MS = 257.0 * 1000.0 / 60.0;   // 4283.3, f5..262

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

std::optional<DanSongEntry> DanNavigator::load_song_entry(const rapidjson::Value& chart,
                                                          std::pair<std::string, std::string>* titles_out) {
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

        // ROUND 95 -- keep the display strings off THIS parse (see dan_select.h).
        if (titles_out && global_data.config) {
            const std::string& lang = global_data.config->general.language;
            titles_out->first  = sp.metadata.title.count(lang)
                ? sp.metadata.title.at(lang)
                : (sp.metadata.title.count("en") ? sp.metadata.title.at("en") : std::string());
            titles_out->second = sp.metadata.subtitle.count(lang)
                ? sp.metadata.subtitle.at(lang) : std::string();
        }

        int genre = (int)GenreIndex::NAMCO;
        fs::path box_def_dir = path_opt->parent_path().parent_path();
        // ROUND 95 -- the UNCACHED parse. This runs on the course-scan worker,
        // and `Navigator::parse_box_def` writes `box_def_cache`, which
        // `Navigator::loader_thread` also writes. Same parse, no shared write.
        if (fs::exists(box_def_dir / "box.def"))
            genre = (int)Navigator::parse_box_def_uncached(box_def_dir).genre_index;

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

// ROUND 95 — everything the old `load_dan_box` did EXCEPT constructing the
// `DanBox`. Worker-safe by construction: filesystem, rapidjson, SongParser and
// read-only globals only. The one thing that had to change to make that true is
// the box.def lookup, which used to go through `navigator.parse_box_def` and so
// wrote `Navigator::box_def_cache` -- a member `loader_thread` also writes.
// `Navigator::parse_box_def_uncached` is that same parse without the cache store.
std::optional<DanBoxData> DanNavigator::load_dan_box_data(const fs::path& json_path) {
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
    std::vector<std::pair<std::string, std::string>> song_titles;   // ROUND 95
    if (doc.HasMember("charts")) {
        for (auto& chart : doc["charts"].GetArray()) {
            std::pair<std::string, std::string> t;
            if (auto entry = load_song_entry(chart, &t)) {
                songs.push_back(*entry);
                song_titles.push_back(std::move(t));
            }
        }
    }
    if (songs.empty()) return std::nullopt;

    std::vector<Exam> exams;
    if (doc.HasMember("exams")) {
        for (auto& e : doc["exams"].GetArray())
            exams.push_back(parse_exam(e));
    }

    DanBoxData d;
    d.json_path   = json_path;
    d.title       = title;
    d.color       = color;
    d.rank        = rank;
    d.dan_index   = dan_index;   // ROUND 50
    d.songs       = songs;
    d.song_titles = song_titles;   // ROUND 95
    d.exams       = exams;
    d.total_notes = total_notes_for(songs);
    // ROUND 57 — optional course-level `"gaiden": true` (Cabinet.GaidenDaniInfo
    // semantics: no 昇段/nameplate/congrats on a pass; see DanResultData).
    d.gaiden = doc.HasMember("gaiden") && doc["gaiden"].IsBool() &&
               doc["gaiden"].GetBool();
    return d;
}

// ROUND 95 — the main-thread half: `DanBox` reaches `tex`, so it is only ever
// built here.
std::unique_ptr<DanBox> DanNavigator::make_box(const DanBoxData& d) {
    auto box = std::make_unique<DanBox>(d.json_path, d.title, d.color,
                                        d.songs, d.exams, d.total_notes);
    box->dan_rank  = d.rank;
    box->dan_index = d.dan_index;
    box->gaiden    = d.gaiden;
    box->song_titles = d.song_titles;   // ROUND 95 -- see DanBox::load_text()
    return box;
}

std::unique_ptr<DanBox> DanNavigator::load_dan_box(const fs::path& json_path) {
    auto d = load_dan_box_data(json_path);
    if (!d) return nullptr;
    return make_box(*d);
}

// ROUND 66 (r66-danselect-empty-after-course) -- one root's worth of the dan
// walk, split out of init() so the same code can serve both the caller's roots
// and the default-library fallback below. Returns how many courses it added.
//
// Every reason to refuse a root is now checked and LOGGED here rather than left
// to the exception handler. The reported defect ("段位道場 is empty after I
// finish a course") arrived as
//     DanNavigator: error loading : filesystem error: recursive directory
//     iterator cannot open directory: Not a directory []
// -- an EMPTY root_path, which `fs::recursive_directory_iterator("")` turns into
// exactly that throw. Going through the catch made a bad input indistinguishable
// from a broken library, and the screen then presented zero courses, which is a
// dead end for the player (no course can be picked, and the only way out is the
// back key).
// ROUND 95 -- `scan_root`'s body, producing DanBoxData rather than DanBox, so it
// can run on the course-scan worker. `scan_root` below is unchanged in behaviour
// (this, then construct) and every ROUND 66 refusal/log is still made here.
int DanNavigator::scan_root_data(const fs::path& root_path, std::vector<DanBoxData>& out) {
    if (root_path.empty()) {
        spdlog::warn("DanNavigator: skipping an empty dan root path");
        return 0;
    }
    std::error_code ec;
    if (!fs::is_directory(root_path, ec)) {
        spdlog::warn("DanNavigator: skipping dan root '{}': not a directory ({})",
                     root_path.string(), ec ? ec.message() : "no such directory");
        return 0;
    }
    // A path at or inside a game's data holds no dan.json, only tens of
    // thousands of chart files this walk would crawl through.
    if (!gen4::find_data_root(root_path).empty() ||
        !green::find_data_root(root_path).empty()) return 0;

    int added = 0;
    try {
        auto it = fs::recursive_directory_iterator(
            root_path, fs::directory_options::skip_permission_denied);
        for (; it != fs::end(it); ++it) {
            if (scan_abort.load()) break;          // ROUND 95 -- cheap bail-out
            const auto& entry = *it;
            if (entry.is_directory() &&
                (gen4::find_data_root(entry.path()) == entry.path() ||
                 green::find_data_root(entry.path()) == entry.path())) {
                it.disable_recursion_pending();
                continue;
            }
            if (entry.path().filename() == "dan.json") {
                if (auto d = load_dan_box_data(entry.path())) {
                    out.push_back(std::move(*d));
                    added++;
                }
            }
        }
    } catch (const std::exception& ex) {
        spdlog::warn("DanNavigator: error loading {}: {}", root_path.string(), ex.what());
    }
    return added;
}

int DanNavigator::scan_root(const fs::path& root_path) {
    std::vector<DanBoxData> data;
    int added = scan_root_data(root_path, data);
    for (const DanBoxData& d : data) boxes.push_back(make_box(d));
    return added;
}

// ROUND 95 — the worker body. Produces data; touches no member except
// `scan_abort` (atomic). Everything below used to be the first half of `init()`.
std::vector<DanBoxData> DanNavigator::scan_all_data(const std::vector<fs::path>& song_paths) {
    std::vector<DanBoxData> data;

    // ROUND 95 -- `navigator.song_files` has exactly one writer,
    // `Navigator::song_files_thread`, and no reader synchronisation; this walk
    // reads it through `find_song_by_title`. On the main thread that race was
    // already there; from a worker it would be a second concurrent reader. Wait
    // it out HERE, on the worker, so the render loop is never the thing blocked.
    // (`song_files_ready` starts true, so an engine that never preloads or a
    // build with no such thread does not wait at all.)
    while (!navigator.song_files_ready.load() && !scan_abort.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(4));

    // ROUND 95 -- TEST HOOK, off unless the env var is set. On this machine's
    // library the scan (~2.1 s) is FASTER than the intro's 2950 ms cover, so the
    // "animation finishes first" branch of the rendezvous is never exercised by a
    // plain run. YATAIDON_R95_STALL=<ms> delays the worker so the clamp can be
    // measured live rather than only unit-tested. Nothing reads it in normal play.
    if (const char* stall = std::getenv("YATAIDON_R95_STALL")) {
        const int ms = std::atoi(stall);
        for (int i = 0; i < ms && !scan_abort.load(); i += 10)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (const fs::path& root_path : song_paths) {
        if (scan_abort.load()) return data;
        scan_root_data(root_path, data);
    }

    // ROUND 66 -- degrade to the whole library rather than to nothing.
    //
    // An empty ribbon is unrecoverable from inside the screen, so ONE bad root
    // must not be able to produce it. The library roots are where SONG_SELECT
    // itself found the 段位道場 folder in the first place
    // (`song_select.cpp`: `navigator.init(global_data.config->paths.tja_path)`),
    // so re-walking them rediscovers `Songs/11 Dan Dojo` (and any other course
    // library) exactly the way the first entry did. Deliberately independent of
    // whatever fixed the caller's path: this is the last line of defence.
    if (data.empty() && global_data.config) {
        for (const fs::path& lib_root : global_data.config->paths.tja_path) {
            if (scan_abort.load()) return data;
            if (std::find(song_paths.begin(), song_paths.end(), lib_root) != song_paths.end())
                continue;
            scan_root_data(lib_root, data);
        }
        if (!data.empty())
            spdlog::warn("DanNavigator: the requested dan root yielded nothing; "
                         "recovered {} course(s) by re-scanning the song library",
                         data.size());
    }
    return data;
}

// ROUND 95 — MAIN THREAD ONLY. Turns a finished data vector into the live
// ribbon. `boxes` is untouched by anything else until this runs, so draw() and
// update() can never observe a half-built strip.
void DanNavigator::publish(std::vector<DanBoxData>&& data) {
    boxes.clear();
    selected_index = 0;
    boxes.reserve(data.size());
    for (const DanBoxData& d : data) boxes.push_back(make_box(d));

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

// ── ROUND 95 — the async course scan ─────────────────────────────────────────
//
// The reported defect: 「他現在比較像先載入dan, 才載入段位道場動畫，你要邊放動畫邊讓
// 他同時載入dan」. `init()` blocked the render loop, and `on_screen_start` called it
// BEFORE arming the intro movie, so the wall-clock order was scan-then-animate.
// `begin_init()` starts the same walk on a worker and returns immediately, so
// `on_screen_start` can arm the movie first and the two overlap.
//
// The worker owns nothing shared: it writes only into its own local vector and
// hands that over once, under `scan_mutex`, with `scan_done` as the release. See
// scan_all_data() for the two Navigator hazards that had to be closed for that to
// be true.
void DanNavigator::begin_init(const std::vector<fs::path>& song_paths) {
    abort_init();                     // idempotent; a re-entry joins the old one
    boxes.clear();
    selected_index = 0;
    scan_done.store(false);
    scan_abort.store(false);
    scan_published = false;
    { std::lock_guard<std::mutex> lock(scan_mutex); scan_result.clear(); }

    scan_thread = std::thread([this, song_paths]() {
        std::vector<DanBoxData> data;
        try {
            data = scan_all_data(song_paths);
        } catch (const std::exception& ex) {
            // FAIL-SOFT: a throw here must not take the screen with it (a
            // generated-table `require` failure once killed the whole result
            // screen). Publish whatever was gathered; an empty ribbon is at
            // least escapable with the back key.
            spdlog::error("DanNavigator: course scan threw: {}", ex.what());
        } catch (...) {
            spdlog::error("DanNavigator: course scan threw (unknown)");
        }
        {
            std::lock_guard<std::mutex> lock(scan_mutex);
            scan_result = std::move(data);
        }
        scan_done.store(true);        // release: publisher for scan_result
    });
}

bool DanNavigator::poll_init() {
    if (scan_published) return true;
    if (!scan_thread.joinable()) return false;
    if (!scan_done.load()) return false;   // acquire
    scan_thread.join();
    std::vector<DanBoxData> data;
    { std::lock_guard<std::mutex> lock(scan_mutex); data = std::move(scan_result); scan_result.clear(); }
    publish(std::move(data));
    scan_published = true;
    return true;
}

void DanNavigator::abort_init() {
    scan_abort.store(true);
    if (scan_thread.joinable()) scan_thread.join();
    scan_abort.store(false);
    scan_done.store(false);
    scan_published = false;
    { std::lock_guard<std::mutex> lock(scan_mutex); scan_result.clear(); }
}

DanNavigator::~DanNavigator() { abort_init(); }

// The synchronous path, kept so every other caller (and YATAIDON_R95_LEGACY)
// behaves exactly as it did before ROUND 95: begin, wait, publish.
void DanNavigator::init(const std::vector<fs::path>& song_paths) {
    begin_init(song_paths);
    while (scan_thread.joinable() && !scan_done.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    poll_init();
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
    // ROUND 95 -- at most ONE text build per frame.
    //
    // Found while measuring the r95 rendezvous, and it is a second, smaller
    // instance of the same defect: `load_text()` builds this course's
    // OutlinedText objects (font rasterisation), and on the frame the ribbon is
    // first laid out EVERY on-screen box needs one at once. The r95 stall trace
    // (`scratchpad/r95/TIMELINE.md`) caught the render loop dropping to 0 frames
    // for ~1 s immediately after the courses landed -- i.e. right on the intro's
    // reveal, the most visible moment on the screen.
    //
    // Spreading it one box per frame is the cabinet's own shape here: its
    // `SceneDaniSelect::Preparing` (CHN05 decompile :601) runs exactly one
    // `EnsoDataManager::UpdateDataTable` slice per frame while the Lua scene
    // keeps drawing. Worst case is one box per frame over the ribbon, ~26 frames
    // = ~217 ms at 120 fps, and a box is never DRAWN before its text exists
    // because draw() and this walk share the same on-screen test.
    bool built_one = false;
    for (auto& b : boxes) {
        bool on_screen = b->position > -156 * tex.screen_scale && b->position < tex.screen_width + 144 * tex.screen_scale;
        if (on_screen && !b->text_loaded && !built_one) {
            b->load_text();
            built_one = true;
        }
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

    // ROUND 73 (QA defect 4) -- the 1P Don + nameplate, built exactly the way
    // DanResultScreen builds its pair (scenes/dan_result.cpp on_screen_start).
    {
        auto pd = scores_manager.get_player_data(get_player_id(global_data.player_num));
        chara = make_chara_from_player_data(pd ? &*pd : nullptr);
        if (pd) {
            chara->set_don_colors(pd->chara_color_1, pd->chara_color_2, pd->chara_color_3);
            chara->apply_face(pd->chara_face_index);
        } else {
            chara->set_don_colors(chara_default_color_1(get_player_id(global_data.player_num)),
                                  chara_default_color_2(get_player_id(global_data.player_num)),
                                  {249, 240, 225, 255});
        }
        chara->set_anim(AnimIndex::DON_NORMAL);
        nameplate = Nameplate(pd ? pd->username : "", pd ? pd->title : "",
                              global_data.player_num,
                              pd ? pd->dan : -1, pd ? pd->gold : false,
                              pd ? pd->rainbow : false, pd ? pd->title_bg : 0);
    }

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

    // ROUND 66 (r66-danselect-empty-after-course) -- the reported defect:
    // 「當我完成一次段位，回到段位畫面」 the dojo came back EMPTY, with
    //     DanNavigator: error loading : filesystem error: recursive directory
    //     iterator cannot open directory: Not a directory []
    // in the log -- an EMPTY root path (the format arg is root_path.string()).
    //
    // Cause: `SessionData::selected_dan_folder` is written ONCE, by SONG_SELECT
    // (song_select/player.cpp select_song), and `DanResultScreen::on_screen_end`
    // calls `reset_session()`, which default-constructs session_data[1] and [2].
    // DAN_RESULT's exit screen is DAN_SELECT, so the second entry to the dojo
    // always read a wiped path. Both dan return paths go through DAN_RESULT
    // (natural course end and ROUND 47's fail-out), so both were affected.
    //
    // `GlobalData::dan_folder` is the same path in a slot reset_session() does
    // not touch (same rationale as `last_difficulty`). Prefer the session value
    // so nothing changes on the FIRST entry, and restore it from the persistent
    // copy when the session has been reset, so everything downstream that reads
    // `selected_dan_folder` sees a live path again.
    SessionData& sd_boot = global_data.session_data[(int)global_data.player_num];
    if (sd_boot.selected_dan_folder.empty() &&
        (int)global_data.player_num < (int)global_data.dan_folder.size())
        sd_boot.selected_dan_folder = global_data.dan_folder[(int)global_data.player_num];
    fs::path dan_folder = sd_boot.selected_dan_folder;

    // ── ROUND 95 (r95-danselect-concurrent-intro) ───────────────────────────
    //
    // 「他現在比較像先載入dan, 才載入段位道場動畫，你要邊放動畫邊讓他同時載入dan」
    //
    // ROUND 64's ordering — CORRECTED BY ROUND 95, in place rather than silently
    // rewritten (house style; see ROUND 87's correction of ROUND 47). It read
    //
    //     dan_navigator.init({dan_folder});   // blocking, multi-second
    //     screen_start_ms = get_current_ms(); // "AFTER the scan on purpose, so
    //                                         //  the intro window is wall-clock
    //                                         //  from the first frame that
    //                                         //  actually renders"
    //
    // That reasoning was SOUND FOR A BLOCKING SCAN -- anchoring the window to the
    // screen change would have made the doors jump straight to open on the first
    // frame the render loop came back. But it bakes in the wrong wall-clock order:
    // the movie whose entire purpose is to hide the load ran AFTER it. ROUND 64's
    // comment is superseded; the scan is no longer blocking, so the anchor no
    // longer has to hide behind it.
    //
    // New order: stamp the clock, arm the movie, THEN start the scan on a worker.
    // The rendezvous (both completion orders) is documented on `scan_ready` in
    // dan_select.h and implemented in tick_timer() + publish_scan_state().
    select_timer.reset();
    timer_started   = false;
    timer_fired     = false;
    screen_start_ms = get_current_ms();
    scan_ready      = false;
    scan_ready_ms   = 0;
    scan_begin_ms   = screen_start_ms;
    traced_end      = false;
    legacy_blocking = (std::getenv("YATAIDON_R95_LEGACY") != nullptr);
    trace_timeline  = (std::getenv("YATAIDON_R95_TRACE")  != nullptr);

    if (legacy_blocking) {
        // The pre-ROUND-95 order, kept behind an env gate so the before/after
        // timeline is one binary rather than two builds (a stale exe has made a
        // whole A/B meaningless in this project before).
        if (trace_timeline) spdlog::info("R95 scan begin (legacy blocking) t=+0.00 ms");
        dan_navigator.init({dan_folder});
        screen_start_ms = get_current_ms();          // ROUND 64's anchor, verbatim
        scan_ready      = true;
        scan_ready_ms   = screen_start_ms;
        if (trace_timeline)
            spdlog::info("R95 scan end   (legacy blocking) t=+{:.2f} ms  courses={}",
                         screen_start_ms - scan_begin_ms, dan_navigator.boxes.size());
    } else {
        if (trace_timeline) spdlog::info("R95 scan begin t=+0.00 ms (worker)");
        dan_navigator.begin_init({dan_folder});
    }

    // ─── ROUND 86 (r86) — the 段位道場 introduction movie, on BOTH entry paths ────
    //
    // Reported defect 2: 「段位道場的進入動畫消失了」. Root cause is a one-line gate in
    // the skin, not in the movie: `Scripts/song_select/dan_doors.lua` splits the clip in
    // two halves, and `M.armed_open` — the flag `M.draw_open()` (l.418) refuses to run
    // without — is set in exactly ONE place, `M.draw()` (l.386-389), the SONG_SELECT half.
    // ROUND 83's new ENTRY -> DAN_SELECT route never touches SONG_SELECT, so the flag was
    // never set and the DAN_SELECT half returned false on every frame.
    //
    // The cabinet's own route into the dojo is the mode board, and there the WHOLE movie
    // runs on DAN_SELECT: `dani_select_introduction_main.lua` loads
    // `enso_dani/loading_dani/loading_dani_intro` as the top layer of the scene and
    // `dani_select_all.lua`'s IntroductionMain plays `mc_main.main:GotoAndPlay("in")` —
    // label `in` = frame 5 — through stop@248 and out@253..262. It opens on a full-screen
    // black (`#1@3` alpha 1.0 for f0..55, easing to 0.5 by f62 — Scripts/anim/dani_intro.lua),
    // which is why ENTRY -> DAN_SELECT also needs no generic screen fade (YataiDON.cpp
    // screen_fade_applies). So: from ENTRY play f5..262, from SONG_SELECT keep ROUND 64's
    // f182..262 reveal, because the close+stamp half has already been drawn over the wheel.
    const bool from_entry = (global_data.previous_screen == "ENTRY");
    if (script_manager.lua)
        (*script_manager.lua)["__hss_dan_intro"] = from_entry ? "full" : "open";
    intro_ms = from_entry ? DAN_INTRO_FULL_MS : DAN_INTRO_MS;
    // ROUND 95 -- the skin reads this every frame; make sure it is defined on the
    // first one, before draw_open() can ever see a nil.
    publish_scan_state(screen_start_ms);
    if (trace_timeline)
        spdlog::info("R95 intro arm  t=+{:.2f} ms  leg={}  intro_ms={:.1f}  cover_ms={:.1f}",
                     get_current_ms() - scan_begin_ms, from_entry ? "full(f5..262)" : "open(f182..262)",
                     intro_ms, intro_ms - DAN_INTRO_MS);
}

// ── ROUND 95 — the engine half of the clamp ──────────────────────────────────
//
// `__hss_dan_ready` is written EVERY frame (not one-shot like ROUND 86's
// `__hss_dan_intro`): `Scripts/song_select/dan_doors.lua` M.draw_open() holds the
// clip on f182 -- doors shut, scrim up, the last frame before anything behind
// them is visible -- while it is false, and re-anchors on f182 the frame it turns
// true. Same construction as ROUND 85's f113 clamp on the genre board.
//
// This is also where the scan's completion is picked up (`poll_init`, main
// thread) and where the FAIL-SOFT timeout lives: a scan that has not landed
// within SCAN_TIMEOUT_MS stops holding the reveal, so the player gets an empty
// but escapable ribbon rather than a permanent black scrim. The worker's later
// publish simply fills the ribbon in.
void DanSelectScreen::publish_scan_state(double current_ms) {
    if (!scan_ready) {
        if (dan_navigator.poll_init()) {
            scan_ready    = true;
            scan_ready_ms = current_ms;
            if (trace_timeline)
                spdlog::info("R95 scan end   t=+{:.2f} ms  courses={}",
                             current_ms - scan_begin_ms, dan_navigator.boxes.size());
        } else if (current_ms - scan_begin_ms > SCAN_TIMEOUT_MS) {
            scan_ready    = true;
            scan_ready_ms = current_ms;
            spdlog::error("DanNavigator: course scan still running after {:.0f} ms; "
                          "opening the dojo anyway (the ribbon will fill in when it lands)",
                          SCAN_TIMEOUT_MS);
        }
    } else {
        // Still poll: after a timeout the worker may land later, and the ribbon
        // must appear when it does.
        dan_navigator.poll_init();
    }
    if (script_manager.lua)
        (*script_manager.lua)["__hss_dan_ready"] = scan_ready;
}

// ROUND 86 — `intro_ms` (set in on_screen_start) is the leg this entry actually plays;
// the two constants and the reasoning are at the top of this file.
//
// ROUND 95 — and it is no longer a plain `screen_start_ms + intro_ms` deadline,
// because the movie no longer runs after the load: it runs OVER it. The clip's
// reveal (f182..f262, DAN_INTRO_MS) cannot begin before the ribbon exists, so
//
//     cover_ms     = intro_ms - DAN_INTRO_MS      2950.0 (ENTRY) / 0.0 (SONG_SELECT)
//     reveal_start = max(screen_start_ms + cover_ms, scan_ready_ms)
//     intro_end    = reveal_start + DAN_INTRO_MS
//
// which is exactly the formula dan_doors.lua's clamp+re-anchor produces, so the
// credit clock and the movie can never disagree about when the screen went live.
// This is the cabinet's `DaniSelectMain` gate (`if daniselectdani_.is_setUpAllOk
// == true then this.timer_:StartCount()`, dani_select_all.lua:126-128): the clock
// starts at max(intro end, board ready), NOT at the intro's end alone. And the
// intro is never cut short when the scan wins the race -- the cabinet's
// `Is_EndAnimation` is `mc_main.main:IsPlay() == false`, purely the clip.
std::optional<Screens> DanSelectScreen::tick_timer(double current_ms) {
    if (!timer_started) {
        const double cover_ms     = intro_ms - DAN_INTRO_MS;
        const double reveal_start = std::max(screen_start_ms + cover_ms,
                                             scan_ready ? scan_ready_ms : current_ms);
        if (!scan_ready || current_ms < reveal_start + DAN_INTRO_MS) return std::nullopt;
        if (trace_timeline && !traced_end) {
            traced_end = true;
            spdlog::info("R95 intro end / screen ready t=+{:.2f} ms  (reveal began +{:.2f} ms)",
                         current_ms - scan_begin_ms, reveal_start - scan_begin_ms);
        }
        timer_started = true;
        select_timer  = std::make_unique<Timer>(100, current_ms, [this]() {
            timer_fired = true;
        });
    }
    if (!select_timer) return std::nullopt;
    select_timer->update(current_ms);

    if (!timer_fired) return std::nullopt;
    timer_fired = false;
    if (state == SongSelectState::BROWSING) {
        // DaniSelectMain's IsZero branch: TimeUpBegin + se_common_v12a/don_big,
        // which decides the course under the cursor -- the same path a don
        // takes, so it goes through open_confirm().
        audio.play_sound("don_big", VolumePreset::SOUND);
        open_confirm(current_ms);
        return std::nullopt;
    }
    // ConfirmationMain's `Timeup` state walks selectIndex onto 挑戦する
    // (kselectYes = 2, lines 133-145) and decides it, then StopCount()s.
    if (modifier_selector.has_value()) modifier_selector.reset();
    confirm_index = CONFIRM_YES;
    select_timer.reset();
    audio.play_sound("don", VolumePreset::SOUND);
    return on_screen_end(Screens::GAME_DAN);
}

// ROUND 64 -- the "a course was decided" path, shared by a don on the wheel and
// by the wheel's own time-up (dani_select_all.lua:155-168, which runs the same
// ReStartDaniSelect -> ConfirmationMain hand-off for both).
void DanSelectScreen::open_confirm(double current_ms) {
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
    // ROUND 64 -- dani_select_all.lua:159-163. The dialog gets AT LEAST 30 s:
    // a clock already under 30 (or one that has just run out) is reset to 30
    // and restarted, a healthier one is left alone.
    if (select_timer) {
        const int left = select_timer->time();
        if (left >= 0 && left < 30)
            select_timer = std::make_unique<Timer>(30, current_ms, [this]() {
                timer_fired = true;
            });
    }
}

Screens DanSelectScreen::on_screen_end(Screens next_screen) {
    // ROUND 95 -- leaving the screen (back key, or a course decided before the
    // scan finished) must never leave the worker running against a navigator that
    // is about to be re-inited. abort_init() is a no-op once it has published.
    dan_navigator.abort_init();
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
        open_confirm(current_ms);   // ROUND 64: shared with the wheel time-up
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
    // ROUND 95 -- FIRST: pick the worker's result up (main thread) and republish
    // `__hss_dan_ready` for this frame, before anything else can draw.
    publish_scan_state(current_ms);
    allnet_indicator.update(current_ms);
    dan_navigator.update(current_ms);
    indicator->update(current_ms);
    confirm_fade->update(current_ms);
    // ROUND 73 (QA defect 4) -- the Don breathes/blinks on this screen exactly as
    // it does on DAN_RESULT; without the tick it would stand on frame 0.
    nameplate.update(current_ms);
    if (chara) chara->update(current_ms);
    if (auto next = tick_timer(current_ms)) return next;   // ROUND 64

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

    // ROUND 73 (QA defect 4) -- the 1P Don + nameplate, at the cabinet's OWN
    // registration points on THIS screen (not another screen's anchor).
    //
    // `dani_select.nulm` sprite 315 (`main`) places, with the matrix constant
    // across the whole 260-frame timeline and alpha 256 from f30 on:
    //     depth 7  don_1p             tx (-740, 246)
    //     depth 8  plate_1p_instance  tx (-740, 430)
    // Sprite 315's own origin is stage (960,540) -- proved on the same frame by
    // `window_instance` and `qricon_instance`, two full-screen overlays whose
    // authored content is in absolute stage coords and which are placed at
    // exactly (-960,-540). So screen = local + (960,540):
    //     don_1p reg    = (220, 786)
    //     plate_1p reg  = (220, 970)
    // Converting to our objects' anchors with the SAME two offsets ROUND 16
    // measured and dan_result.cpp uses (Chara3D anchors at the feet = reg +
    // (-33,+148); Nameplate is drawn from its top-left = reg - (196.6, 48)):
    //     chara      -> (187.0, 934.0)
    //     nameplate  -> ( 23.4, 922.0)
    //
    // SCALE: `don_1p` (char 294) places its `don1p` child with matrix a=d=0.8,
    // so the cabinet draws the dan-select Don at 80 %, unlike DAN_RESULT's 1.0.
    // (This does NOT contradict ROUND 64's "the board is 1:1": that finding was
    // about the board, sprite 185, and it still measures 1:1 -- the 0.8 lives
    // inside the Don's own container, one level below the board.)
    //
    // Drawn here so the z-order matches the cabinet's depths: over the board and
    // the task-name plates, under `window_instance` (the confirmation dialog,
    // drawn immediately below) and under the timer/QR/indicator layers.
    if (chara) chara->draw(187.0f, 934.0f, 0.8f);
    nameplate.draw(23.4f, 922.0f);

    if (state == SongSelectState::SONG_SELECTED) {
        draw_confirm_overlay();
        // Over the dialog, as the cabinet's `option_instance` (root depth 13)
        // sits above `window_instance` (depth 12).
        if (modifier_selector.has_value()) modifier_selector->draw();
    }

    indicator->draw(tex.skin_config[SC::DAN_SELECT_INDICATOR].x, tex.skin_config[SC::DAN_SELECT_INDICATOR].y);
    tex.draw_texture(GLOBAL::DAN_SELECT, {});
    allnet_indicator.draw();

    // ROUND 64 -- the credit clock. `timer_instance` is root depth 14 in
    // dani_select.nulm, above every board and plate and below only the QR icon
    // and the introduction movie, so it is drawn here: over the ribbon and the
    // DAN_SELECT plate, under Indicator::draw_top's shutter. Its screen
    // position is the timer movie's own and is therefore identical to
    // SONG_SELECT's (Graphics/global/timer/texture.json), which is exactly what
    // the cabinet does -- one timer movie, one place on screen.
    if (select_timer) select_timer->draw();

    // ROUND 17: the skin's top-most slot on this screen, above the coin overlay,
    // the DAN_SELECT plate and the allnet chip. The 段位道場 shutter's SECOND half
    // (the doors opening to reveal this screen) is drawn from here; see
    // Indicator::draw_top and Scripts/song_select/dan_doors.lua.
    indicator->draw_top();
}
