#include "logging.h"

#include <iostream>
#include <sstream>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dup_filter_sink.h>
#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#endif
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <exception>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif
#if !defined(__ANDROID__) && !defined(YATAIDON_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
#include <cpptrace/cpptrace.hpp>
#endif

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>

// dbghelp is not reentrant/thread-safe; serialize entry so two threads
// crashing concurrently don't call into it at the same time.
static std::atomic_flag g_dbghelp_lock = ATOMIC_FLAG_INIT;

static void log_trace_from_context(CONTEXT* ctx) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    while (g_dbghelp_lock.test_and_set(std::memory_order_acquire)) {}

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    const bool sym_initialized = SymInitialize(process, nullptr, TRUE);
    if (!sym_initialized) {
        spdlog::critical("SymInitialize failed (error {}); stack trace may lack symbols", GetLastError());
    }

    CONTEXT ctx_copy = *ctx;
    STACKFRAME64 sf  = {};
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrStack.Mode   = AddrModeFlat;
    sf.AddrFrame.Mode   = AddrModeFlat;
#if defined(_M_X64)
    sf.AddrPC.Offset    = ctx_copy.Rip;
    sf.AddrStack.Offset = ctx_copy.Rsp;
    sf.AddrFrame.Offset = ctx_copy.Rbp;
    const DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    sf.AddrPC.Offset    = ctx_copy.Pc;
    sf.AddrStack.Offset = ctx_copy.Sp;
    sf.AddrFrame.Offset = ctx_copy.Fp;
    const DWORD machine_type = IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_IX86)
    sf.AddrPC.Offset    = ctx_copy.Eip;
    sf.AddrStack.Offset = ctx_copy.Esp;
    sf.AddrFrame.Offset = ctx_copy.Ebp;
    const DWORD machine_type = IMAGE_FILE_MACHINE_I386;
#else
    #error "Unsupported architecture for Windows stack walking"
#endif

    cpptrace::raw_trace raw;
    while (StackWalk64(
        machine_type, process, thread, &sf, &ctx_copy,
        nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr
    )) {
        if (sf.AddrPC.Offset == 0) break;
        raw.frames.push_back(static_cast<cpptrace::frame_ptr>(sf.AddrPC.Offset));
    }

    try {
        auto resolved = raw.resolve();
        std::ostringstream oss;
        resolved.print(oss, false);
        spdlog::critical("Stack trace:\n{}", oss.str());
    } catch (...) {
        spdlog::critical("(stack trace resolution failed)");
    }
    spdlog::default_logger()->flush();

    if (sym_initialized) SymCleanup(process);
    g_dbghelp_lock.clear(std::memory_order_release);
}

static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS* ep) {
    const char* exc_name = "Unknown exception";
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:    exc_name = "Access violation"; break;
        case EXCEPTION_STACK_OVERFLOW:      exc_name = "Stack overflow"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION: exc_name = "Illegal instruction"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:  exc_name = "FP divide by zero"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:  exc_name = "Int divide by zero"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exc_name = "Array bounds exceeded"; break;
    }
    spdlog::critical("Crash: {} (code 0x{:08X})", exc_name,
                     static_cast<unsigned>(ep->ExceptionRecord->ExceptionCode));
    log_trace_from_context(ep->ContextRecord);
    std::_Exit(1);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

static void log_stacktrace() {
#if !defined(__ANDROID__) && !defined(YATAIDON_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
    try {
        std::ostringstream oss;
        cpptrace::generate_trace().print(oss, false);
        spdlog::critical("Stack trace:\n{}", oss.str());
        spdlog::default_logger()->flush();
    } catch (...) {}
#endif
}

void handle_exception() {
    try {
        auto exception_ptr = std::current_exception();
        if (exception_ptr) std::rethrow_exception(exception_ptr);
    } catch (const std::exception& e) {
        spdlog::critical("Uncaught exception: {}", e.what());
    } catch (...) {
        spdlog::critical("Uncaught exception of unknown type");
    }
    log_stacktrace();
    std::_Exit(1);
}

void signal_handler(int signal) {
    if (signal == SIGINT) {
        // std::exit() runs static destructors (including the spdlog logger)
        // from signal-handler context, which is not async-signal-safe and can
        // deadlock if the interrupted thread was mid-log. Flush best-effort
        // and terminate immediately instead.
        auto logger = spdlog::default_logger();
        if (logger) logger->flush();
        std::_Exit(0);
    }
}

#ifndef _WIN32
static void write_hex(int fd, std::uintptr_t value) {
    char buf[2 + sizeof(value) * 2];
    buf[0] = '0';
    buf[1] = 'x';
    for (std::size_t i = 0; i < sizeof(value) * 2; i++) {
        int nibble = (value >> (4 * (sizeof(value) * 2 - 1 - i))) & 0xF;
        buf[2 + i] = nibble < 10 ? char('0' + nibble) : char('a' + nibble - 10);
    }
    (void)!write(fd, buf, sizeof(buf));
}

static void crash_signal_handler(int sig) {
    const char* name = "Unknown signal\n";
    switch (sig) {
        case SIGSEGV: name = "Crash: SIGSEGV (Segmentation fault)\n"; break;
        case SIGABRT: name = "Crash: SIGABRT (Abort)\n"; break;
        case SIGFPE:  name = "Crash: SIGFPE (Floating point exception)\n"; break;
        case SIGILL:  name = "Crash: SIGILL (Illegal instruction)\n"; break;
    }
    (void)!write(STDERR_FILENO, name, strlen(name));
#if !defined(__ANDROID__) && !defined(YATAIDON_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
    // Raw addresses only -- symbolizing (resolve()) allocates and is not
    // signal-safe. Pipe these through addr2line/cpptrace offline.
    cpptrace::frame_ptr frames[64];
    std::size_t count = cpptrace::safe_generate_raw_trace(frames, 64);
    for (std::size_t i = 0; i < count; i++) {
        write_hex(STDERR_FILENO, frames[i]);
        (void)!write(STDERR_FILENO, "\n", 1);
    }
#endif
    _exit(1);
}
#endif

static void install_crash_handlers() {
    std::set_terminate(handle_exception);
#ifdef _WIN32
    std::signal(SIGINT, signal_handler);
    SetUnhandledExceptionFilter(crash_exception_filter);
#else
    // Run the crash handler on its own stack so a stack-overflow SIGSEGV
    // (where the normal stack is exhausted) still gets caught. On modern
    // glibc SIGSTKSZ is not a compile-time constant and is too small for
    // spdlog formatting + cpptrace symbolisation, so enforce a floor.
    static std::vector<char> altstack(std::max<std::size_t>(SIGSTKSZ, 64 * 1024));
    stack_t ss{};
    ss.ss_sp = altstack.data();
    ss.ss_size = altstack.size();
    ss.ss_flags = 0;
    if (sigaltstack(&ss, nullptr) != 0) {
        spdlog::warn("sigaltstack failed: {}", strerror(errno));
    }

    struct sigaction sa{};
    sa.sa_handler = crash_signal_handler;
    sa.sa_flags = SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, nullptr) != 0) spdlog::warn("sigaction(SIGSEGV) failed: {}", strerror(errno));
    if (sigaction(SIGABRT, &sa, nullptr) != 0) spdlog::warn("sigaction(SIGABRT) failed: {}", strerror(errno));
    if (sigaction(SIGFPE,  &sa, nullptr) != 0) spdlog::warn("sigaction(SIGFPE) failed: {}", strerror(errno));
    if (sigaction(SIGILL,  &sa, nullptr) != 0) spdlog::warn("sigaction(SIGILL) failed: {}", strerror(errno));

    // Use sigaction (not std::signal) for consistent restart/mask semantics
    // with the crash handlers above.
    struct sigaction sa_int{};
    sa_int.sa_handler = signal_handler;
    sigemptyset(&sa_int.sa_mask);
    if (sigaction(SIGINT, &sa_int, nullptr) != 0) spdlog::warn("sigaction(SIGINT) failed: {}", strerror(errno));
#endif
}

static spdlog::level::level_enum parse_log_level(const std::string& log_level_str) {
    if (log_level_str == "debug") return spdlog::level::debug;
    if (log_level_str == "info") return spdlog::level::info;
    if (log_level_str == "warning") return spdlog::level::warn;
    if (log_level_str == "error") return spdlog::level::err;
    if (log_level_str == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

static void apply_flush_policy() {
    spdlog::flush_on(spdlog::level::critical);
    // Without a periodic flush the file trails several seconds behind
    // the game, which reads like a freeze wherever the log happens to
    // stop mid-line.
    spdlog::flush_every(std::chrono::seconds(1));
}

static void setup_fallback_logging(const std::string& log_level_str) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%^%l%$] %n: %v");
    auto logger = std::make_shared<spdlog::logger>("", console_sink);
    logger->set_level(parse_log_level(log_level_str));
    spdlog::set_default_logger(logger);
    apply_flush_policy();

    install_crash_handlers();
}

void setup_logging(const std::string& log_level_str) {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("[%^%l%$] %n: %v");

        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            "latest.log", true);
        file_sink->set_pattern("[%H:%M:%S.%e] [%l] %n: %v");

        auto dup_filter = std::make_shared<spdlog::sinks::dup_filter_sink_mt>(
            std::chrono::seconds(5));
        dup_filter->add_sink(file_sink);

#ifdef __ANDROID__
        auto android_sink = std::make_shared<spdlog::sinks::android_sink_mt>("YataiDON");
        std::vector<spdlog::sink_ptr> sinks {android_sink, dup_filter};
#else
        std::vector<spdlog::sink_ptr> sinks {console_sink, dup_filter};
#endif
        auto logger = std::make_shared<spdlog::logger>("",
            sinks.begin(), sinks.end());

        logger->set_level(parse_log_level(log_level_str));
        spdlog::set_default_logger(logger);
        apply_flush_policy();

        install_crash_handlers();

    } catch (const std::exception& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << " -- falling back to console-only logging" << std::endl;
        setup_fallback_logging(log_level_str);
    } catch (...) {
        std::cerr << "Log initialization failed with unknown exception -- falling back to console-only logging" << std::endl;
        setup_fallback_logging(log_level_str);
    }
}
