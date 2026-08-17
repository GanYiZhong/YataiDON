#include "network.h"
#include "scores.h"
#include "color_utils.h"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <spdlog/spdlog.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <map>
#include <random>

NetworkClient network;

#if defined(NETWORK_ENABLED)

namespace {

template <std::size_t N>
struct ObfuscatedString {
    std::array<char, N> data{};

    constexpr ObfuscatedString(const char (&str)[N]) {
        for (std::size_t i = 0; i < N; ++i) data[i] = str[i] ^ key(i);
    }

    static constexpr char key(std::size_t i) {
        constexpr char seed[] = __TIME__;
        return seed[i % (sizeof(seed) - 1)] ^ static_cast<char>(i * 41 + 7);
    }

    std::string decode() const {
        std::string out(N - 1, '\0');
        for (std::size_t i = 0; i < N - 1; ++i) out[i] = data[i] ^ key(i);
        return out;
    }
};

std::string secret_key() {
    static constexpr ObfuscatedString obfuscated_key(NETWORK_AUTH_KEY);
    static const std::string key = obfuscated_key.decode();
    return key;
}

std::string random_nonce() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(dist(rng)),
                  static_cast<unsigned long long>(dist(rng)));
    return std::string(buf);
}

std::string current_timestamp() {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return std::to_string(secs.count());
}

std::array<uint8_t, 32> sha256_bytes(const std::string& data) {
    unsigned int* words = ray::ComputeSHA256(
        reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; ++i) {
        out[i * 4 + 0] = static_cast<uint8_t>(words[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(words[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(words[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(words[i]);
    }
    return out;
}

std::array<uint8_t, 32> hmac_sha256(const std::string& key, const std::string& message) {
    constexpr size_t block_size = 64;
    std::array<uint8_t, block_size> key_block{};
    if (key.size() > block_size) {
        auto hashed = sha256_bytes(key);
        std::copy(hashed.begin(), hashed.end(), key_block.begin());
    } else {
        std::copy(key.begin(), key.end(), key_block.begin());
    }

    std::string ipad_msg(key_block.begin(), key_block.end());
    for (auto& c : ipad_msg) c ^= 0x36;
    ipad_msg += message;
    auto inner_hash = sha256_bytes(ipad_msg);

    std::string opad_msg(key_block.begin(), key_block.end());
    for (auto& c : opad_msg) c ^= 0x5c;
    opad_msg.append(reinterpret_cast<const char*>(inner_hash.data()), inner_hash.size());
    return sha256_bytes(opad_msg);
}

std::string to_hex(const std::array<uint8_t, 32>& bytes) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string out(64, '0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = hex_chars[bytes[i] >> 4];
        out[i * 2 + 1] = hex_chars[bytes[i] & 0xF];
    }
    return out;
}

cpr::Header signed_headers(const std::string& method, const std::string& path,
                            const std::map<std::string, std::string>& params) {
    std::string timestamp = current_timestamp();
    std::string nonce = random_nonce();

    std::string canonical = method + "\n" + path + "\n";
    for (const auto& [k, v] : params) canonical += k + "=" + v + "&";
    canonical += "\n" + timestamp + "\n" + nonce;

    std::string signature = to_hex(hmac_sha256(secret_key(), canonical));

    return cpr::Header{
        {"X-Timestamp", timestamp},
        {"X-Nonce", nonce},
        {"X-Signature", signature},
        {"X-Client-Version", CLIENT_VERSION},
    };
}

}  // namespace

static std::string network_url(const std::string& endpoint) {
    std::string base = NETWORK_URL;
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base + endpoint;
}

void NetworkClient::check_heartbeat() {
    if (pending_heartbeat.has_value()) return;
    pending_heartbeat = cpr::GetAsync(
        cpr::Url{network_url("/health")},
        signed_headers("GET", "/health", {}),
        cpr::Timeout{2000}
    );
}

bool NetworkClient::check_import_requested(const std::string& access_code) {
    cpr::Response response = cpr::Get(
        cpr::Url{network_url("/user")},
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
        cpr::Url{network_url("/user")},
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
        cpr::Url{network_url("/clear_import_flag")},
        signed_headers("POST", "/clear_import_flag", {{"access_code", access_code}}),
        cpr::Parameters{{"access_code", access_code}},
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) {
        spdlog::error("Failed to clear import flag: HTTP {} - {}", response.status_code, response.text);
    }
}

std::string NetworkClient::register_user(const std::string& username) {
    cpr::Response response = cpr::Post(
        cpr::Url{network_url("/register_user")},
        signed_headers("POST", "/register_user", {{"username", username}}),
        cpr::Payload{{"username", username}},
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) {
        spdlog::error("Failed to register user: HTTP {} - {}", response.status_code, response.text);
        return "";
    }
    return response.text;
}

std::string NetworkClient::map_to_json(const std::map<double, InputLogType>& my_map) {
    rapidjson::Document doc;
    doc.SetObject();
    rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();

    for (const auto& pair : my_map) {
        rapidjson::Value key(std::to_string(pair.first).c_str(), allocator);
        doc.AddMember(key, (int)pair.second, allocator);
    }

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

void NetworkClient::submit_score(std::string& hash, int difficulty, const std::string& access_code, Score score, std::map<double, InputLogType> input_log) {
    std::map<std::string, std::string> params{
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
    };
    cpr::Response response = cpr::Post(
        cpr::Url{network_url("/submit_score")},
        signed_headers("POST", "/submit_score", params),
        cpr::Parameters{
            {"access_code", params["access_code"]},
            {"hash", params["hash"]},
            {"difficulty", params["difficulty"]},
            {"crown", params["crown"]},
            {"rank", params["rank"]},
            {"score", params["score"]},
            {"good", params["good"]},
            {"ok", params["ok"]},
            {"bad", params["bad"]},
            {"drumroll", params["drumroll"]},
            {"max_combo", params["max_combo"]},
            {"input_log", map_to_json(input_log)},
        },
        cpr::Timeout{5000}
    );
    if (response.status_code != 200) {
        spdlog::error("Failed to submit score: HTTP {} - {}", response.status_code, response.text);
    }
}

void NetworkClient::poll_song_jump(const std::string& access_code) {
    if (pending_song_jump.has_value()) return;
    pending_song_jump = cpr::GetAsync(
        cpr::Url{network_url("/poll_song_jump")},
        cpr::Parameters{{"access_code", access_code}},
        cpr::Timeout{5000}
    );
}

std::optional<std::string> NetworkClient::take_song_jump_result() {
    if (!song_jump_result.has_value()) return std::nullopt;
    std::optional<std::string> result = std::move(song_jump_result);
    song_jump_result.reset();
    return result;
}

void NetworkClient::update(double current_ms) {
    if (current_ms - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
        last_heartbeat_ms = current_ms;
        check_heartbeat();
    }

    if (pending_heartbeat.has_value() &&
        pending_heartbeat->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        cpr::Response response = pending_heartbeat->get();
        pending_heartbeat.reset();

        bool was_online = online;
        online = response.status_code == 200;
        if (online != was_online) {
            spdlog::info("hiroba heartbeat: {}", online ? "online" : "offline");
        }
    }

    if (pending_song_jump.has_value() &&
        pending_song_jump->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        cpr::Response response = pending_song_jump->get();
        pending_song_jump.reset();

        if (response.status_code == 200) {
            rapidjson::Document doc;
            if (!doc.Parse(response.text.c_str()).HasParseError() &&
                doc.HasMember("hash") && doc["hash"].IsString()) {
                song_jump_result = doc["hash"].GetString();
            }
        }
    }
}

#else

std::string NetworkClient::register_user(const std::string&) { return ""; }
void NetworkClient::submit_score(std::string&, int, const std::string&, Score) {}
bool NetworkClient::check_import_requested(const std::string&) { return false; }
void NetworkClient::clear_import_flag(const std::string&) {}
bool NetworkClient::fetch_chara_colors(const std::string&, ray::Color&, ray::Color&, ray::Color&) { return false; }
void NetworkClient::poll_song_jump(const std::string&) {}
std::optional<std::string> NetworkClient::take_song_jump_result() { return std::nullopt; }
void NetworkClient::update(double) {}

#endif
