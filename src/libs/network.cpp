#include "network.h"
#include "scores.h"
#include "color_utils.h"
#include <rapidjson/document.h>
#include <spdlog/spdlog.h>

NetworkClient network;

#if defined(NETWORK_ENABLED)

void NetworkClient::check_heartbeat() {
    if (pending_heartbeat.has_value()) return;
    pending_heartbeat = cpr::GetAsync(
        cpr::Url{std::string(NETWORK_URL) + "/health"},
        cpr::Header{{"Authorization", "Bearer " NETWORK_AUTH_KEY}},
        cpr::Timeout{2000}
    );
}

bool NetworkClient::check_import_requested(const std::string& access_code) {
    cpr::Response response = cpr::Get(
        cpr::Url{std::string(NETWORK_URL) + "/user"},
        cpr::Parameters{{"access_code", access_code}},
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) return false;

    rapidjson::Document doc;
    if (doc.Parse(response.text.c_str()).HasParseError()) return false;
    return doc.HasMember("import_requested") && doc["import_requested"].IsBool()
        && doc["import_requested"].GetBool();
}

bool NetworkClient::fetch_chara_colors(const std::string& access_code, ray::Color& color_1, ray::Color& color_2, ray::Color& color_3) {
    cpr::Response response = cpr::Get(
        cpr::Url{std::string(NETWORK_URL) + "/user"},
        cpr::Parameters{{"access_code", access_code}},
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) return false;

    rapidjson::Document doc;
    if (doc.Parse(response.text.c_str()).HasParseError()) return false;
    if (!doc.HasMember("chara_color_1") || !doc["chara_color_1"].IsString()) return false;
    if (!doc.HasMember("chara_color_2") || !doc["chara_color_2"].IsString()) return false;
    if (!doc.HasMember("chara_color_3") || !doc["chara_color_3"].IsString()) return false;

    try {
        color_1 = parse_hex_color(doc["chara_color_1"].GetString());
        color_2 = parse_hex_color(doc["chara_color_2"].GetString());
        color_3 = parse_hex_color(doc["chara_color_3"].GetString());
    } catch (const std::invalid_argument&) {
        return false;
    }
    return true;
}

void NetworkClient::clear_import_flag(const std::string& access_code) {
    cpr::Response response = cpr::Post(
        cpr::Url{std::string(NETWORK_URL) + "/clear_import_flag"},
        cpr::Header{{"Authorization", "Bearer " NETWORK_AUTH_KEY}},
        cpr::Parameters{{"access_code", access_code}},
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) {
        spdlog::error("Failed to clear import flag: HTTP {} - {}", response.status_code, response.text);
    }
}

std::string NetworkClient::register_user(const std::string& username) {
    cpr::Response response = cpr::Post(
        cpr::Url{std::string(NETWORK_URL) + "/register_user"},
        cpr::Header{{"Authorization", "Bearer " NETWORK_AUTH_KEY}},
        cpr::Payload{{"username", username}},
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) {
        spdlog::error("Failed to register user: HTTP {} - {}", response.status_code, response.text);
        return "";
    }
    return response.text;
}

void NetworkClient::submit_score(std::string& hash, int difficulty, const std::string& access_code, Score score) {
    cpr::Response response = cpr::Post(
        cpr::Url{std::string(NETWORK_URL) + "/submit_score"},
        cpr::Header{{"Authorization", "Bearer " NETWORK_AUTH_KEY}},
        cpr::Parameters{
            {"access_code", access_code},
            {"hash", hash},
            {"difficulty", std::to_string(difficulty)},
            {"crown", std::to_string(static_cast<int>(score.crown))},
            {"rank", std::to_string(static_cast<int>(score.rank))},
            {"score", std::to_string(score.score)},
            {"good", std::to_string(score.good)},
            {"ok", std::to_string(score.ok)},
            {"bad", std::to_string(score.bad)},
            {"drumroll", std::to_string(score.drumroll)},
            {"max_combo", std::to_string(score.max_combo)},
        },
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) {
        spdlog::error("Failed to submit score: HTTP {} - {}", response.status_code, response.text);
    }
}

void NetworkClient::update(double current_ms) {
    if (current_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
        last_heartbeat_ms = current_ms;
        check_heartbeat();
    }

    if (!pending_heartbeat.has_value()) return;
    if (pending_heartbeat->wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;

    cpr::Response response = pending_heartbeat->get();
    pending_heartbeat.reset();

    bool was_online = online;
    online = response.status_code == 200;
    if (online != was_online) {
        spdlog::info("hiroba heartbeat: {}", online ? "online" : "offline");
    }
}

#else

std::string NetworkClient::register_user(const std::string&) { return ""; }
void NetworkClient::submit_score(std::string&, int, const std::string&, Score) {}
bool NetworkClient::check_import_requested(const std::string&) { return false; }
void NetworkClient::clear_import_flag(const std::string&) {}
bool NetworkClient::fetch_chara_colors(const std::string&, ray::Color&, ray::Color&, ray::Color&) { return false; }
void NetworkClient::update(double) {}

#endif
