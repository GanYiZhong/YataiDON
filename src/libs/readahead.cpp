#include "readahead.h"

#include "filesystem.h"
#include "global_data.h"
#ifdef _WIN32
#include "../platform/platform_windows.h"
#endif

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__linux__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace readahead {
namespace {

// Where a screen usually leads, by screen name (Graphics/<name>, Sounds/<name>).
const std::unordered_map<std::string, std::vector<std::string>>& next_screens() {
    static const std::unordered_map<std::string, std::vector<std::string>> map = {
        {"title",       {"entry", "song_select"}},
        {"entry",       {"song_select"}},
        {"song_select", {"game", "result"}},
        {"dan_select",  {"game", "dan_result"}},
        {"game",        {"result"}},
        {"result",      {"song_select"}},
        {"dan_result",  {"dan_select"}},
        {"retry_game",  {"game"}},
    };
    return map;
}

// a cap per request, so a large skin can't push the whole page cache out
constexpr uintmax_t kMaxBytes = uintmax_t{512} << 20;

struct Request {
    std::vector<fs::path> roots;   // the skin's root, then its ancestors'
    std::vector<std::string> screens;
};

std::mutex mutex;
std::condition_variable cv;
std::optional<Request> queued;   // the latest request, taken by the thread
uint64_t generation = 0;         // bumped by every request; the thread drops older work
bool quit = false;

void shut_down();

// joined at exit too, so a read still running when the game quits doesn't terminate it
struct Worker {
    std::thread t;
    ~Worker() { shut_down(); }
} worker;

bool superseded(uint64_t gen) {
    std::lock_guard<std::mutex> lock(mutex);
    return quit || generation != gen;
}

// Brings one file into the page cache; false when a newer request (or quit) came in.
bool warm(const fs::path& path, uint64_t gen) {
#if defined(__linux__) || defined(__ANDROID__)
    (void)gen;
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return true;
    ::posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);   // the kernel reads it in asynchronously
    ::close(fd);
    return true;
#else
    std::ifstream in(path, std::ios::binary);
    static thread_local std::vector<char> buf(1 << 20);
    while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0) {
        if (superseded(gen)) return false;
    }
    return true;
#endif
}

// Reads the request's folders in, one after another; stops early for a newer request.
void serve(const Request& req, uint64_t gen) {
    uintmax_t total = 0;
    for (const std::string& screen : req.screens) {
        for (const fs::path& root : req.roots) {
            for (const char* kind : {"Graphics", "Sounds"}) {
                const fs::path dir = root / kind / screen;
                std::error_code ec;
                if (!fs::is_directory(dir, ec)) continue;
                for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                    if (superseded(gen)) return;
                    if (!it->is_regular_file(ec)) continue;
                    const uintmax_t size = it->file_size(ec);
                    if (ec) continue;
                    if (total + size > kMaxBytes) return;
                    total += size;
                    if (!warm(it->path(), gen)) return;
                }
            }
        }
    }
}

void run() {
#ifdef _WIN32
    win32_background_mode_thread();   // low CPU and I/O priority, so the game's own reads go first
#endif
    while (true) {
        Request req;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [] { return quit || queued.has_value(); });
            if (quit) return;
            req = std::move(*queued);
            queued.reset();
            gen = generation;
        }
        serve(req, gen);
    }
}

void shut_down() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        quit = true;
        ++generation;
    }
    cv.notify_all();
    if (worker.t.joinable()) worker.t.join();
}

}  // namespace

void after_screen(const std::string& screen_name) {
#ifdef __EMSCRIPTEN__
    (void)screen_name;   // no threads, and the files are in memory already
    return;
#else
    const auto& map = next_screens();
    auto it = map.find(screen_name);

    std::lock_guard<std::mutex> lock(mutex);
    ++generation;   // whatever the previous screen asked for is stale now
    queued.reset();
    if (it == map.end() || !global_data.config || quit) return;
    Request req;
    req.roots.push_back(fs::path("Skins") / global_data.config->paths.skin);
    for (const fs::path& r : skin_ancestor_roots()) req.roots.push_back(r);
    req.screens = it->second;
    queued = std::move(req);
    if (!worker.t.joinable()) worker.t = std::thread(run);
    cv.notify_one();
#endif
}

void stop() {
    shut_down();
}

}  // namespace readahead
