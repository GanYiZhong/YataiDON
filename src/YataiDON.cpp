#include <cstdlib>
#include <fstream>
#include <iostream>
#include <rlgl.h>
#ifdef PLATFORM_ANDROID
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#endif

#include "libs/animation.h"
#include "libs/audio.h"
#include "libs/automation.h"
#include "libs/global_data.h"
#include "libs/filesystem.h"
#include "libs/input.h"
#include "libs/logging.h"
#include "libs/camera_utils.h"
#include "libs/network.h"
#include "libs/perf.h"
#include "libs/screen.h"
#include "libs/script.h"
#include "libs/song_parser.h"

#include "scenes/dan_result.h"
#include "scenes/dan_select.h"
#include "scenes/entry.h"
#include "scenes/game.h"
#include "scenes/game_2p.h"
#include "scenes/game_dan.h"
#include "scenes/game_practice.h"
#include "scenes/input_cali.h"
#include "scenes/input_test.h"
#include "scenes/copyright.h"
#include "scenes/loading.h"
#include "scenes/result.h"
#include "scenes/result_2p.h"
#include "scenes/sandbox.h"
#include "scenes/settings.h"
#include "scenes/skin_viewer.h"
#include "scenes/song_select.h"
#include "scenes/song_select_2p.h"
#include "scenes/song_select_practice.h"
#include "scenes/title.h"
#include "scenes/game_over.h"

#include "objects/global/fps_counter.h"

#ifdef _WIN32
    #define CloseWindow CloseWindow_WinAPI
    #define ShowCursor ShowCursor_WinAPI
    #include <windows.h>
    #undef CloseWindow
    #undef ShowCursor
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace fs = std::filesystem;


void draw_outer_border(int screen_width, int screen_height, ray::Color last_color) {
    DrawRectangle(-screen_width, 0, screen_width, screen_height, last_color);
    DrawRectangle(screen_width, 0, screen_width, screen_height, last_color);
    DrawRectangle(0, -screen_height, screen_width, screen_height, last_color);
    DrawRectangle(0, screen_height, screen_width, screen_height, last_color);
}

// --automation <port> / --automation-visible are stripped from argv before the
// normal argument parsing runs, so the harness flags are never mistaken for a
// song path. Both are inert unless explicitly passed.
static int  g_automation_port    = 0;
// Automation runs are invisible by default: the window is created hidden (so it
// never flashes anywhere) and then parked outside the virtual desktop and shown
// without activating. --automation-visible opts back into a normal window for
// debugging. --automation-hidden is the old, superseded spelling and now means
// the same thing as the default (a truly hidden window captured black).
static bool g_automation_visible = false;

static std::vector<char*> strip_automation_args(int argc, char* argv[]) {
    std::vector<char*> out;
    out.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--automation" && i + 1 < argc) {
            g_automation_port = std::atoi(argv[++i]);
            continue;
        }
        if (arg.rfind("--automation=", 0) == 0) {
            g_automation_port = std::atoi(arg.c_str() + 13);
            continue;
        }
        if (arg == "--automation-hidden") {          // legacy spelling
            continue;
        }
        if (arg == "--automation-visible") {
            g_automation_visible = true;
            continue;
        }
        out.push_back(argv[i]);
    }
    return out;
}

// Leaving check_args means the process has nothing left to do. std::exit() is
// not enough here: it runs the static destructors, and tearing the audio engine
// down from an already-initialised RtAudio/WASAPI stream blocks forever, so
// `YataiDON.exe <path that does not exist>` printed its error and then hung
// instead of returning to the shell. Flush what we have and leave immediately.
[[noreturn]] static void exit_now(int code) {
    std::cout.flush();
    std::cerr.flush();
    if (auto logger = spdlog::default_logger()) logger->flush();
    std::_Exit(code);
}

// A command-line path -> std::filesystem::path, without ever throwing.
//
// libstdc++ treats a `char` string handed to std::filesystem::path as UTF-8 and
// converts it to UTF-16 for the Win32 API. The narrow `argv` the mingw CRT
// builds is in the process' legacy ANSI code page instead (CP950 on this
// machine), so every non-ASCII song path - i.e. nearly every chart this project
// cares about - made that conversion fail and threw
// std::filesystem::filesystem_error straight out of check_args and out of main:
// `Crash: Unknown exception (code 0x20474343)` before the window ever opened.
// widen_argv() below now rebuilds argv as UTF-8 from the wide command line, and
// this helper does the UTF-8 -> UTF-16 step explicitly (falling back to the ANSI
// code page for a byte sequence that is not valid UTF-8, e.g. a path pasted from
// a legacy shell), so a path that still cannot be interpreted comes back empty
// and is reported as an error instead of crashing.
static std::filesystem::path path_from_arg(const std::string& arg) {
#ifdef _WIN32
    if (arg.empty()) return {};
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                arg.c_str(), -1, nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(CP_ACP, 0, arg.c_str(), -1, nullptr, 0);
    }
    if (n <= 1) return {};
    std::wstring wide(static_cast<size_t>(n - 1), L'\0');
    if (MultiByteToWideChar(cp, 0, arg.c_str(), -1, wide.data(), n) <= 0) return {};
    return std::filesystem::path(wide);
#else
    try {
        return std::filesystem::path(arg);
    } catch (const std::exception&) {
        return {};
    }
#endif
}

#ifdef _WIN32
// Replace the CRT's ANSI argv with a UTF-8 one taken from GetCommandLineW, so
// everything downstream (song path, --automation <port>, difficulty) sees the
// exact characters the user typed no matter what the console code page is.
// The storage is function-static and outlives main.
static void widen_argv(int& argc, char**& argv) {
    static std::vector<std::string> args;
    static std::vector<char*>       ptrs;
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (!wargv || wargc <= 0) return;               // keep the CRT's argv
    args.reserve(static_cast<size_t>(wargc));
    for (int i = 0; i < wargc; i++) {
        int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                    nullptr, 0, nullptr, nullptr);
        if (n <= 0) { LocalFree(wargv); return; }
        std::string utf8(static_cast<size_t>(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8.data(), n, nullptr, nullptr);
        args.push_back(std::move(utf8));
    }
    LocalFree(wargv);
    ptrs.reserve(args.size());
    for (auto& a : args) ptrs.push_back(a.data());
    argc = static_cast<int>(ptrs.size());
    argv = ptrs.data();
}
#endif

Screens check_args(int argc, char* argv[]) {
    if (argc == 1) {
        return Screens::LOADING;
    }

    std::string song_path;
    std::optional<int> difficulty;
    bool auto_play = false;
    bool practice = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--auto") {
            auto_play = true;
        } else if (arg == "--practice") {
            practice = true;
        } else if (arg == "--sandbox") {
            return Screens::SANDBOX;
        } else if (arg == "--skin-viewer") {
            return Screens::SKIN_VIEWER;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " <song_path> [difficulty] [--auto] [--practice]\n";
            std::cout << "  song_path   : Path to the TJA song file\n";
            std::cout << "  difficulty  : Difficulty level (optional, defaults to max difficulty)\n";
            std::cout << "  --auto      : Enable auto mode\n";
            std::cout << "  --practice  : Start in practice mode\n";
            std::cout << "  --skin-viewer : Open skin viewer\n";
            std::cout << "  --sandbox   : Open sandbox mode\n";
            std::cout << "  --automation <port> : Listen on 127.0.0.1:<port> for scripted input\n";
            std::cout << "                        (see Skins/<skin>/AUTOMATION.md)\n";
            std::cout << "  --automation-visible : With --automation, show the window on the desktop\n";
            std::cout << "                         (default: the window is parked off-screen)\n";
            exit_now(0);
        } else if (song_path.empty()) {
            song_path = arg;
        } else if (!difficulty.has_value()) {
            try {
                difficulty = std::stoi(arg);
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid difficulty value: " << arg << "\n";
                exit_now(1);
            }
        }
    }

    if (song_path.empty()) {
        // r30-danresult-fullrebuild: `--automation <port> --auto` (no song path)
        // used to hard-exit here even though automation mode navigates to a song
        // through the menus itself -- there was previously no way to combine
        // scripted menu navigation with force_auto_play, so every automated
        // capture of a *completed* song/dan run had to be a no-input guaranteed
        // FAIL. `force_auto_play` only needs to be latched before GAME starts;
        // it does not need a CLI song path. See ENGINE_BINDINGS.md.
        if (g_automation_port > 0) {
            if (auto_play) global_data.force_auto_play = true;
            return Screens::LOADING;
        }
        std::cerr << "Error: song_path is required\n";
        std::cerr << "Use --help for usage information\n";
        exit_now(1);
    }

    std::filesystem::path path = path_from_arg(song_path);
    if (path.empty()) {
        std::cerr << "Error: Song path could not be interpreted: " << song_path << "\n";
        exit_now(1);
    }
    std::error_code path_ec;
    if (!std::filesystem::exists(path, path_ec) || path_ec) {
        std::cerr << "Error: Song file not found: " << song_path << "\n";
        exit_now(1);
    }

    {
        std::error_code abs_ec;
        std::filesystem::path abs = std::filesystem::absolute(path, abs_ec);
        if (!abs_ec) path = abs;
    }
    SongParser tja(path);

    int selected_difficulty;
    if (difficulty.has_value()) {
        auto& course_data = tja.metadata.course_data;
        if (course_data.find(difficulty.value()) == course_data.end()) {
            std::cerr << "Error: Invalid difficulty: " << difficulty.value() << ". Available: ";
            for (const auto& [key, value] : course_data) {
                std::cerr << key << " ";
            }
            std::cerr << "\n";
            exit_now(1);
        }
        selected_difficulty = difficulty.value();
    } else {
        if (tja.metadata.course_data.empty()) {
            selected_difficulty = static_cast<int>(Difficulty::EASY);
        } else {
            selected_difficulty = std::max_element(
                tja.metadata.course_data.begin(),
                tja.metadata.course_data.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; }
            )->first;
        }
    }

    Screens current_screen = practice ? Screens::GAME_PRACTICE : Screens::GAME;
    global_data.session_data[(int)PlayerNum::P1].selected_song = path;
    global_data.session_data[(int)PlayerNum::P1].selected_difficulty = selected_difficulty;
    if (auto_play) global_data.force_auto_play = true;

    return current_screen;
}

struct LoopState {
    std::unordered_map<Screens, std::unique_ptr<Screen>> screens;
    Screens current_screen = Screens::LOADING;
    ray::Camera2D camera   = {};
    int screen_width       = 0;
    int screen_height      = 0;
    std::chrono::steady_clock::duration target_duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
    std::chrono::time_point<std::chrono::steady_clock> next_frame_time = std::chrono::steady_clock::now();
    FPSCounter fps_counter;
    ray::Color last_color = ray::BLACK;
    TextureResizeAnimation* touch_drum_resize = nullptr;
    // ROUND 15 (r15-audit-global): the arcade's generic scene transition, see
    // screen_fade_* below.  0 start = not running.
    double screen_fade_start = 0.0;
};

// ---------------------------------------------------------------------------
// The arcade's generic scene transition (`loading/loading_kuro`)
//
// Every screen change in this engine is a hard cut.  The cabinet never cuts: it owns three
// transition clips as SHARED LumenPlayers (`App::SharedResourceManager` preloads them at
// indices 10/11/12 = `loading_kuro` / `loading_song` / `loading_kuro_result`), and
// `App::LoadingHelper::StartFadeIn` picks between them with
//     index = (song id string is empty) ? 10 + 2*flag : 11
// so: a scene change with a song = the rainbow rig, a scene change into the result = the slow
// black plate, and EVERY OTHER scene change = `loading_kuro`.
//
// `loading_kuro` (root sprite 4, 95 frames, labels init 0 / in 4 / stop 34 / out 64 / end 94) is
// one full-stage opaque BLACK plate whose alpha ramps linearly 0 -> 256 over frames 4..34
// (500 ms), holds while the next scene loads, then 256 -> 0 over frames 64..94 (500 ms).
//
// Only the second half is portable here.  A screen's `update()` returns the next screen from
// inside `on_screen_end()`, i.e. the old screen has already torn itself down by the time the
// loop learns a change is coming, so there is nothing left to fade OUT over.  What this does is
// the `out` half: 500 ms of black over the new screen, ramping to nothing - the same curve, the
// same duration, drawn over the incoming scene instead of split across both.
//
// OFF unless the skin declares `screen_fade_ms` (x = duration in ms), so PyTaikoGreen and any
// other skin keep the hard cut.  Screens that already own a transition are excluded, otherwise
// the black plate would sit on top of the rainbow rig / the result fade / the boot white-out.
static bool screen_fade_applies(Screens from, Screens to) {
    switch (to) {
        // the song-loading rainbow rig (`loading_song`) covers these
        case Screens::GAME: case Screens::GAME_2P: case Screens::GAME_DAN:
        case Screens::GAME_PRACTICE: case Screens::AI_GAME:
        // `loading_kuro_result` covers these
        case Screens::RESULT: case Screens::RESULT_2P: case Screens::DAN_RESULT:
            return false;
        default: break;
    }
    // the boot screen ends on its own white flash into TITLE (`attract/notice` depth 6)
    if (from == Screens::LOADING) return false;
    // ROUND 79: COPYRIGHT is a boot-chain scene; the cabinet hard-cuts into it out of
    // the notice white-out and hard-cuts out of it into BNLogo, so no fade either side.
    if (from == Screens::COPYRIGHT || to == Screens::COPYRIGHT) return false;
    // ROUND 17: SONG_SELECT -> DAN_SELECT is owned by the 段位道場 shutter. The rig
    // closes the doors on SONG_SELECT and OPENS them on DAN_SELECT (the cabinet's
    // `loading_dani_intro` f187..200, drawn through Indicator::draw_top), so a black
    // plate here would sit on top of the reveal and hide it - exactly the defect this
    // exclusion list exists for.
    if (from == Screens::SONG_SELECT && to == Screens::DAN_SELECT) return false;
    // ROUND 86: ENTRY -> DAN_SELECT is the cabinet's OWN route into the dojo (the 段位道場
    // mode board), and there the whole `loading_dani_intro` runs on DAN_SELECT starting from
    // label `in` (f5), whose first ~50 frames are a full-screen black scrim (`#1@3` alpha 1
    // to f55). The intro therefore already owns the blackout; a generic plate on top of it
    // would only hide the reveal, exactly as on the SONG_SELECT route above.
    if (from == Screens::ENTRY && to == Screens::DAN_SELECT) return false;
    return true;
}

static LoopState* g_loop = nullptr;
double g_frame_ms = 0.0;

static void run_frame() {
    LoopState& L = *g_loop;

    g_frame_ms = get_current_ms();

    // TEMP r14-hud instrumentation
    static bool dbg = getenv("YATAIDON_FRAME_DEBUG") != nullptr;
    #define FMARK(x) do { if (dbg) { spdlog::warn("[framedbg] {}", x); spdlog::default_logger()->flush(); } } while(0)
    FMARK("0:frame_top");
    ray::PollInputEvents();
    FMARK("0b:after_pollinput");
#ifdef __EMSCRIPTEN__
    poll_keyboard_once();
#endif
    poll_touch_once();

    auto frame_start = std::chrono::steady_clock::now();

    // One-shot: hand the perf sampler a handle to this (the render) thread.
    static bool perf_registered = (perf::register_render_thread(), true);
    (void)perf_registered;
    const unsigned long long cyc0 = perf::thread_cycles();

    if (check_key_pressed(global_data.config->keys.fullscreen_key)) {
        ray::ToggleFullscreen();
        spdlog::info("Toggled fullscreen");
    } else if (check_key_pressed(global_data.config->keys.borderless_key)) {
        ray::ToggleBorderlessWindowed();
        spdlog::info("Toggled borderless windowed mode");
    }

    L.camera = compute_camera2d(L.screen_width, L.screen_height);

    FMARK("0c:before_begindrawing");
    ray::BeginDrawing();
    FMARK("0d:after_begindrawing");

    if (global_data.camera.border_color != L.last_color) {
        ray::ClearBackground(global_data.camera.border_color);
        L.last_color = global_data.camera.border_color;
    }

    ray::BeginMode2D(L.camera);
    ray::BeginBlendMode(ray::BLEND_CUSTOM_SEPARATE);

    Screen* screen = L.screens[L.current_screen].get();
    const std::string& drew_screen = screen->screen_name;

    FMARK("0e:before_network");
    network.update(g_frame_ms);
    FMARK("0f:after_network");
    auto t_upd0 = std::chrono::steady_clock::now();
    std::optional<Screens> next_screen = screen->update();
    FMARK("0g:after_screen_update");
    auto t_upd1 = std::chrono::steady_clock::now();

    if (screen->screen_init) {
        screen->_do_draw();
    }
    // `loading_kuro`'s `out` half, over the screen that has just come up (see
    // screen_fade_applies above).  Drawn after the screen and before the debug overlays, which
    // is where the cabinet's transition player sits in its own draw order.
    if (L.screen_fade_start > 0.0) {
        const SkinInfo* cfg = tex.skin_entry("screen_fade_ms");
        const double dur = (cfg && cfg->x > 0) ? cfg->x : 500.0;
        const double t = (g_frame_ms - L.screen_fade_start) / dur;
        if (t >= 1.0) {
            L.screen_fade_start = 0.0;
        } else {
            const float a = static_cast<float>(1.0 - (t < 0.0 ? 0.0 : t));
            ray::DrawRectangle(0, 0, tex.screen_width, tex.screen_height, ray::Fade(ray::BLACK, a));
        }
    }
    FMARK("0h:after_screen_draw");
    auto t_drw1 = std::chrono::steady_clock::now();

    // ROUND 103: a screen-level marker for a slow DRAW phase. Deliberately
    // separate from the Chara3D sub-timers: if `slow_draw` fires on a frame
    // and `chara3d_slow_draw` does NOT, the cost is somewhere else in the
    // screen's draw and I must not blame the mascot for it.
    if (perf::events_enabled()) {
        const double dms =
            std::chrono::duration<double, std::milli>(t_drw1 - t_upd1).count();
        if (dms > 5.0) perf::note_event("slow_draw", drew_screen, dms);
    }

    // Scripted `goto <SCREEN>`: only ever consulted when the screen did not ask
    // for a transition itself, and routed through the screen's own
    // on_screen_end() so teardown matches a normal change. No-op without
    // --automation (automation_take_screen_request returns false immediately).
    if (!next_screen.has_value()) {
        int requested = 0;
        if (automation_take_screen_request(requested)) {
            Screens want = static_cast<Screens>(requested);
            if (want != L.current_screen) next_screen = screen->on_screen_end(want);
        }
    }

    if (next_screen.has_value()) {
        spdlog::info("Screen changed from {} to {}", L.current_screen, next_screen.value());
        clear_input_buffers();
        // arm the generic black transition, unless the destination owns one already
        if (tex.skin_entry("screen_fade_ms") && screen_fade_applies(L.current_screen, next_screen.value()))
            L.screen_fade_start = g_frame_ms;
        else
            L.screen_fade_start = 0.0;
        // ROUND 86 — stamped BEFORE current_screen is overwritten; DAN_SELECT reads it in
        // on_screen_start to tell the ENTRY mode-board entry (whole intro movie) from the
        // SONG_SELECT shutter hand-off (reveal half only).
        global_data.previous_screen = global_data.current_screen;
        L.current_screen = next_screen.value();
        global_data.current_screen = screens_to_string(L.current_screen);
        global_data.input_locked = 0;
    }

    if (global_data.config->general.touch_input) {
        if (touch_drum_pressed.exchange(false, std::memory_order_relaxed))
            L.touch_drum_resize->restart();
        L.touch_drum_resize->update(get_current_ms());
        const float scale = (float)L.touch_drum_resize->attribute;
        float y_fix = 0.0f;
        auto drum_it = global_tex.textures.find(OVERLAY::TOUCH_DRUM);
        if (drum_it != global_tex.textures.end())
            y_fix = drum_it->second->height * 0.5f * (1.0f - scale);
        global_tex.draw_texture(OVERLAY::TOUCH_DRUM, {.scale=scale, .center=true, .y=y_fix, .fade=0.5f});
    }

    if (global_data.config->general.fps_counter) {
        L.fps_counter.update();
        L.fps_counter.draw();
    }

    draw_outer_border(L.screen_width, L.screen_height, L.last_color);

    // No-op unless --automation is active; grabs this frame's framebuffer for
    // `shot` and refreshes the snapshot behind `state` / `waitscreen`.
    automation_frame_hook();
    FMARK("1:after_automation_hook");

    ray::EndBlendMode();
    ray::EndMode2D();
    FMARK("2:after_endmode2d");
    auto t_flush0 = std::chrono::steady_clock::now();
    ray::EndDrawing();
    FMARK("3:after_enddrawing");
    auto t_flush1 = std::chrono::steady_clock::now();

    if (!next_screen.has_value()) {
        ray::SwapScreenBuffer();
        FMARK("4:after_swapscreenbuffer");
    }

    // Round-14 perf instrumentation: the cost of the frame's *work*, i.e.
    // everything except the 60 Hz pacing spin below. `screen_name` is the
    // screen that actually drew this frame (captured before a transition
    // could have swapped it).
    {
        auto t_end = std::chrono::steady_clock::now();
        using ms = std::chrono::duration<double, std::milli>;
        perf::record_frame(drew_screen,
                           ms(t_upd1 - t_upd0).count(),
                           ms(t_drw1 - t_upd1).count(),
                           ms(t_flush1 - t_flush0).count(),
                           ms(t_end - frame_start).count(),
                           perf::thread_cycles() - cyc0);
    }
    FMARK("5:after_perf_record");
    if (ray::IsKeyPressed(ray::KEY_F12)) {
        static int screenshot_counter = 0;
        ray::Image image = ray::LoadImageFromScreen();
        if (L.current_screen == Screens::RESULT) {
            ray::ImageCrop(&image, {0, 0, (float)image.width, (float)image.height / 2.0f});
        }
        ray::ExportImage(image, ray::TextFormat("screenshot%03i.png", screenshot_counter));
        ray::UnloadImage(image);
        screenshot_counter++;
        spdlog::info("Screenshot saved");
    }

#ifndef __EMSCRIPTEN__
    if (L.target_duration.count() > 0) {
        L.next_frame_time += L.target_duration;
        auto now = std::chrono::steady_clock::now();
        if (L.next_frame_time < now) {
            L.next_frame_time = now;
        }
        auto spin_start = L.next_frame_time - std::chrono::microseconds(500);
        if (spin_start > now) {
            std::this_thread::sleep_until(spin_start);
        }
        while (std::chrono::steady_clock::now() < L.next_frame_time) { }
    }
#endif
    FMARK("6:frame_end");
    #undef FMARK
}

int main(int argc, char* argv[]) {
    spdlog::info("Starting YataiDON");
#ifdef _WIN32
    widen_argv(argc, argv);
#endif
    std::vector<char*> filtered_argv = strip_automation_args(argc, argv);
    argc = (int)filtered_argv.size();
    argv = filtered_argv.data();
    set_working_directory_to_executable();
    init_scores_manager();
    global_data.config = new Config(get_config());
    unsigned int flags = ray::FLAG_WINDOW_RESIZABLE;
    if (global_data.config->video.vsync) {
        flags |= ray::FLAG_VSYNC_HINT;
        spdlog::info("VSync enabled");
    }
    if (g_automation_port > 0) {
        // Never steal focus from whatever the user is doing, and keep the loop
        // running at full speed while the window is in the background.
        flags |= ray::FLAG_WINDOW_UNFOCUSED | ray::FLAG_WINDOW_ALWAYS_RUN;
        // Created hidden so it never appears at its default position; it is
        // moved off-screen and shown (without activating) right after
        // InitWindow by automation_prepare_window().
        if (!g_automation_visible) flags |= ray::FLAG_WINDOW_HIDDEN;
    }
    ray::SetConfigFlags(flags);
    ray::SetTraceLogLevel(ray::LOG_ERROR);
    setup_logging(global_data.config->general.log_level);

    fs::path root_skin_path = fs::path("Skins") / global_data.config->paths.skin;

    tex.init(root_skin_path / "Graphics");

#ifdef PLATFORM_ANDROID
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
    ray::InitWindow(tex.screen_width, tex.screen_height, "YataiDON");

    if (g_automation_port > 0) automation_prepare_window(!g_automation_visible);

    global_tex.init(root_skin_path / "Graphics");
    global_tex.load_screen_textures("global");
    script_manager.init(root_skin_path / "Scripts");
    // A child skin may leave font.ttf to its parent - or carry a git symlink
    // that Windows checked out as a 36-byte text file, which stb_truetype
    // would crash on. Only a real TrueType/OpenType file is used; anything
    // else falls back to the parent's.
    {
        fs::path font = root_skin_path / "Graphics/font.ttf";
        auto looks_like_font = [](const fs::path& p) {
            std::ifstream f(p, std::ios::binary);
            unsigned char magic[4] = {0, 0, 0, 0};
            if (!f.read(reinterpret_cast<char*>(magic), 4)) return false;
            // 00 01 00 00 (TrueType), "OTTO" (CFF OpenType), "true", "ttcf"
            return (magic[0] == 0 && magic[1] == 1 && magic[2] == 0 && magic[3] == 0) ||
                   memcmp(magic, "OTTO", 4) == 0 || memcmp(magic, "true", 4) == 0 ||
                   memcmp(magic, "ttcf", 4) == 0;
        };
        if (!looks_like_font(font) && global_tex.has_parent_skin()) {
            spdlog::warn("Skin font {} is not a usable font file, using the parent skin's", font.string());
            font = global_tex.parent_root() / "Graphics/font.ttf";
        }
        font_manager.init(font);
    }
    audio.init_audio_device(root_skin_path / "Sounds", global_data.config->audio, global_data.config->volume);

    scores_manager.player_1 = global_data.config->general.player_1_id;
    scores_manager.player_2 = global_data.config->general.player_2_id;
    if (auto pd = scores_manager.get_player_data(scores_manager.player_1))
        scores_manager.player_1_data = *pd;
    if (auto pd = scores_manager.get_player_data(scores_manager.player_2))
        scores_manager.player_2_data = *pd;

    if (global_data.config->network.access_code.empty()) {
        std::string access_code = network.register_user(scores_manager.player_1_data.username);
        if (!access_code.empty()) {
            global_data.config->network.access_code = access_code;
            save_config(*global_data.config);
        }
    }

    if (!global_data.config->network.access_code.empty() &&
        network.check_import_requested(global_data.config->network.access_code)) {
        spdlog::info("hiroba requested a score import, exporting scores.db");
        scores_manager.export_to_hiroba(global_data.config->network.access_code, scores_manager.player_1);
        network.clear_import_flag(global_data.config->network.access_code);
    }

    if (!global_data.config->network.access_code.empty()) {
        ray::Color chara_color_1, chara_color_2, chara_color_3;
        if (network.fetch_chara_colors(global_data.config->network.access_code, chara_color_1, chara_color_2, chara_color_3)) {
            scores_manager.player_1_data.chara_color_1 = chara_color_1;
            scores_manager.player_1_data.chara_color_2 = chara_color_2;
            scores_manager.player_1_data.chara_color_3 = chara_color_3;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        std::string server_username;
        if (network.fetch_username(global_data.config->network.access_code, server_username) &&
            !server_username.empty() && server_username != scores_manager.player_1_data.username) {
            scores_manager.player_1_data.username = server_username;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        std::string server_title;
        if (network.fetch_title(global_data.config->network.access_code, server_title) &&
            !server_title.empty() && server_title != scores_manager.player_1_data.title) {
            scores_manager.player_1_data.title = server_title;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        int server_title_bg;
        if (network.fetch_title_bg(global_data.config->network.access_code, server_title_bg) &&
            server_title_bg != scores_manager.player_1_data.title_bg) {
            scores_manager.player_1_data.title_bg = server_title_bg;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        int head_index, body_index, cos_index;
        bool is_costume;
        if (network.fetch_costume(global_data.config->network.access_code, head_index, body_index, cos_index, is_costume) &&
            (head_index != scores_manager.player_1_data.chara_head_index ||
             body_index != scores_manager.player_1_data.chara_body_index ||
             cos_index != scores_manager.player_1_data.chara_cos_index ||
             is_costume != scores_manager.player_1_data.chara_is_costume)) {
            scores_manager.player_1_data.chara_head_index = head_index;
            scores_manager.player_1_data.chara_body_index = body_index;
            scores_manager.player_1_data.chara_cos_index = cos_index;
            scores_manager.player_1_data.chara_is_costume = is_costume;
            scores_manager.save_player_data(scores_manager.player_1_data);
        }

        if (global_data.config->network.sync_scores) {
            scores_manager.sync_from_server(global_data.config->network.access_code);
        }
    }

    Screens initial_screen = check_args(argc, argv);

    double target_fps = global_data.config->video.target_fps;
    if (target_fps != -1) {
        spdlog::info("Target FPS set to {}", target_fps);
    }

    g_loop = new LoopState();
    LoopState& L = *g_loop;

    L.screen_width       = tex.screen_width;
    L.screen_height      = tex.screen_height;
    L.current_screen     = initial_screen;
    global_data.current_screen = screens_to_string(initial_screen);
    L.target_duration    = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / target_fps));
    L.touch_drum_resize  = (TextureResizeAnimation*)global_tex.get_animation(66);
    L.touch_drum_resize->start();

    L.screens[Screens::ENTRY]           = std::make_unique<EntryScreen>();
    L.screens[Screens::TITLE]           = std::make_unique<TitleScreen>();
    L.screens[Screens::SONG_SELECT]     = std::make_unique<SongSelectScreen>();
    L.screens[Screens::SONG_SELECT_2P]  = std::make_unique<SongSelect2PScreen>();
    L.screens[Screens::LOADING]         = std::make_unique<LoadingScreen>();
    L.screens[Screens::COPYRIGHT]       = std::make_unique<CopyrightScreen>();
    L.screens[Screens::GAME]            = std::make_unique<GameScreen>();
    L.screens[Screens::GAME_2P]         = std::make_unique<Game2PScreen>();
    L.screens[Screens::GAME_PRACTICE]   = std::make_unique<PracticeGameScreen>();
    L.screens[Screens::PRACTICE_SELECT] = std::make_unique<PracticeSongSelectScreen>();
    L.screens[Screens::RESULT]          = std::make_unique<ResultScreen>();
    L.screens[Screens::RESULT_2P]       = std::make_unique<Result2PScreen>();
    L.screens[Screens::DAN_SELECT]      = std::make_unique<DanSelectScreen>();
    L.screens[Screens::GAME_DAN]        = std::make_unique<DanGameScreen>();
    L.screens[Screens::DAN_RESULT]      = std::make_unique<DanResultScreen>();
    L.screens[Screens::SETTINGS]        = std::make_unique<SettingsScreen>();
    L.screens[Screens::INPUT_CALI]      = std::make_unique<InputCaliScreen>();
    L.screens[Screens::SKIN_VIEWER]     = std::make_unique<SkinViewerScreen>();
    L.screens[Screens::SANDBOX]         = std::make_unique<SandboxScreen>();
    L.screens[Screens::GAME_OVER]       = std::make_unique<GameOverScreen>();
    L.screens[Screens::INPUT_TEST]      = std::make_unique<InputTestScreen>();

    L.camera = compute_camera2d(L.screen_width, L.screen_height);

#ifndef __EMSCRIPTEN__
    if (global_data.config->video.borderless) {
        ray::ToggleBorderlessWindowed();
        spdlog::info("Borderless window enabled");
    }
    if (global_data.config->video.fullscreen) {
        ray::ToggleFullscreen();
        spdlog::info("Fullscreen enabled");
    }
#endif

    rlSetBlendFactorsSeparate(RL_SRC_ALPHA, RL_ONE_MINUS_SRC_ALPHA, RL_ONE, RL_ONE_MINUS_SRC_ALPHA, RL_FUNC_ADD, RL_FUNC_ADD);
#if defined(PLATFORM_ANDROID) || defined(__EMSCRIPTEN__)
    ray::SetExitKey(ray::KEY_NULL);
#else
    ray::SetExitKey(global_data.config->keys.exit_key);
#endif
    ray::HideCursor();

    if (g_automation_port > 0 && !automation_start(g_automation_port)) {
        spdlog::error("Automation server failed to start on port {}", g_automation_port);
    }

    L.next_frame_time = std::chrono::steady_clock::now();
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(run_frame, 0, 1);
#else
    input_thread = std::thread(input_polling_thread);

#ifdef PLATFORM_ANDROID
    while (!ray::WindowShouldClose() && !automation_quit_requested()) {
#else
    while (!ray::WindowShouldClose() && !automation_quit_requested() &&
           !check_key_pressed(global_data.config->keys.exit_key)) {
#endif
        run_frame();
    }

    automation_shutdown();
    input_thread_running = false;
    if (input_thread.joinable()) {
        input_thread.join();
    }
    shutdown_sdl_joysticks();
    delete g_loop;
    global_tex.unload_textures();
    tex.unload_textures();
    script_manager.shutdown();
    ray::CloseWindow();
    audio.close_audio_device();
    spdlog::info("Game closed");
#endif
}
