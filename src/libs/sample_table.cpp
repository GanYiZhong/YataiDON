#include "sample_table.h"
#include "texture.h"
#include <mutex>
#include <sol/sol.hpp>
#include <spdlog/spdlog.h>

// ---------------------------------------------------------------------------
// shared plumbing
// ---------------------------------------------------------------------------

namespace {

// Resolve Scripts/anim/<name>.lua child-first, parent fallback -- the same
// order Lua's `require("anim/<name>")` sees through package.path
// (ScriptManager::init indexes the child Scripts dir before the parent's).
fs::path resolve_table_path(const std::string& name) {
    fs::path rel = fs::path("Scripts") / "anim" / (name + ".lua");
    fs::path child = tex.skin_root() / rel;
    if (fs::exists(child)) return child;
    if (tex.has_parent_skin()) {
        fs::path parent = tex.parent_root() / rel;
        if (fs::exists(parent)) return parent;
    }
    return {};
}

// One private Lua state per load; the generated files are pure data
// (`return { ... }`) so no libraries are opened.
sol::table load_lua_table(sol::state& lua, const fs::path& path) {
    auto result = lua.safe_script_file(path.string(), sol::script_pass_on_error);
    if (!result.valid()) {
        sol::error err = result;
        throw std::runtime_error(err.what());
    }
    sol::object obj = result;
    if (obj.get_type() != sol::type::table)
        throw std::runtime_error("file did not return a table");
    return obj.as<sol::table>();
}

std::mutex g_cache_mutex;

}  // namespace

// ---------------------------------------------------------------------------
// SampleTable
// ---------------------------------------------------------------------------

double SampleTable::sample(const SampleTrack& trk, int field_col, double frame,
                           bool step) const {
    const auto& rows = trk.rows;
    if (rows.empty() || field_col < 0) return 0.0;
    const size_t col = static_cast<size_t>(field_col) + 1;   // rows[i][0] = frame
    if (frame <= rows.front()[0]) return rows.front().size() > col ? rows.front()[col] : 0.0;
    if (frame >= rows.back()[0])  return rows.back().size()  > col ? rows.back()[col]  : 0.0;

    // binary search: last row whose frame <= frame (sampler.lua's seek())
    size_t lo = 0, hi = rows.size() - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (rows[mid][0] <= frame) lo = mid; else hi = mid - 1;
    }
    const auto& a = rows[lo];
    if (a.size() <= col) return 0.0;
    if (step || lo + 1 >= rows.size()) return a[col];
    const auto& b = rows[lo + 1];
    if (b.size() <= col || b[0] <= a[0]) return a[col];
    double u = (frame - a[0]) / (b[0] - a[0]);
    return a[col] + (b[col] - a[col]) * u;
}

const SampleTable* get_sample_table(const std::string& name) {
    static std::unordered_map<std::string, std::unique_ptr<SampleTable>> cache;
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.get();

    // negative results are cached too (nullptr), so a missing table warns once
    auto& slot = cache[name];
    fs::path path = resolve_table_path(name);
    if (path.empty()) {
        spdlog::warn("[sample_table] Scripts/anim/{}.lua not found in skin or parent"
                     " - consumers keep their fallback behaviour", name);
        return nullptr;
    }
    try {
        sol::state lua;
        sol::table t = load_lua_table(lua, path);

        auto st = std::make_unique<SampleTable>();
        st->name = name;
        st->fps = t.get_or("fps", 60.0);
        if (sol::optional<sol::table> range = t["range"]) {
            st->range0 = range->get_or(1, 0.0);
            st->range1 = range->get_or(2, 0.0);
        }
        sol::table fields = t["fields"];
        for (size_t i = 1; i <= fields.size(); i++) {
            std::string f = fields[i];
            st->field_idx[f] = static_cast<int>(st->fields.size());
            st->fields.push_back(f);
        }
        if (st->fields.empty()) throw std::runtime_error("no fields");
        if (sol::optional<sol::table> labels = t["labels"]) {
            for (auto& [k, v] : *labels) {
                if (k.is<std::string>() && v.is<double>())
                    st->labels[k.as<std::string>()] = v.as<double>();
            }
        }
        sol::table tracks = t["tracks"];
        const size_t want = st->fields.size() + 1;
        for (auto& [k, v] : tracks) {
            if (!k.is<std::string>() || !v.is<sol::table>()) continue;
            SampleTrack trk;
            sol::table rows = v.as<sol::table>();
            for (size_t i = 1; i <= rows.size(); i++) {
                sol::table row = rows[i];
                std::vector<double> r;
                r.reserve(want);
                for (size_t j = 1; j <= want; j++) r.push_back(row.get_or(j, 0.0));
                trk.rows.push_back(std::move(r));
            }
            if (!trk.rows.empty())
                st->tracks[k.as<std::string>()] = std::move(trk);
        }
        if (st->tracks.empty()) throw std::runtime_error("no tracks");

        spdlog::info("[sample_table] loaded {} ({} tracks, {} fields, fps {})",
                     path.string(), st->tracks.size(), st->fields.size(), st->fps);
        slot = std::move(st);
    } catch (const std::exception& e) {
        spdlog::warn("[sample_table] failed to load {}: {} - consumers keep their"
                     " fallback behaviour", path.string(), e.what());
        slot.reset();
    }
    return slot.get();
}

// ---------------------------------------------------------------------------
// ClipMap
// ---------------------------------------------------------------------------

const ClipMap* get_clip_map(const std::string& name) {
    static std::unordered_map<std::string, std::unique_ptr<ClipMap>> cache;
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.get();

    auto& slot = cache[name];
    fs::path path = resolve_table_path(name);
    if (path.empty()) {
        spdlog::warn("[sample_table] clip map Scripts/anim/{}.lua not found", name);
        return nullptr;
    }
    try {
        sol::state lua;
        sol::table t = load_lua_table(lua, path);

        auto cm = std::make_unique<ClipMap>();
        cm->table = t.get_or<std::string>("table", "");
        if (cm->table.empty()) throw std::runtime_error("no `table` key");
        if (sol::optional<sol::table> r = t["intro"]) {
            cm->intro0 = r->get_or(1, 0.0);
            cm->intro1 = r->get_or(2, -1.0);
        }
        if (sol::optional<sol::table> r = t["loop"]) {
            cm->loop0 = r->get_or(1, 0.0);
            cm->loop1 = r->get_or(2, 0.0);
        }
        sol::table draws = t["draws"];
        for (size_t i = 1; i <= draws.size(); i++) {
            sol::table d = draws[i];
            ClipMapDraw e;
            e.track = d.get_or<std::string>(1, "");
            e.tex   = d.get_or<std::string>(2, "");
            e.ox    = static_cast<float>(d.get_or(3, 0.0));
            e.oy    = static_cast<float>(d.get_or(4, 0.0));
            if (e.track.empty() || e.tex.empty())
                throw std::runtime_error("draw entry " + std::to_string(i) +
                                         " missing track/tex");
            cm->draws.push_back(std::move(e));
        }
        if (cm->draws.empty()) throw std::runtime_error("no draws");

        spdlog::info("[sample_table] loaded clip map {} ({} draws -> table {})",
                     path.string(), cm->draws.size(), cm->table);
        slot = std::move(cm);
    } catch (const std::exception& e) {
        spdlog::warn("[sample_table] failed to load clip map {}: {}", path.string(),
                     e.what());
        slot.reset();
    }
    return slot.get();
}
