#pragma once

#include "scores.h"

#if defined(NETWORK_URL) && defined(NETWORK_AUTH_KEY)
#define NETWORK_ENABLED 1
#ifdef _WIN32
#define CloseWindow CloseWindow_WinAPI
#define ShowCursor ShowCursor_WinAPI
#endif
#include <cpr/cpr.h>
#ifdef _WIN32
#undef CloseWindow
#undef ShowCursor
#endif
#endif

#include <string>

enum class InputLogType {
    KAT_L = 0,
    DON_L = 1,
    DON_R = 2,
    KAT_R = 3
};

class NetworkClient {
public:
    void update(double current_ms);

    bool is_online() const { return online; }

    std::string register_user(const std::string& username);
    std::string map_to_json(const std::map<double, InputLogType>& my_map);
    void submit_score(std::string& hash, int difficulty, const std::string& access_code, Score score, std::map<double, InputLogType> input_log);

    bool check_import_requested(const std::string& access_code);
    void clear_import_flag(const std::string& access_code);

    bool fetch_chara_colors(const std::string& access_code, ray::Color& color_1, ray::Color& color_2, ray::Color& color_3);

    void poll_song_jump(const std::string& access_code);
    std::optional<std::string> take_song_jump_result();

private:
    void check_heartbeat();

    bool online = false;
#if defined(NETWORK_ENABLED)
    std::optional<cpr::AsyncResponse> pending_heartbeat;
    static constexpr double HEARTBEAT_INTERVAL_MS = 30000.0;
    double last_heartbeat_ms = -HEARTBEAT_INTERVAL_MS;

    std::optional<cpr::AsyncResponse> pending_song_jump;
    std::optional<std::string> song_jump_result;
#endif
};

extern NetworkClient network;
