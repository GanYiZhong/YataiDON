#include <chrono>

#ifdef YATAIDON_PLATFORM_IOS
#include "../platform/ios.h"
#endif

inline double get_current_ms() {
#ifdef YATAIDON_PLATFORM_IOS
    return ios_game_time_ms();
#else
    using namespace std::chrono;
    auto now = high_resolution_clock::now();
    return duration<double, std::milli>(now.time_since_epoch()).count();
#endif
}

extern double g_frame_ms;

inline double get_frame_ms() {
    return (g_frame_ms > 0.0) ? g_frame_ms : get_current_ms();
}
