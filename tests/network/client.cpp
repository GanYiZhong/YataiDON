#include "network.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

GlobalData global_data;

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template<class Predicate>
static void wait_for(Predicate done) {
    for (int i = 0; i < 200; ++i) {
        if (done()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    throw std::runtime_error("asynchronous request timed out");
}

int main() {
    try {
        Config config{};
        global_data.config = &config;
        require(!network.probe_online(), "disabled networking must stay offline");
        config.network.online_play = true;
        require(network.probe_online(), "health probe");
        const auto code = network.register_user("iOS 测试 & +");
        require(code == "test-access", "registration/signature/Unicode encoding");
        require(network.check_import_requested(code), "import request");
        network.clear_import_flag(code);
        require(!network.check_import_requested(code), "clear import flag");

        std::string value;
        require(network.fetch_username(code, value) && value == "iOS 测试 & +", "username download");
        network.update_username(code, "更新 + &");
        require(network.fetch_username(code, value) && value == "更新 + &", "username update");
        require(network.fetch_title(code, value) && value == "Test title", "title");
        int bg = 0;
        require(network.fetch_title_bg(code, bg) && bg == 2, "title background");
        ray::Color a{}, b{}, c{};
        require(network.fetch_chara_colors(code, a, b, c) && a.r == 0x12 && b.g == 0xab && c.b == 0xff, "colors");
        network.update_costume(code, 3, 4, 5, true);
        int head = 0, body = 0, costume = 0;
        bool is_costume = false;
        require(network.fetch_costume(code, head, body, costume, is_costume)
                && head == 3 && body == 4 && costume == 5 && is_costume, "costume update/download");

        std::string hash = "test-song";
        Score score{Crown::FC, Rank{}, 987654, 100, 2, 1, 30, 99};
        const auto begin = std::chrono::steady_clock::now();
        network.submit_score(hash, 3, code, score, {{12.5, InputLogType::DON_L}},
                             1700000000, "{\"auto_play\":false}", true, 5);
        require(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(250), "score upload blocks caller");
        wait_for([&] { return !network.fetch_scores(code).empty(); });
        auto scores = network.fetch_scores(code);
        require(scores.size() == 1 && scores[0].hash == hash && scores[0].difficulty == 3
                && scores[0].score.score == 987654 && scores[0].score.max_combo == 99, "score round-trip");

        network.poll_song_jump(code);
        std::optional<std::string> jump;
        wait_for([&] {
            network.update(0);
            jump = network.take_song_jump_result();
            return jump.has_value();
        });
        require(*jump == hash && !network.take_song_jump_result(), "song jump consumed once");
        wait_for([&] { network.update(0); return network.is_outdated(); });

        // The mock server can return failure without touching a real backend.
        cpr::Post(cpr::Url{"http://127.0.0.1:18765/test/offline"});
        require(!network.probe_online(), "unreachable backend");
        cpr::Post(cpr::Url{"http://127.0.0.1:18765/test/online"});
        require(network.probe_online(), "reconnection");
        config.network.online_play = false;
        network.update(60000);
        require(!network.is_online() && network.register_user("disabled").empty(), "offline toggle");

        // Use public endpoints without credentials to verify native trust.
        auto good = cpr::Get(cpr::Url{"https://www.apple.com/"}, cpr::Timeout{15000});
        require(good.error.code == cpr::ErrorCode::OK && good.status_code == 200, "trusted HTTPS");
        auto bad = cpr::Get(cpr::Url{"https://self-signed.badssl.com/"}, cpr::Timeout{15000});
        require(bad.error.code == cpr::ErrorCode::PEER_FAILED_VERIFICATION,
                "untrusted TLS certificate must be rejected");
        std::cout << "PASS: iOS network client round-trips, async upload, offline/reconnect and TLS trust\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
