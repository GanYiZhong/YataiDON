#include "skin_updater.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

#include "sha256.h"

#ifndef __EMSCRIPTEN__

#include <cpr/cpr.h>

#ifdef _WIN32
#include "../platform/platform_windows.h"
#endif

#if defined(__ANDROID__)
// Bundled Mozilla CA bundle for libcurl on Android (which has no system trust
// store). Defined in libs/network.cpp; reused here so skin updates trust the
// same roots as the rest of the client.
cpr::SslOptions android_ca();
#include <curl/curl.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string strip_dot_git(std::string url) {
    if (url.size() >= 4 && url.compare(url.size() - 4, 4, ".git") == 0) url.resize(url.size() - 4);
    return url;
}

void trim(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
}

void init_session(cpr::Session& session) {
#if defined(__ANDROID__)
    session.SetOption(android_ca());
    curl_easy_setopt(session.GetCurlHolder()->handle, CURLOPT_SSL_EC_CURVES, "X25519:P-256:P-384");
#else
    (void)session;
#endif
}

cpr::Response get_url(cpr::Session& session, const std::string& url, int32_t timeout_ms, int32_t connect_timeout_ms) {
    session.SetUrl(cpr::Url{url});
    session.SetTimeout(cpr::Timeout{timeout_ms});
    session.SetConnectTimeout(cpr::ConnectTimeout{connect_timeout_ms});
    return session.Get();
}

// Hashes the file in pieces rather than reading all of it into memory. Empty on a read error.
std::string sha256_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    crypto::Sha256 ctx;
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) ctx.update(reinterpret_cast<const uint8_t*>(buf.data()), static_cast<size_t>(got));
    }
    if (in.bad()) return {};
    return crypto::to_hex(ctx.finalize());
}

// The hash of each skin file as of its last check, so a pass reads only the files whose size or
// modification time changed since. Kept in cache/skin_hashes/<skin>.txt, one
// "<hash> <size> <mtime> <path>" line per file (the path last: it may contain spaces).
struct HashCacheEntry {
    std::string hash;
    uintmax_t size = 0;
    int64_t mtime = 0;
};
using HashCache = std::unordered_map<std::string, HashCacheEntry>;

fs::path hash_cache_path(const fs::path& skin_dir) {
    return fs::path("cache") / "skin_hashes" / (skin_dir.filename().string() + ".txt");
}

HashCache load_hash_cache(const fs::path& skin_dir) {
    HashCache cache;
    std::ifstream in(hash_cache_path(skin_dir));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        HashCacheEntry e;
        if (!(fields >> e.hash >> e.size >> e.mtime)) continue;
        std::string rel;
        std::getline(fields >> std::ws, rel);
        if (!rel.empty()) cache[rel] = std::move(e);
    }
    return cache;
}

void save_hash_cache(const fs::path& skin_dir, const HashCache& cache) {
    const fs::path file = hash_cache_path(skin_dir);
    const fs::path tmp = fs::path(file).concat(".tmp");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return;
        for (const auto& [rel, e] : cache)
            out << e.hash << ' ' << e.size << ' ' << e.mtime << ' ' << rel << '\n';
        if (!out) return;
    }
    fs::rename(tmp, file, ec);
    if (ec) fs::remove(tmp, ec);
}

// Size and modification time of `path`; false if it cannot be read
bool file_stamp(const fs::path& path, uintmax_t& size, int64_t& mtime) {
    std::error_code ec;
    size = fs::file_size(path, ec);
    if (ec) return false;
    const auto t = fs::last_write_time(path, ec);
    if (ec) return false;
    mtime = static_cast<int64_t>(t.time_since_epoch().count());
    return true;
}

// HTTP 200 alone is not enough: a transfer cut short by the timeout still carries the 200 it
// started with, and its body is only the part that arrived.
bool fetched(const cpr::Response& r) {
    return r.status_code == 200 && r.error.code == cpr::ErrorCode::OK;
}

bool is_lfs_pointer(const std::string& body) {
    static const std::string kLfsMarker = "version https://git-lfs.github.com/spec/v1";
    return body.compare(0, kLfsMarker.size(), kLfsMarker) == 0;
}

}  // namespace

void SkinUpdater::set_current_skin(const std::string& name) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_skin_ = name;
    current_file_.clear();
}

void SkinUpdater::set_current_file(const std::string& file) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_file_ = file;
}

void SkinUpdater::note_file_updated() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    ++files_updated_;
}

void SkinUpdater::update_one_skin(const std::filesystem::path& skin_dir, const std::string& repo_url, const std::string& branch) {
    set_current_skin(skin_dir.filename().string());

    cpr::Session session;
    init_session(session);

    auto raw_url = [&](const std::string& rel_path) {
        return repo_url + "/raw/branch/" + branch + "/" + rel_path;
    };

    auto media_url = [&](const std::string& rel_path) {
        return repo_url + "/media/branch/" + branch + "/" + rel_path;
    };

    // a whole manifest or nothing: files it doesn't list get removed below, so a truncated
    // one would delete everything past the cut (and the last line must be complete)
    cpr::Response checksums = get_url(session, raw_url("checksums.sha256"), 60000, 5000);
    if (!fetched(checksums) || checksums.text.empty() || checksums.text.back() != '\n') {
        spdlog::warn("Skin update ({}): could not fetch a complete checksums.sha256 (HTTP {}, curl error {}: {})",
                     skin_dir.filename().string(), checksums.status_code,
                     static_cast<int>(checksums.error.code), checksums.error.message);
        return;
    }

    HashCache cache = load_hash_cache(skin_dir);
    HashCache seen;   // this manifest's files: entries for files it no longer lists are dropped
    std::unordered_set<std::string> listed;
    std::istringstream lines(checksums.text);
    std::string hash, rel_path;
    int updated = 0;
    while (lines >> hash >> rel_path) {
        if (!rel_path.empty() && rel_path.front() == '*') rel_path.erase(0, 1);
        if (fs::path(rel_path).filename() == "checksums.sha256") continue;

        fs::path local_file = (skin_dir / rel_path).lexically_normal();
        const fs::path local_rel = local_file.lexically_relative(skin_dir);
        if (local_rel.empty() || *local_rel.begin() == "..") {
            spdlog::warn("Skin update ({}): skipping unsafe manifest path {}", skin_dir.filename().string(), rel_path);
            continue;
        }
        const std::string& key = rel_path;  // the manifest's own spelling (UTF-8)
        listed.insert(key);
        uintmax_t size = 0;
        int64_t mtime = 0;
        if (file_stamp(local_file, size, mtime)) {
            // unchanged since its last check: the cached hash stands for the file
            auto cached = cache.find(key);
            std::string local_hash;
            if (cached != cache.end() && cached->second.size == size && cached->second.mtime == mtime)
                local_hash = cached->second.hash;
            else
                local_hash = sha256_file(local_file);
            if (!local_hash.empty()) seen[key] = HashCacheEntry{local_hash, size, mtime};
            if (local_hash == hash) continue;
        }

        // File is missing or differs: this pass has at least one real update.
        any_updated_.store(true);
        set_current_file(rel_path);
        cpr::Response file_resp = get_url(session, raw_url(rel_path), 15000, 5000);
        if (!fetched(file_resp)) {
            spdlog::warn("Skin update ({}): failed to download {} (HTTP {})", skin_dir.filename().string(), rel_path, file_resp.status_code);
            continue;
        }

        if (is_lfs_pointer(file_resp.text)) {
            spdlog::debug("Skin update ({}): {} is an LFS pointer; fetching content from media endpoint",
                          skin_dir.filename().string(), rel_path);
            cpr::Response lfs_resp = get_url(session, media_url(rel_path), 60000, 5000);
            if (!fetched(lfs_resp)) {
                spdlog::warn("Skin update ({}): failed to download LFS content {} (HTTP {})",
                             skin_dir.filename().string(), rel_path, lfs_resp.status_code);
                continue;
            }
            file_resp = std::move(lfs_resp);
        }

        // A complete download (fetched() above) is written even when it doesn't match the
        // manifest: that only means the manifest is behind the repo, and keeping the old file
        // (or none, for a new one) would leave the skin half updated.
        const std::string got_hash = crypto::to_hex(crypto::sha256(file_resp.text));
        if (got_hash != hash) {
            spdlog::debug("Skin update ({}): {} differs from its checksums.sha256 entry (stale manifest)",
                          skin_dir.filename().string(), rel_path);
        }

        std::error_code mkdir_ec;
        fs::create_directories(local_file.parent_path(), mkdir_ec);
        std::ofstream out(local_file, std::ios::binary | std::ios::trunc);
        if (!out) {
            spdlog::warn("Skin update ({}): failed to write {}", skin_dir.filename().string(), rel_path);
            continue;
        }
        out << file_resp.text;
        out.close();
        if (out && file_stamp(local_file, size, mtime))
            seen[key] = HashCacheEntry{got_hash, size, mtime};
        else
            seen.erase(key);
        note_file_updated();
        ++updated;
    }

    int removed = 0;
    for (const auto& [rel, e] : cache) {
        if (listed.count(rel)) continue;
        const fs::path local_file = (skin_dir / rel).lexically_normal();
        const fs::path local_rel = local_file.lexically_relative(skin_dir);
        if (local_rel.empty() || *local_rel.begin() == "..") continue;
        uintmax_t size = 0;
        int64_t mtime = 0;
        if (!file_stamp(local_file, size, mtime) || size != e.size || mtime != e.mtime) continue;
        std::error_code ec;
        if (!fs::remove(local_file, ec)) continue;
        ++removed;
        for (fs::path dir = local_file.parent_path(); dir != skin_dir && fs::is_empty(dir, ec) && !ec; dir = dir.parent_path())
            if (!fs::remove(dir, ec)) break;
    }
    if (removed > 0) any_updated_.store(true);

    save_hash_cache(skin_dir, seen);
    spdlog::info("Skin update ({}): {} file(s) updated, {} removed", skin_dir.filename().string(), updated, removed);
}

void SkinUpdater::scan_skins() {
    std::error_code ec;
    if (!fs::exists("Skins", ec)) return;
    for (const auto& entry : fs::directory_iterator("Skins", ec)) {
        if (ec || !entry.is_directory()) continue;

        std::ifstream repo_file(entry.path() / ".skin-repo");
        if (!repo_file) continue;
        std::string repo_url, branch;
        std::getline(repo_file, repo_url);
        std::getline(repo_file, branch);
        trim(repo_url);
        trim(branch);
        if (repo_url.empty()) continue;
        if (branch.empty()) branch = "main";

        update_one_skin(entry.path(), strip_dot_git(repo_url), branch);
    }
}

void SkinUpdater::start() {
    if (started_.exchange(true)) return;
    running_ = true;
    thread_ = std::thread([this] {
#ifdef _WIN32
        // below the game for CPU time: a pass without a hash cache reads every skin file
        win32_lower_thread_priority();
#endif
        try {
            scan_skins();
        } catch (const std::exception& e) {
            spdlog::error("Skin update: unhandled exception: {}", e.what());
        }
        running_ = false;
        std::lock_guard<std::mutex> lock(status_mutex_);
        current_skin_.clear();
        current_file_.clear();
    });
}

bool SkinUpdater::in_progress() {
    return running_.load();
}

bool SkinUpdater::finished() {
    return started_.load() && !running_.load();
}

bool SkinUpdater::applying_updates() {
    return running_.load() && any_updated_.load();
}

bool SkinUpdater::updated_this_pass() {
    return any_updated_.load();
}

SkinUpdater::Status SkinUpdater::snapshot() {
    Status s;
    s.running = running_.load();
    s.finished = finished();
    s.any_updated = any_updated_.load();
    std::lock_guard<std::mutex> lock(status_mutex_);
    s.current_skin = current_skin_;
    s.current_file = current_file_;
    s.files_updated = files_updated_;
    return s;
}

void SkinUpdater::shutdown() {
    if (thread_.joinable()) thread_.join();
}

SkinUpdater skin_updater;

#else  // __EMSCRIPTEN__

void SkinUpdater::start() {}
bool SkinUpdater::in_progress() { return false; }
bool SkinUpdater::finished() { return true; }
bool SkinUpdater::applying_updates() { return false; }
bool SkinUpdater::updated_this_pass() { return false; }
SkinUpdater::Status SkinUpdater::snapshot() {
    Status s;
    s.finished = true;
    return s;
}
void SkinUpdater::shutdown() {}

SkinUpdater skin_updater;

#endif
