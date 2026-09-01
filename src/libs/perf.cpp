#include "perf.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>   // ROUND 103: std::getenv for the YATAIDON_R103_TRACE gate
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define CloseWindow CloseWindow_WinAPI
    #define ShowCursor ShowCursor_WinAPI
    #include <windows.h>
    #include <dbghelp.h>
    #include <psapi.h>
    #undef CloseWindow
    #undef ShowCursor
#endif

namespace perf {

// ---------------------------------------------------------------------------
// frame timing
// ---------------------------------------------------------------------------

namespace {

struct Sample {
    float update, draw, flush, work;
    float kcycles;   // render-thread cycles for the frame, in thousands
};

// Parallel to g_all, so a dump can say which screen each sample came from
// without duplicating the samples themselves.
std::vector<const std::string*> g_all_screen;

std::mutex                                           g_mtx;
std::unordered_map<std::string, std::vector<Sample>> g_by_screen;
std::vector<Sample>                                  g_all;

double pct(std::vector<float>& v, double p) {
    if (v.empty()) return 0.0;
    size_t i = (size_t)std::llround(p * (double)(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + i, v.end());
    return v[i];
}

std::string phase_json(std::vector<Sample>& s, float Sample::*field) {
    std::vector<float> v;
    v.reserve(s.size());
    double sum = 0.0;
    for (auto& x : s) { v.push_back(x.*field); sum += x.*field; }
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(3);
    o << "{\"mean\":" << (v.empty() ? 0.0 : sum / v.size())
      << ",\"p50\":"  << pct(v, 0.50)
      << ",\"p95\":"  << pct(v, 0.95)
      << ",\"p99\":"  << pct(v, 0.99)
      << ",\"max\":"  << pct(v, 1.00) << "}";
    return o.str();
}

std::string block_json(std::vector<Sample>& s) {
    std::ostringstream o;
    o << "{\"frames\":" << s.size()
      << ",\"work\":"   << phase_json(s, &Sample::work)
      << ",\"update\":" << phase_json(s, &Sample::update)
      << ",\"draw\":"   << phase_json(s, &Sample::draw)
      << ",\"flush\":"  << phase_json(s, &Sample::flush)
      << ",\"kcycles\":" << phase_json(s, &Sample::kcycles) << "}";
    return o.str();
}

}  // namespace

unsigned long long thread_cycles() {
#ifdef _WIN32
    ULONG64 c = 0;
    if (QueryThreadCycleTime(GetCurrentThread(), &c)) return c;
#endif
    return 0;
}

void record_frame(const std::string& screen,
                  double update_ms, double draw_ms, double flush_ms,
                  double work_ms, unsigned long long cycles) {
    Sample s{(float)update_ms, (float)draw_ms, (float)flush_ms, (float)work_ms,
             (float)(cycles / 1000.0)};
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_all.size() > 2000000) return;  // never grows unbounded
    auto& bucket = g_by_screen[screen];
    g_all.push_back(s);
    g_all_screen.push_back(&g_by_screen.find(screen)->first);
    bucket.push_back(s);
}

void reset() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_all.clear();
    g_all_screen.clear();
    g_by_screen.clear();
}

// ---------------------------------------------------------------------------
// ROUND 103: one-off-event recorder. See perf.h for why this exists.
// ---------------------------------------------------------------------------
namespace {
struct Event {
    size_t      frame;     // index into the per-frame recorder's series
    const char* kind;      // static string literal, never freed
    std::string detail;
    double      ms;
};
std::mutex         g_ev_mtx;
std::vector<Event> g_events;
}  // namespace

bool events_enabled() {
    // One-time getenv, then a plain bool. Deliberately NOT re-read: an
    // instrument that can be switched on mid-run would make a trace's meaning
    // depend on when it was switched.
    static const bool on = std::getenv("YATAIDON_R103_TRACE") != nullptr;
    return on;
}

void note_event(const char* kind, const std::string& detail, double ms) {
    if (!events_enabled()) return;
    size_t frame;
    { std::lock_guard<std::mutex> lk(g_mtx); frame = g_all.size(); }
    std::lock_guard<std::mutex> lk(g_ev_mtx);
    if (g_events.size() > 200000) return;   // never grows unbounded
    g_events.push_back({frame, kind, detail, ms});
}

void events_reset() {
    std::lock_guard<std::mutex> lk(g_ev_mtx);
    g_events.clear();
}

bool dump_events(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_ev_mtx);
    std::ofstream out(path);
    if (!out) return false;
    out << "frame,kind,ms,detail\n";
    out.setf(std::ios::fixed);
    out.precision(3);
    for (const auto& e : g_events)
        out << e.frame << "," << e.kind << "," << e.ms << ","
            << "\"" << e.detail << "\"\n";
    return true;
}

std::string stats_json() {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::ostringstream o;
    o << "{\"all\":" << block_json(g_all) << ",\"screens\":{";
    bool first = true;
    std::map<std::string, std::vector<Sample>*> ordered;  // stable diff order
    for (auto& kv : g_by_screen) ordered[kv.first] = &kv.second;
    for (auto& kv : ordered) {
        if (!first) o << ",";
        first = false;
        o << "\"" << kv.first << "\":" << block_json(*kv.second);
    }
    o << "}";
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        o << ",\"rss_mb\":"      << (pmc.WorkingSetSize / (1024 * 1024))
          << ",\"peak_rss_mb\":" << (pmc.PeakWorkingSetSize / (1024 * 1024))
          << ",\"private_mb\":"  << (pmc.PrivateUsage / (1024 * 1024));
    }
#endif
    o << "}";
    return o.str();
}

bool dump_csv(const std::string& path) {
    std::lock_guard<std::mutex> lk(g_mtx);
    std::ofstream out(path);
    if (!out) return false;
    out << "i,screen,update_ms,draw_ms,flush_ms,work_ms,kcycles\n";
    out.setf(std::ios::fixed);
    out.precision(4);
    for (size_t i = 0; i < g_all.size(); ++i) {
        out << i << ","
            << (i < g_all_screen.size() ? *g_all_screen[i] : std::string("?")) << ","
            << g_all[i].update << "," << g_all[i].draw << ","
            << g_all[i].flush << "," << g_all[i].work << ","
            << g_all[i].kcycles << "\n";
    }
    return true;
}

// ---------------------------------------------------------------------------
// per-Lua-hook timing
// ---------------------------------------------------------------------------

namespace {
std::atomic<bool>                                        g_lua_on{false};
std::mutex                                               g_lua_mtx;
std::unordered_map<std::string, std::pair<double, uint64_t>> g_lua;  // ms, calls
}

bool lua_timing_enabled() { return g_lua_on.load(std::memory_order_relaxed); }
void lua_timing(bool on)  { g_lua_on.store(on, std::memory_order_relaxed); }

void lua_add(const char* ctx, double ms) {
    std::lock_guard<std::mutex> lk(g_lua_mtx);
    auto& e = g_lua[ctx];
    e.first  += ms;
    e.second += 1;
}

void lua_reset() {
    std::lock_guard<std::mutex> lk(g_lua_mtx);
    g_lua.clear();
}

std::string lua_json() {
    std::lock_guard<std::mutex> lk(g_lua_mtx);
    std::map<std::string, std::pair<double, uint64_t>> ordered(g_lua.begin(), g_lua.end());
    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(4);
    o << "{";
    bool first = true;
    for (auto& kv : ordered) {
        if (!first) o << ",";
        first = false;
        o << "\"" << kv.first << "\":{\"ms\":" << kv.second.first
          << ",\"calls\":" << kv.second.second << "}";
    }
    o << "}";
    return o.str();
}

// ---------------------------------------------------------------------------
// draw-param parser mode
// ---------------------------------------------------------------------------

namespace {
std::atomic<int>      g_params_mode{0};
std::atomic<uint64_t> g_params_checked{0};
std::atomic<uint64_t> g_params_mismatch{0};
}

int  params_mode()          { return g_params_mode.load(std::memory_order_relaxed); }
void params_mode(int mode)  {
    g_params_mode.store(mode, std::memory_order_relaxed);
    g_params_checked.store(0, std::memory_order_relaxed);
    g_params_mismatch.store(0, std::memory_order_relaxed);
}
void params_mismatch() { g_params_mismatch.fetch_add(1, std::memory_order_relaxed); }
void params_checked()  { g_params_checked.fetch_add(1, std::memory_order_relaxed); }

std::string params_json() {
    std::ostringstream o;
    o << "{\"mode\":" << g_params_mode.load()
      << ",\"checked\":" << g_params_checked.load()
      << ",\"mismatch\":" << g_params_mismatch.load() << "}";
    return o.str();
}

// ---------------------------------------------------------------------------
// sampling profiler
// ---------------------------------------------------------------------------

#ifdef _WIN32

namespace {

constexpr int kMaxDepth = 48;

std::atomic<bool> g_prof_on{false};
std::thread       g_prof_thread;
HANDLE            g_render_thread = nullptr;  // registered by the render thread
HANDLE            g_target        = nullptr;
std::mutex        g_stack_mtx;
std::map<std::vector<DWORD64>, uint32_t> g_stacks;
uint64_t          g_samples_taken  = 0;
uint64_t          g_samples_failed = 0;

void sampler_loop(int interval_us) {
    HANDLE process = GetCurrentProcess();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    LARGE_INTEGER freq, next;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&next);
    const long long step = (freq.QuadPart * interval_us) / 1000000;

    std::vector<DWORD64> frames;
    frames.reserve(kMaxDepth);

    while (g_prof_on.load(std::memory_order_relaxed)) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (now.QuadPart < next.QuadPart) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }
        next.QuadPart += step;
        if (next.QuadPart < now.QuadPart) next = now;

        frames.clear();
        if (SuspendThread(g_target) == (DWORD)-1) { g_samples_failed++; continue; }

        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(g_target, &ctx)) {
            STACKFRAME64 sf{};
            sf.AddrPC.Mode      = AddrModeFlat;
            sf.AddrPC.Offset    = ctx.Rip;
            sf.AddrStack.Mode   = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Rsp;
            sf.AddrFrame.Mode   = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Rbp;
            // Symbols are NOT resolved here: that would take the dbghelp/loader
            // locks while the render thread is suspended. Only addresses are
            // collected; resolution happens offline.
            while ((int)frames.size() < kMaxDepth &&
                   StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, g_target, &sf,
                               &ctx, nullptr, SymFunctionTableAccess64,
                               SymGetModuleBase64, nullptr)) {
                if (sf.AddrPC.Offset == 0) break;
                frames.push_back(sf.AddrPC.Offset);
            }
        } else {
            g_samples_failed++;
        }
        ResumeThread(g_target);

        if (frames.empty()) continue;
        std::lock_guard<std::mutex> lk(g_stack_mtx);
        g_stacks[frames]++;
        g_samples_taken++;
    }
}

}  // namespace

void register_render_thread() {
    if (g_render_thread) return;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                    GetCurrentProcess(), &g_render_thread,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                    THREAD_QUERY_INFORMATION,
                    FALSE, 0);
}

bool prof_running() { return g_prof_on.load(std::memory_order_relaxed); }

bool prof_start(int interval_us) {
    if (g_prof_on.load()) return false;
    if (interval_us < 100) interval_us = 100;

    // prof_start() runs on the automation socket thread, so the target has to
    // be the handle the render thread registered — never GetCurrentThread().
    if (!g_render_thread) {
        spdlog::error("[perf] render thread not registered yet");
        return false;
    }
    g_target = g_render_thread;

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    {
        std::lock_guard<std::mutex> lk(g_stack_mtx);
        g_stacks.clear();
        g_samples_taken = g_samples_failed = 0;
    }
    g_prof_on.store(true);
    g_prof_thread = std::thread(sampler_loop, interval_us);
    spdlog::info("[perf] sampling profiler started ({} us)", interval_us);
    return true;
}

std::string prof_stop(const std::string& out_path) {
    if (!g_prof_on.load()) return "ERR profiler not running";
    g_prof_on.store(false);
    if (g_prof_thread.joinable()) g_prof_thread.join();

    std::map<std::vector<DWORD64>, uint32_t> stacks;
    uint64_t taken, failed;
    {
        std::lock_guard<std::mutex> lk(g_stack_mtx);
        stacks.swap(g_stacks);
        taken  = g_samples_taken;
        failed = g_samples_failed;
    }

    // The Release build carries no DWARF, but mingw leaves the COFF symbol
    // table in the exe, so addresses are resolved offline by
    // scratchpad/r14pf/resolve.py (nm + bisect). All that is needed here is
    // the runtime image base so the offline step can undo ASLR.
    DWORD64 base = (DWORD64)GetModuleHandleW(nullptr);
    DWORD64 size = 0;
    MODULEINFO mi{};
    if (GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr),
                             &mi, sizeof(mi)))
        size = mi.SizeOfImage;

    std::ofstream out(out_path);
    if (!out) return "ERR cannot write " + out_path;
    out << "# samples=" << taken << " failed=" << failed
        << " distinct_stacks=" << stacks.size()
        << " imagebase=0x" << std::hex << base
        << " imagesize=0x" << size << std::dec << "\n";
    // One line per distinct stack, innermost FIRST: "addr,addr,... <count>"
    for (auto& kv : stacks) {
        for (size_t i = 0; i < kv.first.size(); ++i) {
            if (i) out << ",";
            out << std::hex << kv.first[i] << std::dec;
        }
        out << " " << kv.second << "\n";
    }
    out.close();

    std::ostringstream o;
    o << "OK " << out_path << " samples=" << taken << " failed=" << failed
      << " stacks=" << stacks.size();
    return o.str();
}

#else   // !_WIN32

void register_render_thread() {}
bool prof_running() { return false; }
bool prof_start(int) { return false; }
std::string prof_stop(const std::string&) { return "ERR profiler is Windows-only"; }

#endif

}  // namespace perf
