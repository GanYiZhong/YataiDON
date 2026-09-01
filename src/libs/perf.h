#pragma once

// Round-14 performance instrumentation.
//
// Two independent things live here:
//
//  1. A per-frame timing recorder. `run_frame()` in YataiDON.cpp feeds it the
//     cost of the phases it can see (screen update, screen draw, the rlgl
//     batch flush inside EndDrawing, and the whole frame's *work* excluding
//     the 60 Hz pacing sleep). Samples are tagged with the screen name so the
//     stats can be reported per screen. Cost when nothing asks for the
//     numbers: four steady_clock reads and a push_back per frame.
//
//  2. A sampling profiler. A worker thread suspends the render thread every
//     `interval_us`, walks its stack with StackWalk64 (x64 unwind data lives
//     in the PE, so this works without a PDB) and aggregates identical stacks.
//     `prof_stop()` resolves them with cpptrace and writes a folded report.
//     Windows-only; a no-op elsewhere.
//
// Nothing here is compiled out, but everything except the four clock reads is
// behind an atomic bool that is false unless a profiling run was asked for
// over the automation socket.

#include <chrono>
#include <string>

namespace perf {

// ROUND 103: tiny scope timer for the event recorder's call sites. Taking a
// clock reading costs ~20 ns, but the sites below only construct one when the
// recorder is enabled, so a normal run pays nothing.
struct PerfTimer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double ms() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - t0).count();
    }
};

// --- frame timing -----------------------------------------------------------
void record_frame(const std::string& screen,
                  double update_ms, double draw_ms, double flush_ms,
                  double work_ms, unsigned long long cycles);
// Render-thread cycle counter (QueryThreadCycleTime). 0 off Windows.
unsigned long long thread_cycles();
void reset();
// JSON: overall + per-screen mean/p50/p95/p99/max of each phase, frame count,
// working set / private bytes.
std::string stats_json();
// Every sample, in order: screen,update_ms,draw_ms,flush_ms,work_ms.
// Percentiles hide hitches; this is how a spike gets located and
// attributed to a phase.
bool        dump_csv(const std::string& path);

// --- ROUND 103: one-off-event recorder --------------------------------------
//
// A frame-time series says WHEN a hitch happened; it cannot say WHAT did it.
// The sampling profiler below cannot answer it either: at `prof start 500` it
// suspends the render thread so hard the game managed 5 frames in 300 s, and
// its folded output is unsymbolised addresses (measured, ROUND 103).
//
// So: the handful of places that can do expensive one-time work report it
// here, stamped with the frame index the per-frame recorder is currently at,
// so an event lines up with a row of `perfdump`'s CSV. Dumped by the
// automation command `perfevents <path.csv>`.
//
// OFF unless the environment variable **YATAIDON_R103_TRACE** is set. When
// off the cost is one relaxed atomic bool load at each call site, and no
// timing is taken at all - see the `events_enabled()` guard each site uses.
bool        events_enabled();
void        note_event(const char* kind, const std::string& detail, double ms);
// CSV: frame,kind,detail_ms -> frame,kind,ms,detail
bool        dump_events(const std::string& path);
void        events_reset();

// --- per-Lua-hook timing ----------------------------------------------------
// LuaScript::call()/call_r() feed this when it is switched on over the
// automation socket ("luaperf on"). Off, the cost is one relaxed atomic load
// per Lua call.
extern bool lua_timing_enabled();
void        lua_timing(bool on);
void        lua_add(const char* ctx, double ms);
void        lua_reset();
std::string lua_json();

// --- draw-param parser mode (r14 perf A/B + equivalence check) -------------
// 0 = fast (single pass, the default), 1 = legacy (one lookup per key),
// 2 = check (run both and compare).
enum ParamsMode { PARAMS_FAST = 0, PARAMS_LEGACY = 1, PARAMS_CHECK = 2 };
extern int  params_mode();
void        params_mode(int mode);
void        params_mismatch();        // CHECK found a difference
void        params_checked();         // CHECK compared one table
std::string params_json();

// --- sampling profiler ------------------------------------------------------
void        register_render_thread();   // call once, from the render thread
bool        prof_start(int interval_us);
bool        prof_running();
std::string prof_stop(const std::string& out_path);

}  // namespace perf
