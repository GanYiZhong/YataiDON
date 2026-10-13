#include "lua_bytecode_cache.h"

#include "ray.h"
#ifdef _WIN32
#include "../platform/platform_windows.h"
#endif

#include <lua.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace lua_bytecode_cache {
namespace {

constexpr char kMagic[4] = {'Y', 'D', 'L', 'C'};

// Smaller scripts compile in well under a millisecond, less than the extra seek a cache
// entry in another folder costs on a hard drive; only big (generated data) chunks are kept.
constexpr uint64_t kMinSourceBytes = 128 * 1024;

struct Stamp {
    uint64_t size = 0;
    int64_t mtime = 0;
};

bool stamp_of(const std::string& path, Stamp& out) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) return false;
    const auto t = fs::last_write_time(path, ec);
    if (ec) return false;
    out.size = static_cast<uint64_t>(size);
    out.mtime = static_cast<int64_t>(t.time_since_epoch().count());
    return true;
}

fs::path entry_path(const std::string& path) {
    uint64_t h = 1469598103934665603ULL;   // FNV-1a of the path as spelled
    for (unsigned char c : path) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.luac", static_cast<unsigned long long>(h));
    return fs::path("cache") / "lua_bytecode" / name;
}

template <typename T>
void put(std::string& out, T v) {
    out.append(reinterpret_cast<const char*>(&v), sizeof(v));
}

// Entry: magic, Lua release, source size and mtime, path, bytecode size, then the bytecode
// deflated (it shrinks several times, so an entry is less to read than its source).
std::string header(const std::string& path, const Stamp& st) {
    std::string h(kMagic, sizeof(kMagic));
    put<uint32_t>(h, LUA_VERSION_RELEASE_NUM);
    put<uint64_t>(h, st.size);
    put<int64_t>(h, st.mtime);
    put<uint32_t>(h, static_cast<uint32_t>(path.size()));
    h += path;
    return h;
}

// Opens path's entry positioned after its header when the entry is current.
bool open_current(const std::string& path, const Stamp& st, std::ifstream& in) {
    in.open(entry_path(path), std::ios::binary);
    if (!in) return false;
    const std::string want = header(path, st);
    std::string got(want.size(), '\0');
    return in.read(got.data(), static_cast<std::streamsize>(got.size())) && got == want;
}

// The bytecode stored for path, or empty when there is no current entry.
std::string read_entry(const std::string& path, const Stamp& st) {
    std::ifstream in;
    if (!open_current(path, st, in)) return {};
    uint64_t raw_size = 0;
    if (!in.read(reinterpret_cast<char*>(&raw_size), sizeof(raw_size))) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string packed = ss.str();
    int out_size = 0;
    unsigned char* raw = ray::DecompressData(reinterpret_cast<const unsigned char*>(packed.data()),
                                             static_cast<int>(packed.size()), &out_size);
    if (!raw) return {};
    std::string bc;
    if (static_cast<uint64_t>(out_size) == raw_size) bc.assign(reinterpret_cast<char*>(raw), out_size);
    ray::MemFree(raw);
    return bc;
}

int writer(lua_State*, const void* p, size_t sz, void* ud) {
    static_cast<std::string*>(ud)->append(static_cast<const char*>(p), sz);
    return 0;
}

// Dumps the function on top of the stack (left there) into its cache entry.
void write_entry(lua_State* L, const std::string& path, const Stamp& st) {
    std::string bc;
    if (lua_dump(L, writer, &bc, 0) != 0 || bc.empty()) return;
    int packed_size = 0;
    unsigned char* packed = ray::CompressData(reinterpret_cast<const unsigned char*>(bc.data()),
                                              static_cast<int>(bc.size()), &packed_size);
    if (!packed) return;
    const fs::path file = entry_path(path);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // written aside and renamed in, so a reader never sees half an entry
    std::ostringstream tmp_name;
    tmp_name << file.string() << "." << std::this_thread::get_id() << ".tmp";
    const fs::path tmp = tmp_name.str();
    bool ok = false;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (out) {
            const uint64_t raw_size = bc.size();
            out << header(path, st);
            out.write(reinterpret_cast<const char*>(&raw_size), sizeof(raw_size));
            out.write(reinterpret_cast<const char*>(packed), packed_size);
            ok = static_cast<bool>(out);
        }
    }
    ray::MemFree(packed);
    if (ok) fs::rename(tmp, file, ec);
    if (!ok || ec) fs::remove(tmp, ec);
}

int l_loadfile(lua_State* L) {
    // loadfile(file [, mode [, env]]): only the plain form is cached
    if (!lua_isstring(L, 1) || !lua_isnoneornil(L, 2) || !lua_isnoneornil(L, 3)) {
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_insert(L, 1);
        lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
        return lua_gettop(L);
    }
    if (load(L, lua_tostring(L, 1)) == LUA_OK) return 1;
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;   // nil, message
}

int l_searcher(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchpath");
    lua_pushstring(L, name);
    lua_getfield(L, -3, "path");
    lua_call(L, 2, 2);
    if (lua_isnil(L, -2)) return 1;   // the "no file ..." list
    lua_pop(L, 1);
    const std::string file = lua_tostring(L, -1);
    if (load(L, file) != LUA_OK)
        return luaL_error(L, "error loading module '%s' from file '%s':\n\t%s", name, file.c_str(), lua_tostring(L, -1));
    lua_pushstring(L, file.c_str());
    return 2;
}

std::atomic<bool> prewarm_stop{false};

// joined at exit too, so a prewarm still running when the game quits doesn't terminate it
struct PrewarmThread {
    std::thread t;
    ~PrewarmThread() {
        prewarm_stop = true;
        if (t.joinable()) t.join();
    }
} prewarm_thread;

}  // namespace

int load(lua_State* L, const std::string& path) {
    Stamp st;
    if (!stamp_of(path, st) || st.size < kMinSourceBytes) return luaL_loadfilex(L, path.c_str(), nullptr);

    const std::string bc = read_entry(path, st);
    if (!bc.empty()) {
        if (luaL_loadbufferx(L, bc.data(), bc.size(), ("@" + path).c_str(), "b") == LUA_OK) return LUA_OK;
        lua_pop(L, 1);   // unreadable entry: compile the source again
    }
    const int status = luaL_loadfilex(L, path.c_str(), nullptr);
    if (status == LUA_OK) write_entry(L, path, st);
    return status;
}

void install(lua_State* L) {
    lua_getglobal(L, "loadfile");
    lua_pushcclosure(L, l_loadfile, 1);
    lua_setglobal(L, "loadfile");

    // package.searchers[2] is the Lua file searcher
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchers");
    lua_pushcfunction(L, l_searcher);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 2);
}

void prewarm(const std::vector<std::string>& script_dirs) {
#ifdef __EMSCRIPTEN__
    (void)script_dirs;   // no threads; entries are still written as scripts load
    return;
#endif
    stop_prewarm();
    prewarm_stop = false;
    prewarm_thread.t = std::thread([script_dirs]() {
#ifdef _WIN32
        win32_background_mode_thread();   // low CPU and I/O priority, so the game's own reads go first
#endif
        lua_State* L = luaL_newstate();
        if (!L) return;
        int compiled = 0;
        for (const std::string& dir : script_dirs) {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                if (prewarm_stop) break;
                if (!it->is_regular_file(ec) || it->path().extension() != ".lua") continue;
                const std::string path = dir + "/" + it->path().lexically_relative(dir).generic_string();
                Stamp st;
                if (!stamp_of(path, st) || st.size < kMinSourceBytes) continue;
                std::ifstream current;
                if (open_current(path, st, current)) continue;   // only its header is read
                if (luaL_loadfilex(L, path.c_str(), nullptr) == LUA_OK) {
                    write_entry(L, path, st);
                    ++compiled;
                }
                lua_settop(L, 0);
            }
        }
        lua_close(L);
        if (compiled > 0) spdlog::info("Lua bytecode cache: compiled {} script(s)", compiled);
    });
}

void stop_prewarm() {
    prewarm_stop = true;
    if (prewarm_thread.t.joinable()) prewarm_thread.t.join();
}

}  // namespace lua_bytecode_cache
