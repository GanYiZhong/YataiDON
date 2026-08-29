#include <rlgl.h>

#include "automation.h"

#include "global_data.h"
#include "perf.h"
#include "screen.h"
#include "ray.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #define CloseWindow CloseWindow_WinAPI
    #define ShowCursor ShowCursor_WinAPI
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #undef CloseWindow
    #undef ShowCursor
    using socket_t = SOCKET;
    static constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
    #define CLOSE_SOCKET closesocket
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <unistd.h>
    using socket_t = int;
    static constexpr socket_t INVALID_SOCK = -1;
    #define CLOSE_SOCKET ::close
#endif

std::atomic<bool> automation_enabled{false};
std::array<std::atomic<bool>, AUTOMATION_KEY_COUNT> automation_key_state{};

namespace {

std::atomic<bool> server_running{false};
std::atomic<bool> quit_requested{false};
std::thread       server_thread;
socket_t          listen_sock = INVALID_SOCK;
socket_t          client_sock = INVALID_SOCK;
std::atomic<bool> server_finished{false};

// ---------------------------------------------------------------------------
// State snapshot: filled on the render thread once per frame, read by the
// server thread. Everything the protocol exposes goes through here so the
// server thread never touches engine data structures directly.
// ---------------------------------------------------------------------------
struct Snapshot {
    std::string screen        = "LOADING";
    std::string title_state   = "";      // TITLE attract phase, "" elsewhere
    double      title_state_ms = 0.0;    // ms this phase has been running
    bool        in_transition = false;
    int         input_locked  = 0;
    int         songs_played  = 0;
    int         player_num    = 0;
    bool        focused       = false;
    long long   frame         = 0;

    std::string p1_song;
    std::string p1_title;
    int         p1_difficulty = 0;
    int         score = 0, good = 0, ok = 0, bad = 0, max_combo = 0, drumroll = 0;
    // Live in-song counters (global_data.live_*), valid while screen == GAME*.
    int         live_combo = 0, live_score = 0, live_drumroll = 0;
    bool        live_gogo = false;
    // ROUND 52: live gauge data-out.
    double      live_soul = 0.0;
    bool        live_is_clear = false, live_is_rainbow = false;
    // 演奏スキップ: alternating-rim count 0..10, -1 when not armed.
    int         live_skip = -1;
    bool        live_skip_used = false;
    float       gauge = 0.0f;

    // Key codes resolved from the config on the render thread, so the server
    // thread never reads config vectors that the settings screen may rewrite.
    int don_l_1p = 0, don_r_1p = 0, kat_l_1p = 0, kat_r_1p = 0;
    int don_l_2p = 0, don_r_2p = 0, kat_l_2p = 0, kat_r_2p = 0;
    int back_key = 0, pause_key = 0, restart_key = 0;
};

std::mutex snap_mutex;
Snapshot   snap;

// ---------------------------------------------------------------------------
// Screenshot request, serviced by the render thread.
// ---------------------------------------------------------------------------
std::mutex              shot_mutex;
std::condition_variable shot_cv;
std::string             shot_path;      // non-empty => request pending
bool                    shot_done  = false;
bool                    shot_ok    = false;

// ---------------------------------------------------------------------------
// Pending `goto <SCREEN>` request, consumed by the main loop.
// ---------------------------------------------------------------------------
std::mutex       screen_req_mutex;
bool             screen_req_pending = false;
int              screen_req_index   = 0;

// ---------------------------------------------------------------------------
// ROUND 26 (r26-gaugesliver): pending `gotosong <folder path>` request,
// consumed by SongSelectScreen::update() (mirrors poll_second_player_join's
// use of Navigator::jump_to_song, just fed from automation instead of the
// network song-jump poll). Lets a scripted run reach a *known* song directly
// instead of fighting the live wheel order (crown/recent sort, 700+ entries),
// so real hit-timing playthroughs (not --auto) can be driven deterministically
// for transition-heavy capture work. Testing-only; no normal player path uses it.
// ---------------------------------------------------------------------------
std::mutex       song_jump_mutex;
bool             song_jump_pending = false;
std::string      song_jump_path;

// Resolve a screen NAME to its Screens index using screens_to_string() as the
// single source of truth, so this never drifts from the enum.
int screen_index_from_name(const std::string& name) {
    for (int i = 0; i < 22; ++i)
        if (screens_to_string(static_cast<Screens>(i)) == name) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Key name -> raylib key code. Deliberately a private copy of the mapping in
// config.cpp (which is file-static there) so this module stays self-contained.
// ---------------------------------------------------------------------------
int key_code_from_name(const std::string& name) {
    if (name.empty()) return -1;

    // Raw numeric code, e.g. "key 32 down".
    if (std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); })) {
        int v = std::atoi(name.c_str());
        return (v >= 0 && v < AUTOMATION_KEY_COUNT) ? v : -1;
    }

    if (name.size() == 1 && std::isalnum((unsigned char)name[0]))
        return std::toupper((unsigned char)name[0]);

    std::string up = name;
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return std::toupper(c); });

    static const std::map<std::string, int> map = {
        {"SPACE", ray::KEY_SPACE}, {"ESCAPE", ray::KEY_ESCAPE}, {"ESC", ray::KEY_ESCAPE},
        {"ENTER", ray::KEY_ENTER}, {"TAB", ray::KEY_TAB}, {"BACKSPACE", ray::KEY_BACKSPACE},
        {"INSERT", ray::KEY_INSERT}, {"DELETE", ray::KEY_DELETE},
        {"RIGHT", ray::KEY_RIGHT}, {"LEFT", ray::KEY_LEFT},
        {"DOWN", ray::KEY_DOWN}, {"UP", ray::KEY_UP},
        {"PAGE_UP", ray::KEY_PAGE_UP}, {"PAGE_DOWN", ray::KEY_PAGE_DOWN},
        {"HOME", ray::KEY_HOME}, {"END", ray::KEY_END},
        {"F1", ray::KEY_F1}, {"F2", ray::KEY_F2}, {"F3", ray::KEY_F3}, {"F4", ray::KEY_F4},
        {"F5", ray::KEY_F5}, {"F6", ray::KEY_F6}, {"F7", ray::KEY_F7}, {"F8", ray::KEY_F8},
        {"F9", ray::KEY_F9}, {"F10", ray::KEY_F10}, {"F11", ray::KEY_F11}, {"F12", ray::KEY_F12},
        {"LEFT_SHIFT", ray::KEY_LEFT_SHIFT}, {"LEFT_CONTROL", ray::KEY_LEFT_CONTROL},
        {"LEFT_ALT", ray::KEY_LEFT_ALT}, {"RIGHT_SHIFT", ray::KEY_RIGHT_SHIFT},
        {"RIGHT_CONTROL", ray::KEY_RIGHT_CONTROL}, {"RIGHT_ALT", ray::KEY_RIGHT_ALT},
        {"KP_0", ray::KEY_KP_0}, {"KP_1", ray::KEY_KP_1}, {"KP_2", ray::KEY_KP_2},
        {"KP_3", ray::KEY_KP_3}, {"KP_4", ray::KEY_KP_4}, {"KP_5", ray::KEY_KP_5},
        {"KP_6", ray::KEY_KP_6}, {"KP_7", ray::KEY_KP_7}, {"KP_8", ray::KEY_KP_8},
        {"KP_9", ray::KEY_KP_9}, {"KP_ENTER", ray::KEY_KP_ENTER},
        {"APOSTROPHE", ray::KEY_APOSTROPHE}, {"COMMA", ray::KEY_COMMA},
        {"MINUS", ray::KEY_MINUS}, {"PERIOD", ray::KEY_PERIOD}, {"SLASH", ray::KEY_SLASH},
        {"SEMICOLON", ray::KEY_SEMICOLON}, {"EQUAL", ray::KEY_EQUAL},
        {"LEFT_BRACKET", ray::KEY_LEFT_BRACKET}, {"BACKSLASH", ray::KEY_BACKSLASH},
        {"RIGHT_BRACKET", ray::KEY_RIGHT_BRACKET}, {"GRAVE", ray::KEY_GRAVE},
    };
    auto it = map.find(up);
    return it == map.end() ? -1 : it->second;
}

// Named drum/menu actions -> key code, taken from the per-frame snapshot so the
// aliases always follow whatever the player has bound in config.toml.
int alias_key_code(const std::string& raw, std::string& err) {
    std::string a = raw;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });

    Snapshot s;
    { std::lock_guard<std::mutex> lk(snap_mutex); s = snap; }

    int code = 0;
    if      (a == "don_l" || a == "left_don"  || a == "don_l_1p") code = s.don_l_1p;
    else if (a == "don_r" || a == "right_don" || a == "don_r_1p") code = s.don_r_1p;
    else if (a == "kat_l" || a == "left_kat"  || a == "kat_l_1p") code = s.kat_l_1p;
    else if (a == "kat_r" || a == "right_kat" || a == "kat_r_1p") code = s.kat_r_1p;
    else if (a == "don_l_2p") code = s.don_l_2p;
    else if (a == "don_r_2p") code = s.don_r_2p;
    else if (a == "kat_l_2p") code = s.kat_l_2p;
    else if (a == "kat_r_2p") code = s.kat_r_2p;
    else if (a == "start" || a == "confirm" || a == "enter") code = ray::KEY_ENTER;
    else if (a == "back"  || a == "cancel")                  code = s.back_key ? s.back_key : ray::KEY_ESCAPE;
    else if (a == "pause")                                   code = s.pause_key ? s.pause_key : ray::KEY_SPACE;
    else if (a == "restart")                                 code = s.restart_key ? s.restart_key : ray::KEY_F1;
    else {
        code = key_code_from_name(raw);
        if (code < 0) { err = "unknown alias or key '" + raw + "'"; return -1; }
    }

    if (code <= 0 || code >= AUTOMATION_KEY_COUNT) {
        err = "alias '" + raw + "' is not bound in config.toml";
        return -1;
    }
    return code;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Window placement. The automation window is parked outside the virtual
// desktop instead of being hidden: a hidden (never-shown) window has no
// composited surface, so its default framebuffer is not guaranteed to hold a
// complete image and `shot` came back with the background only. An off-screen
// but shown window has a normal swap chain and renders exactly like a visible
// one, while being unreachable for the user's eyes, taskbar and Alt-Tab.
// ---------------------------------------------------------------------------
#ifdef _WIN32
std::atomic<void*> game_hwnd{nullptr};
#endif

std::string window_json() {
    std::ostringstream o;
#ifdef _WIN32
    HWND h = (HWND)game_hwnd.load(std::memory_order_relaxed);
    RECT r{0, 0, 0, 0};
    if (h) GetWindowRect(h, &r);
    o << "{\"hwnd\":" << (unsigned long long)(uintptr_t)h
      << ",\"rect\":[" << r.left << "," << r.top << "," << r.right << "," << r.bottom << "]"
      << ",\"virtual_screen\":[" << GetSystemMetrics(SM_XVIRTUALSCREEN) << ","
      << GetSystemMetrics(SM_YVIRTUALSCREEN) << ","
      << GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) << ","
      << GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) << "]"
      << ",\"foreground\":" << ((h && GetForegroundWindow() == h) ? "true" : "false")
      << ",\"visible\":" << ((h && IsWindowVisible(h)) ? "true" : "false")
      << "}";
#else
    o << "{\"hwnd\":0,\"rect\":[0,0,0,0],\"virtual_screen\":[0,0,0,0],"
         "\"foreground\":false,\"visible\":true}";
#endif
    return o.str();
}

std::string snapshot_json() {
    Snapshot s;
    { std::lock_guard<std::mutex> lk(snap_mutex); s = snap; }

    std::ostringstream o;
    o << "{"
      << "\"screen\":\""        << json_escape(s.screen) << "\","
      << "\"in_transition\":"   << (s.in_transition ? "true" : "false") << ","
      << "\"input_locked\":"    << s.input_locked << ","
      << "\"songs_played\":"    << s.songs_played << ","
      << "\"player_num\":"      << s.player_num << ","
      << "\"focused\":"         << (s.focused ? "true" : "false") << ","
      << "\"frame\":"           << s.frame << ","
      << "\"title_state\":\"" << json_escape(s.title_state) << "\","
      << "\"title_state_ms\":" << (long long)s.title_state_ms << ","
      << "\"song_path\":\""     << json_escape(s.p1_song) << "\","
      << "\"song_title\":\""    << json_escape(s.p1_title) << "\","
      << "\"difficulty\":"      << s.p1_difficulty << ","
      << "\"result\":{"
      << "\"score\":"           << s.score << ","
      << "\"good\":"            << s.good << ","
      << "\"ok\":"              << s.ok << ","
      << "\"bad\":"             << s.bad << ","
      << "\"max_combo\":"       << s.max_combo << ","
      << "\"drumroll\":"        << s.drumroll << ","
      << "\"gauge\":"           << s.gauge
      << "},"
      << "\"live\":{"
      << "\"combo\":"           << s.live_combo << ","
      << "\"score\":"           << s.live_score << ","
      << "\"drumroll\":"        << s.live_drumroll << ","
      << "\"gogo\":"            << (s.live_gogo ? "true" : "false")
      << ",\"soul\":"           << s.live_soul
      << ",\"is_clear\":"       << (s.live_is_clear ? "true" : "false")
      << ",\"is_rainbow\":"     << (s.live_is_rainbow ? "true" : "false")
      << ",\"skip\":"           << s.live_skip << ","
      << "\"skip_used\":"       << (s.live_skip_used ? "true" : "false")
      << "},"
      << "\"keys\":{"
      << "\"don_l\":" << s.don_l_1p << ",\"don_r\":" << s.don_r_1p
      << ",\"kat_l\":" << s.kat_l_1p << ",\"kat_r\":" << s.kat_r_1p
      << "}}";
    return o.str();
}

void sleep_ms(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool send_line(socket_t s, const std::string& line) {
    std::string out = line + "\n";
    size_t sent = 0;
    while (sent < out.size()) {
        int n = ::send(s, out.data() + sent, (int)(out.size() - sent), 0);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

std::string do_shot(const std::string& path) {
    {
        std::lock_guard<std::mutex> lk(shot_mutex);
        shot_path = path;
        shot_done = false;
        shot_ok   = false;
    }
    std::unique_lock<std::mutex> lk(shot_mutex);
    if (!shot_cv.wait_for(lk, std::chrono::seconds(10), [] { return shot_done; }))
        return "ERR screenshot timed out (is the render loop running?)";
    return shot_ok ? ("OK " + path) : ("ERR screenshot failed for " + path);
}

std::string handle_command(const std::string& line) {
    std::istringstream is(line);
    std::string cmd;
    if (!(is >> cmd)) return "OK";
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::tolower(c); });

    if (cmd == "ping") return "OK pong";

    if (cmd == "key") {
        std::string name, dir;
        if (!(is >> name >> dir)) return "ERR usage: key <name> down|up";
        std::string err;
        int code = alias_key_code(name, err);
        if (code < 0) return "ERR " + err;
        std::transform(dir.begin(), dir.end(), dir.begin(), [](unsigned char c) { return std::tolower(c); });
        if (dir == "down")      automation_key_state[code].store(true,  std::memory_order_relaxed);
        else if (dir == "up")   automation_key_state[code].store(false, std::memory_order_relaxed);
        else return "ERR usage: key <name> down|up";
        return "OK " + std::to_string(code);
    }

    if (cmd == "hit") {
        std::string name;
        if (!(is >> name)) return "ERR usage: hit <alias> [hold_ms]";
        int hold = 40;
        is >> hold;
        std::string err;
        int code = alias_key_code(name, err);
        if (code < 0) return "ERR " + err;
        automation_key_state[code].store(true, std::memory_order_relaxed);
        sleep_ms(hold);
        automation_key_state[code].store(false, std::memory_order_relaxed);
        // Let the 500 us polling thread observe the release edge before the
        // caller can ask for the next press on the same key.
        sleep_ms(16);
        return "OK";
    }

    if (cmd == "wait") {
        int ms = 0;
        if (!(is >> ms)) return "ERR usage: wait <ms>";
        sleep_ms(ms);
        return "OK";
    }

    if (cmd == "waitscreen") {
        std::string want;
        if (!(is >> want)) return "ERR usage: waitscreen <SCREEN> [timeout_ms]";
        int timeout = 20000;
        is >> timeout;
        std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return std::toupper(c); });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
        for (;;) {
            std::string cur;
            { std::lock_guard<std::mutex> lk(snap_mutex); cur = snap.screen; }
            if (cur == want) return "OK " + cur;
            if (std::chrono::steady_clock::now() >= deadline) return "ERR timeout, screen is " + cur;
            sleep_ms(10);
        }
    }

    if (cmd == "shot") {
        std::string path;
        std::getline(is, path);
        // trim
        size_t a = path.find_first_not_of(" \t");
        size_t b = path.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return "ERR usage: shot <path.png>";
        path = path.substr(a, b - a + 1);
        return do_shot(path);
    }

    if (cmd == "goto") {
        std::string want;
        if (!(is >> want)) return "ERR usage: goto <SCREEN>";
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        int idx = screen_index_from_name(want);
        if (idx < 0) return "ERR unknown screen '" + want + "'";
        {
            std::lock_guard<std::mutex> lk(screen_req_mutex);
            screen_req_pending = true;
            screen_req_index   = idx;
        }
        // Block until the main loop has actually switched, so a caller can
        // `goto` and `shot` back to back without racing the transition.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            std::string cur;
            { std::lock_guard<std::mutex> lk(snap_mutex); cur = snap.screen; }
            if (cur == want) return "OK " + cur;
            if (std::chrono::steady_clock::now() >= deadline)
                return "ERR goto timed out, screen is " + cur;
            sleep_ms(10);
        }
    }

    if (cmd == "gotosong") {
        std::string path;
        std::getline(is, path);
        size_t a = path.find_first_not_of(" \t");
        size_t b = path.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) return "ERR usage: gotosong <folder path>";
        path = path.substr(a, b - a + 1);
        {
            std::lock_guard<std::mutex> lk(song_jump_mutex);
            song_jump_pending = true;
            song_jump_path    = path;
        }
        return "OK queued";
    }

    if (cmd == "state") return "OK " + snapshot_json();

    // --- round-14 perf instrumentation -----------------------------------
    if (cmd == "perf") return "OK " + perf::stats_json();

    if (cmd == "paramsmode") {
        std::string m;
        if (!(is >> m)) return "OK " + perf::params_json();
        if (m == "fast")   { perf::params_mode(perf::PARAMS_FAST);   return "OK fast"; }
        if (m == "legacy") { perf::params_mode(perf::PARAMS_LEGACY); return "OK legacy"; }
        if (m == "check")  { perf::params_mode(perf::PARAMS_CHECK);  return "OK check"; }
        return "ERR usage: paramsmode [fast|legacy|check]";
    }

    if (cmd == "perfdump") {
        std::string path;
        if (!(is >> path)) return "ERR usage: perfdump <path.csv>";
        return perf::dump_csv(path) ? ("OK " + path) : ("ERR cannot write " + path);
    }

    if (cmd == "perfreset") { perf::reset(); perf::lua_reset(); return "OK reset"; }

    if (cmd == "luaperf") {
        std::string sub;
        is >> sub;
        if (sub == "on")  { perf::lua_reset(); perf::lua_timing(true);  return "OK on"; }
        if (sub == "off") { perf::lua_timing(false); return "OK off"; }
        return "OK " + perf::lua_json();
    }

    if (cmd == "prof") {
        std::string sub;
        if (!(is >> sub)) return "ERR usage: prof start [interval_us] | prof stop <path>";
        if (sub == "start") {
            int us = 1000;
            is >> us;
            return perf::prof_start(us) ? "OK profiling" : "ERR could not start profiler";
        }
        if (sub == "stop") {
            std::string path;
            if (!(is >> path)) return "ERR usage: prof stop <path>";
            return perf::prof_stop(path);
        }
        return "ERR usage: prof start [interval_us] | prof stop <path>";
    }

    if (cmd == "window") return "OK " + window_json();

    if (cmd == "quit") {
        quit_requested.store(true, std::memory_order_relaxed);
        return "OK quitting";
    }

    return "ERR unknown command '" + cmd + "'";
}

void serve_client(socket_t client) {
    std::string buf;
    char        chunk[1024];
    for (;;) {
        int n = ::recv(client, chunk, sizeof chunk, 0);
        if (n <= 0) break;
        buf.append(chunk, (size_t)n);
        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) { if (!send_line(client, "OK")) return; continue; }
            std::string reply = handle_command(line);
            if (!send_line(client, reply)) return;
            if (quit_requested.load(std::memory_order_relaxed)) return;
        }
    }
}

void server_loop(int port) {
    struct Done { ~Done() { server_finished.store(true, std::memory_order_release); } } done;
    for (;;) {
        if (!server_running.load(std::memory_order_relaxed)) break;
        sockaddr_in addr{};
        socklen_t   len = sizeof addr;
        socket_t client = ::accept(listen_sock, (sockaddr*)&addr, &len);
        if (client == INVALID_SOCK) {
            if (!server_running.load(std::memory_order_relaxed)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        int one = 1;
        ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        spdlog::info("[automation] client connected on port {}", port);
        client_sock = client;
        serve_client(client);
        client_sock = INVALID_SOCK;
        CLOSE_SOCKET(client);
        spdlog::info("[automation] client disconnected");
        if (quit_requested.load(std::memory_order_relaxed)) break;
    }
}

} // namespace

static void refresh_snapshot();

bool automation_start(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        spdlog::error("[automation] WSAStartup failed");
        return false;
    }
#endif
    listen_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCK) {
        spdlog::error("[automation] socket() failed");
        return false;
    }
    int one = 1;
    ::setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // loopback only, never exposed

    if (::bind(listen_sock, (sockaddr*)&addr, sizeof addr) != 0 ||
        ::listen(listen_sock, 4) != 0) {
        spdlog::error("[automation] could not listen on 127.0.0.1:{}", port);
        CLOSE_SOCKET(listen_sock);
        listen_sock = INVALID_SOCK;
        return false;
    }

    for (auto& k : automation_key_state) k.store(false, std::memory_order_relaxed);
    server_running.store(true, std::memory_order_relaxed);
    automation_enabled.store(true, std::memory_order_relaxed);
    refresh_snapshot();
    server_thread = std::thread(server_loop, port);
    spdlog::info("[automation] listening on 127.0.0.1:{}", port);
    return true;
}

void automation_shutdown() {
    if (!server_running.exchange(false)) return;
    automation_enabled.store(false, std::memory_order_relaxed);
    if (client_sock != INVALID_SOCK) {
        ::shutdown(client_sock, 2);
    }
    if (listen_sock != INVALID_SOCK) {
        socket_t ls = listen_sock;
        listen_sock = INVALID_SOCK;
        ::shutdown(ls, 2);
        CLOSE_SOCKET(ls);
    }
    // Release anyone blocked on a screenshot round trip.
    {
        std::lock_guard<std::mutex> lk(shot_mutex);
        shot_path.clear();
        shot_done = true;
        shot_ok   = false;
    }
    shot_cv.notify_all();
    // Give the server thread a moment to unwind after its sockets were closed,
    // then join it. Only if it is genuinely wedged do we abandon it - and in
    // that case WSACleanup is skipped, because tearing the stack down under a
    // live socket thread is what turns a clean exit into a crash.
    bool finished = false;
    for (int i = 0; i < 200 && !finished; i++) {
        finished = server_finished.load(std::memory_order_acquire);
        if (!finished) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (finished) {
        if (server_thread.joinable()) server_thread.join();
        // Deliberately no WSACleanup(): other libraries in this process own
        // winsock too, and tearing it down here at exit time is not worth the
        // risk. Windows releases the sockets when the process goes away.
    } else {
        spdlog::warn("[automation] server thread did not finish, detaching");
        if (server_thread.joinable()) server_thread.detach();
    }
}

bool automation_take_screen_request(int& out) {
    if (!automation_enabled.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lk(screen_req_mutex);
    if (!screen_req_pending) return false;
    screen_req_pending = false;
    out = screen_req_index;
    return true;
}

bool automation_take_song_jump(std::string& out) {
    if (!automation_enabled.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lk(song_jump_mutex);
    if (!song_jump_pending) return false;
    song_jump_pending = false;
    out = song_jump_path;
    return true;
}

bool automation_quit_requested() {
    return quit_requested.load(std::memory_order_relaxed);
}

// Refresh the snapshot. Engine-thread only (called from the frame hook and
// once at startup, before the first frame is drawn, so `hit` aliases resolve
// even while the loading screen is still initialising).
static void refresh_snapshot() {
    {
        Snapshot s;
        s.screen        = global_data.current_screen;
        s.title_state   = global_data.title_state;
        s.title_state_ms = global_data.title_state.empty()
                             ? 0.0
                             : (get_current_ms() - global_data.title_state_start_ms);
        s.in_transition = global_data.in_transition;
        s.input_locked  = global_data.input_locked;
        s.songs_played  = global_data.songs_played;
        s.player_num    = (int)global_data.player_num;
        s.focused       = ray::IsWindowFocused();

        const auto& sd = global_data.session_data[(int)PlayerNum::P1];
        s.p1_song       = sd.selected_song.string();
        s.p1_title      = sd.song_title;
        s.p1_difficulty = sd.selected_difficulty;
        s.score     = sd.result_data.score;
        s.good      = sd.result_data.good;
        s.ok        = sd.result_data.ok;
        s.bad       = sd.result_data.bad;
        s.max_combo = sd.result_data.max_combo;
        s.drumroll  = sd.result_data.total_drumroll;
        s.gauge     = sd.result_data.gauge_length;
        s.live_combo    = global_data.live_combo;
        s.live_score    = global_data.live_score;
        s.live_drumroll = global_data.live_drumroll;
        s.live_gogo     = global_data.live_gogo;
        s.live_skip     = global_data.live_skip_count;
        s.live_skip_used = global_data.live_skip_used;
        // ROUND 52 (r52-lua-divergence-fixes): live gauge data-out.
        s.live_soul       = global_data.live_soul;
        s.live_is_clear   = global_data.live_is_clear;
        s.live_is_rainbow = global_data.live_is_rainbow;

        auto first = [](const std::vector<int>& v) { return v.empty() ? 0 : v.front(); };
        if (global_data.config) {
            const Config& c = *global_data.config;
            s.don_l_1p = first(c.keys_1p.left_don);
            s.don_r_1p = first(c.keys_1p.right_don);
            s.kat_l_1p = first(c.keys_1p.left_kat);
            s.kat_r_1p = first(c.keys_1p.right_kat);
            s.don_l_2p = first(c.keys_2p.left_don);
            s.don_r_2p = first(c.keys_2p.right_don);
            s.kat_l_2p = first(c.keys_2p.left_kat);
            s.kat_r_2p = first(c.keys_2p.right_kat);
            s.back_key    = c.keys.back_key;
            s.pause_key   = c.keys.pause_key;
            s.restart_key = c.keys.restart_key;
        }

        std::lock_guard<std::mutex> lk(snap_mutex);
        s.frame = snap.frame + 1;
        snap = std::move(s);
    }

}

void automation_prepare_window(bool offscreen) {
#ifdef _WIN32
    HWND h = (HWND)ray::GetWindowHandle();
    game_hwnd.store((void*)h, std::memory_order_relaxed);
    if (!h) {
        spdlog::error("[automation] no window handle; cannot place the window");
        return;
    }
    if (!offscreen) {
        // Debug path: show it where it was created, but still never activate.
        ShowWindow(h, SW_SHOWNOACTIVATE);
        return;
    }

    // No taskbar button, no Alt-Tab entry, and refuse activation even if the
    // user somehow clicks it.
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    ex |= (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    ex &= ~(LONG_PTR)WS_EX_APPWINDOW;
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);

    RECT r{0, 0, 0, 0};
    GetWindowRect(h, &r);
    int w = r.right - r.left;
    int hh = r.bottom - r.top;
    if (w <= 0) w = 1280;
    if (hh <= 0) hh = 720;

    // Park it fully outside the union of all monitors. Windows keeps composing
    // and presenting it, so the framebuffer stays complete.
    long x = (long)GetSystemMetrics(SM_XVIRTUALSCREEN) - w - 64;
    long y = (long)GetSystemMetrics(SM_YVIRTUALSCREEN) - hh - 64;
    if (x < -30000) x = -30000;
    if (y < -30000) y = -30000;

    SetWindowPos(h, HWND_BOTTOM, (int)x, (int)y, w, hh,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    // The window was created hidden so it never flashed at its default
    // position; show it now, off-screen, without activating.
    ShowWindow(h, SW_SHOWNOACTIVATE);
    SetWindowPos(h, HWND_BOTTOM, (int)x, (int)y, w, hh,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    spdlog::info("[automation] window parked off-screen at {},{} ({}x{})", x, y, w, hh);
#else
    (void)offscreen;
    ray::ClearWindowState(ray::FLAG_WINDOW_HIDDEN);
#endif
}

void automation_frame_hook() {
    if (!automation_enabled.load(std::memory_order_relaxed)) return;
    refresh_snapshot();

    // --- service a pending screenshot ------------------------------------
    std::string path;
    {
        std::lock_guard<std::mutex> lk(shot_mutex);
        if (shot_path.empty() || shot_done) return;
        path = shot_path;
    }

    bool ok = false;
    try {
        std::filesystem::path out(path);
        if (out.has_parent_path())
            std::filesystem::create_directories(out.parent_path());
        // The capture costs a few hundred ms of render-thread time (a GPU
        // read-back plus a PNG encode). That is fine in itself, but it used to
        // wedge a gameplay capture series: a frame longer than the 108 ms BAD
        // window made Player::autoplay_manager() spin forever on a note whose
        // judgement window had closed (fixed in objects/game/player.cpp - see
        // AUTOMATION.md). Kept on the render thread on purpose: it has to run
        // between the last draw call and EndDrawing to capture this frame.
        const auto shot_t0 = std::chrono::steady_clock::now();
        // raylib batches draw calls; without flushing, the framebuffer is
        // still empty at this point in the frame and we would capture black.
        rlDrawRenderBatchActive();
        ray::Image img = ray::LoadImageFromScreen();
        if (img.data) {
            ok = ray::ExportImage(img, path.c_str());
            ray::UnloadImage(img);
            spdlog::debug("[automation] shot {} in {:.0f} ms", path,
                          std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - shot_t0).count());
        } else {
            spdlog::error("[automation] LoadImageFromScreen returned no data");
        }
        if (!ok) spdlog::error("[automation] could not write {}", path);
    } catch (const std::exception& e) {
        spdlog::error("[automation] screenshot failed: {}", e.what());
        ok = false;
    } catch (...) {
        ok = false;
    }

    {
        std::lock_guard<std::mutex> lk(shot_mutex);
        shot_ok   = ok;
        shot_done = true;
        shot_path.clear();
    }
    shot_cv.notify_all();
}
