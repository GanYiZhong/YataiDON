#pragma once

// ---------------------------------------------------------------------------
// Opt-in automation harness.
//
// Normal players never touch any of this: without the `--automation <port>`
// command line flag `automation_enabled` stays false, the server thread is
// never created and the only cost anywhere in the hot path is one relaxed
// atomic bool load per input poll (see poll_keyboard_once in input.cpp).
//
// When enabled the engine listens on 127.0.0.1:<port> and speaks a tiny
// line-oriented text protocol (see AUTOMATION.md in the skin folder). Synthetic
// key state is ORed into the real key state *before* the rising/falling edge
// detection in poll_keyboard_once, so a scripted don/kat is indistinguishable
// from a physical one: same press/release buffers, same edge semantics, same
// consumption by check_key_pressed.
//
// Because the synthetic state bypasses GetAsyncKeyState (which the engine
// deliberately gates on window focus) the game can be driven while it is in the
// background and the user keeps typing in another window.
// ---------------------------------------------------------------------------

#include <array>
#include <atomic>
#include <string>

// Number of raylib key codes the input layer scans (32 .. 348).
constexpr int AUTOMATION_KEY_COUNT = 349;

extern std::atomic<bool> automation_enabled;
extern std::array<std::atomic<bool>, AUTOMATION_KEY_COUNT> automation_key_state;

// Start the TCP command server. Returns false if the socket could not be bound.
bool automation_start(int port);

// Stop the server and join its threads. Safe to call when never started.
void automation_shutdown();

// Called once per frame from the render thread, while the framebuffer still
// holds this frame's image. Services screenshot requests and refreshes the
// state snapshot the server thread answers `state`/`waitscreen` from.
void automation_frame_hook();

// True once a `quit` command has been received; the main loop breaks on it.
bool automation_quit_requested();

// Called once, right after InitWindow, when --automation is active.
//
// `offscreen` (the default for automation runs) parks the window outside the
// virtual desktop and shows it *without activating*, so the user never sees it
// and never loses focus, while the window keeps a real, composited swap chain -
// which is what a plain FLAG_WINDOW_HIDDEN window does NOT have, and why hidden
// mode used to capture only the background. See AUTOMATION.md.
//
// With `offscreen == false` the window is simply shown where it was created
// (still unfocused) - the debugging path.
void automation_prepare_window(bool offscreen);

// ---------------------------------------------------------------------------
// Scripted screen jump (`goto <SCREEN>` command).
//
// Some screens are unreachable by input alone under a normal config - GAME_OVER
// only ever follows RESULT when general.song_limit > 0 and that many songs have
// been played - which made them impossible to verify on-screen without editing
// the user's config.toml. `goto` records a request here; the main loop picks it
// up at the ONE point where a screen change is already legal (right after
// screen->update() returned no transition of its own) and performs it through
// the screen's own on_screen_end(), so teardown/startup are identical to a
// normal transition. No-op unless --automation is active.
// ---------------------------------------------------------------------------

// Returns true and writes the requested screen index into `out` if a jump is
// pending (clearing it); false otherwise.
bool automation_take_screen_request(int& out);

// ROUND 26 (r26-gaugesliver): pending `gotosong <folder path>` request (see
// automation.cpp). Returns true and writes the requested song folder path
// into `out` if a jump is pending (clearing it); false otherwise. Polled from
// SongSelectScreen::update() only, mirroring the existing network song-jump
// path (Navigator::jump_to_song). Testing-only.
bool automation_take_song_jump(std::string& out);

// Synthetic key state for one raylib key code. Bounds-checked.
inline bool automation_key_down(int key) {
    if (key < 0 || key >= AUTOMATION_KEY_COUNT) return false;
    return automation_key_state[key].load(std::memory_order_relaxed);
}
