#include "config.h"
#include "ray.h"
#include <algorithm>

std::string getKeyString(int key_code) {
    // Handle alphanumeric keys
    if (key_code >= 65 && key_code <= 90) {
        return std::string(1, static_cast<char>(key_code));
    }
    if (key_code >= 48 && key_code <= 57) {
        return std::string(1, static_cast<char>(key_code));
    }

    // Map raylib key codes to strings
    static const std::map<int, std::string> key_map = {
        {ray::KEY_SPACE, "space"},
        {ray::KEY_ESCAPE, "escape"},
        {ray::KEY_ENTER, "enter"},
        {ray::KEY_TAB, "tab"},
        {ray::KEY_BACKSPACE, "backspace"},
        {ray::KEY_INSERT, "insert"},
        {ray::KEY_DELETE, "delete"},
        {ray::KEY_RIGHT, "right"},
        {ray::KEY_LEFT, "left"},
        {ray::KEY_DOWN, "down"},
        {ray::KEY_UP, "up"},
        {ray::KEY_PAGE_UP, "page_up"},
        {ray::KEY_PAGE_DOWN, "page_down"},
        {ray::KEY_HOME, "home"},
        {ray::KEY_END, "end"},
        {ray::KEY_CAPS_LOCK, "caps_lock"},
        {ray::KEY_SCROLL_LOCK, "scroll_lock"},
        {ray::KEY_NUM_LOCK, "num_lock"},
        {ray::KEY_PRINT_SCREEN, "print_screen"},
        {ray::KEY_PAUSE, "pause"},
        {ray::KEY_F1, "f1"}, {ray::KEY_F2, "f2"}, {ray::KEY_F3, "f3"}, {ray::KEY_F4, "f4"},
        {ray::KEY_F5, "f5"}, {ray::KEY_F6, "f6"}, {ray::KEY_F7, "f7"}, {ray::KEY_F8, "f8"},
        {ray::KEY_F9, "f9"}, {ray::KEY_F10, "f10"}, {ray::KEY_F11, "f11"}, {ray::KEY_F12, "f12"},
        {ray::KEY_LEFT_SHIFT, "left_shift"},
        {ray::KEY_LEFT_CONTROL, "left_control"},
        {ray::KEY_LEFT_ALT, "left_alt"},
        {ray::KEY_LEFT_SUPER, "left_super"},
        {ray::KEY_RIGHT_SHIFT, "right_shift"},
        {ray::KEY_RIGHT_CONTROL, "right_control"},
        {ray::KEY_RIGHT_ALT, "right_alt"},
        {ray::KEY_RIGHT_SUPER, "right_super"},
        {ray::KEY_KB_MENU, "kb_menu"},
        {ray::KEY_KP_0, "kp_0"}, {ray::KEY_KP_1, "kp_1"}, {ray::KEY_KP_2, "kp_2"},
        {ray::KEY_KP_3, "kp_3"}, {ray::KEY_KP_4, "kp_4"}, {ray::KEY_KP_5, "kp_5"},
        {ray::KEY_KP_6, "kp_6"}, {ray::KEY_KP_7, "kp_7"}, {ray::KEY_KP_8, "kp_8"},
        {ray::KEY_KP_9, "kp_9"},
        {ray::KEY_KP_DECIMAL, "kp_decimal"},
        {ray::KEY_KP_DIVIDE, "kp_divide"},
        {ray::KEY_KP_MULTIPLY, "kp_multiply"},
        {ray::KEY_KP_SUBTRACT, "kp_subtract"},
        {ray::KEY_KP_ADD, "kp_add"},
        {ray::KEY_KP_ENTER, "kp_enter"},
        {ray::KEY_KP_EQUAL, "kp_equal"},
        {ray::KEY_APOSTROPHE, "apostrophe"},
        {ray::KEY_COMMA, "comma"},
        {ray::KEY_MINUS, "minus"},
        {ray::KEY_PERIOD, "period"},
        {ray::KEY_SLASH, "slash"},
        {ray::KEY_SEMICOLON, "semicolon"},
        {ray::KEY_EQUAL, "equal"},
        {ray::KEY_LEFT_BRACKET, "left_bracket"},
        {ray::KEY_BACKSLASH, "backslash"},
        {ray::KEY_RIGHT_BRACKET, "right_bracket"},
        {ray::KEY_GRAVE, "grave"}
    };

    auto it = key_map.find(key_code);
    if (it != key_map.end()) {
        return it->second;
    }

    throw std::runtime_error("Unknown key code: " + std::to_string(key_code));
}

static int getKeyCode(const std::string& key) {
    // Unambiguous escaped form for round-tripping key codes that have no
    // known string representation (see getKeyStringSafe).
    if (key.rfind("code:", 0) == 0) {
        try {
            return std::stoi(key.substr(5));
        } catch (...) {
            throw std::runtime_error("Invalid key: " + key);
        }
    }

    // Handle single alphanumeric characters
    if (key.length() == 1 && std::isalnum(static_cast<unsigned char>(key[0]))) {
        return std::toupper(static_cast<unsigned char>(key[0]));
    }

    // Convert to uppercase for comparison
    std::string upper_key = key;
    std::transform(upper_key.begin(), upper_key.end(), upper_key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    // Map strings to raylib key codes
    static const std::map<std::string, int> key_map = {
        {"SPACE", ray::KEY_SPACE},
        {"ESCAPE", ray::KEY_ESCAPE},
        {"ENTER", ray::KEY_ENTER},
        {"TAB", ray::KEY_TAB},
        {"BACKSPACE", ray::KEY_BACKSPACE},
        {"INSERT", ray::KEY_INSERT},
        {"DELETE", ray::KEY_DELETE},
        {"RIGHT", ray::KEY_RIGHT},
        {"LEFT", ray::KEY_LEFT},
        {"DOWN", ray::KEY_DOWN},
        {"UP", ray::KEY_UP},
        {"PAGE_UP", ray::KEY_PAGE_UP},
        {"PAGE_DOWN", ray::KEY_PAGE_DOWN},
        {"HOME", ray::KEY_HOME},
        {"END", ray::KEY_END},
        {"CAPS_LOCK", ray::KEY_CAPS_LOCK},
        {"SCROLL_LOCK", ray::KEY_SCROLL_LOCK},
        {"NUM_LOCK", ray::KEY_NUM_LOCK},
        {"PRINT_SCREEN", ray::KEY_PRINT_SCREEN},
        {"PAUSE", ray::KEY_PAUSE},
        {"F1", ray::KEY_F1}, {"F2", ray::KEY_F2}, {"F3", ray::KEY_F3}, {"F4", ray::KEY_F4},
        {"F5", ray::KEY_F5}, {"F6", ray::KEY_F6}, {"F7", ray::KEY_F7}, {"F8", ray::KEY_F8},
        {"F9", ray::KEY_F9}, {"F10", ray::KEY_F10}, {"F11", ray::KEY_F11}, {"F12", ray::KEY_F12},
        {"LEFT_SHIFT", ray::KEY_LEFT_SHIFT},
        {"LEFT_CONTROL", ray::KEY_LEFT_CONTROL},
        {"LEFT_ALT", ray::KEY_LEFT_ALT},
        {"LEFT_SUPER", ray::KEY_LEFT_SUPER},
        {"RIGHT_SHIFT", ray::KEY_RIGHT_SHIFT},
        {"RIGHT_CONTROL", ray::KEY_RIGHT_CONTROL},
        {"RIGHT_ALT", ray::KEY_RIGHT_ALT},
        {"RIGHT_SUPER", ray::KEY_RIGHT_SUPER},
        {"KB_MENU", ray::KEY_KB_MENU},
        {"KP_0", ray::KEY_KP_0}, {"KP_1", ray::KEY_KP_1}, {"KP_2", ray::KEY_KP_2},
        {"KP_3", ray::KEY_KP_3}, {"KP_4", ray::KEY_KP_4}, {"KP_5", ray::KEY_KP_5},
        {"KP_6", ray::KEY_KP_6}, {"KP_7", ray::KEY_KP_7}, {"KP_8", ray::KEY_KP_8},
        {"KP_9", ray::KEY_KP_9},
        {"KP_DECIMAL", ray::KEY_KP_DECIMAL},
        {"KP_DIVIDE", ray::KEY_KP_DIVIDE},
        {"KP_MULTIPLY", ray::KEY_KP_MULTIPLY},
        {"KP_SUBTRACT", ray::KEY_KP_SUBTRACT},
        {"KP_ADD", ray::KEY_KP_ADD},
        {"KP_ENTER", ray::KEY_KP_ENTER},
        {"KP_EQUAL", ray::KEY_KP_EQUAL},
        {"APOSTROPHE", ray::KEY_APOSTROPHE},
        {"COMMA", ray::KEY_COMMA},
        {"MINUS", ray::KEY_MINUS},
        {"PERIOD", ray::KEY_PERIOD},
        {"SLASH", ray::KEY_SLASH},
        {"SEMICOLON", ray::KEY_SEMICOLON},
        {"EQUAL", ray::KEY_EQUAL},
        {"LEFT_BRACKET", ray::KEY_LEFT_BRACKET},
        {"BACKSLASH", ray::KEY_BACKSLASH},
        {"RIGHT_BRACKET", ray::KEY_RIGHT_BRACKET},
        {"GRAVE", ray::KEY_GRAVE}
    };

    auto it = key_map.find(upper_key);
    if (it != key_map.end()) {
        return it->second;
    }

    throw std::runtime_error("Invalid key: " + key);
}

static int getKeyCodeOrDefault(const std::string& v, const char* def) {
    try {
        return getKeyCode(v);
    } catch (const std::runtime_error& e) {
        spdlog::warn("{} -- using {}", e.what(), def);
        try {
            return getKeyCode(def);
        } catch (const std::runtime_error& e2) {
            spdlog::error("Invalid default key '{}': {} -- using escape", def, e2.what());
            return ray::KEY_ESCAPE;
        }
    }
}

static std::vector<int> parseKeyArray(const toml::array& arr) {
    std::vector<int> result;
    for (const auto& elem : arr) {
        if (elem.is_string()) {
            try {
                result.push_back(getKeyCode(elem.as_string()->get()));
            } catch (const std::runtime_error& e) {
                spdlog::warn("Skipping invalid key binding: {}", e.what());
            }
        }
    }
    return result;
}

std::vector<int> parseIntArray(const toml::array& arr) {
    std::vector<int> result;
    for (const auto& elem : arr) {
        if (auto val = elem.as_integer()) {
            result.push_back(static_cast<int>(val->get()));
        }
    }
    return result;
}

static std::vector<fs::path> parsePathArray(const toml::array& arr) {
    std::vector<fs::path> result;
    for (const auto& elem : arr) {
        if (elem.is_string()) {
            result.push_back(fs::path(elem.as_string()->get()));
        }
    }
    return result;
}

Config get_config() {
    fs::path config_path = fs::exists("dev-config.toml") ?
                            fs::path("dev-config.toml") :
                            fs::path("config.toml");

    toml::table config_file;
    try {
        config_file = toml::parse_file(config_path.string());
    } catch (const toml::parse_error& err) {
        std::error_code ec;
        if (!fs::exists(config_path, ec)) {
            spdlog::info("{} not found -- using defaults", config_path.string());
        } else {
            spdlog::error("Failed to parse {}: {} -- using defaults", config_path.string(), err.what());
            // Back up the unparsable file before defaults get written over it by
            // the next save_config(), so the user's real settings aren't lost.
            fs::path backup_path = config_path;
            backup_path += ".bak";
            fs::copy_file(config_path, backup_path, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                spdlog::error("Failed to back up {}: {}", config_path.string(), ec.message());
            } else {
                spdlog::warn("Backed up unparsable config to {}", backup_path.string());
            }
        }
    }

    Config config{};

    config.general.fps_counter = config_file["general"]["fps_counter"].value_or(false);
    config.general.audio_offset = config_file["general"]["audio_offset"].value_or(0);
    config.general.visual_offset = config_file["general"]["visual_offset"].value_or(0);
    config.general.language = config_file["general"]["language"].value_or("en");
    config.general.timer_frozen = config_file["general"]["timer_frozen"].value_or(false);
    config.general.song_timer = config_file["general"]["song_timer"].value_or(false);
    config.general.judge_counter = config_file["general"]["judge_counter"].value_or(false);
    config.general.log_level = config_file["general"]["log_level"].value_or("info");
    config.general.practice_mode_bar_delay = config_file["general"]["practice_mode_bar_delay"].value_or(0);
    config.general.score_method = config_file["general"]["score_method"].value_or("standard");
    config.general.display_bpm = config_file["general"]["display_bpm"].value_or(false);
    config.general.song_limit = config_file["general"]["song_limit"].value_or(0);
    config.general.webcam_number = config_file["general"]["webcam_number"].value_or(-1);
    config.general.player_1_id = config_file["general"]["player_1_id"].value_or(1);
    config.general.player_2_id = config_file["general"]["player_2_id"].value_or(1);
    config.general.touch_input = config_file["general"]["touch_input"].value_or(false);

    config.network.access_code = config_file["network"]["access_code"].value_or(
        config_file["general"]["access_code"].value_or(""));
    config.network.online_play = config_file["network"]["online_play"].value_or(
        config_file["general"]["online_play"].value_or(false));
    config.network.sync_scores = config_file["network"]["sync_scores"].value_or(
        config_file["general"]["sync_scores_on_launch"].value_or(false));

    // Parse paths
    if (auto tja_path = config_file["paths"]["tja_path"].as_array()) {
        config.paths.tja_path = parsePathArray(*tja_path);
    }
    config.paths.skin = fs::path(config_file["paths"]["skin"].value_or("PyTaikoGreen"));

    // Parse keys (converting from strings to key codes)
    config.keys.exit_key = getKeyCodeOrDefault(config_file["keys"]["exit_key"].value_or("escape"), "escape");
    config.keys.fullscreen_key = getKeyCodeOrDefault(config_file["keys"]["fullscreen_key"].value_or("f11"), "f11");
    config.keys.borderless_key = getKeyCodeOrDefault(config_file["keys"]["borderless_key"].value_or("f10"), "f10");
    config.keys.pause_key = getKeyCodeOrDefault(config_file["keys"]["pause_key"].value_or("p"), "p");
    config.keys.back_key = getKeyCodeOrDefault(config_file["keys"]["back_key"].value_or("escape"), "escape");
    config.keys.restart_key = getKeyCodeOrDefault(config_file["keys"]["restart_key"].value_or("r"), "r");

    // Parse keys_1p
    if (auto left_kat = config_file["keys_1p"]["left_kat"].as_array()) {
        auto parsed = parseKeyArray(*left_kat);
        if (!parsed.empty()) config.keys_1p.left_kat = std::move(parsed);
    }
    if (auto left_don = config_file["keys_1p"]["left_don"].as_array()) {
        auto parsed = parseKeyArray(*left_don);
        if (!parsed.empty()) config.keys_1p.left_don = std::move(parsed);
    }
    if (auto right_don = config_file["keys_1p"]["right_don"].as_array()) {
        auto parsed = parseKeyArray(*right_don);
        if (!parsed.empty()) config.keys_1p.right_don = std::move(parsed);
    }
    if (auto right_kat = config_file["keys_1p"]["right_kat"].as_array()) {
        auto parsed = parseKeyArray(*right_kat);
        if (!parsed.empty()) config.keys_1p.right_kat = std::move(parsed);
    }

    // Parse keys_2p
    if (auto left_kat = config_file["keys_2p"]["left_kat"].as_array()) {
        auto parsed = parseKeyArray(*left_kat);
        if (!parsed.empty()) config.keys_2p.left_kat = std::move(parsed);
    }
    if (auto left_don = config_file["keys_2p"]["left_don"].as_array()) {
        auto parsed = parseKeyArray(*left_don);
        if (!parsed.empty()) config.keys_2p.left_don = std::move(parsed);
    }
    if (auto right_don = config_file["keys_2p"]["right_don"].as_array()) {
        auto parsed = parseKeyArray(*right_don);
        if (!parsed.empty()) config.keys_2p.right_don = std::move(parsed);
    }
    if (auto right_kat = config_file["keys_2p"]["right_kat"].as_array()) {
        auto parsed = parseKeyArray(*right_kat);
        if (!parsed.empty()) config.keys_2p.right_kat = std::move(parsed);
    }

    // Parse gamepad_1p (fallback to legacy [gamepad] if missing)
    auto gamepad_1p_node = config_file["gamepad_1p"].as_table()
                         ? config_file["gamepad_1p"].as_table()
                         : config_file["gamepad"].as_table();
    if (gamepad_1p_node) {
        if (auto left_kat = (*gamepad_1p_node)["left_kat"].as_array()) {
            auto parsed = parseIntArray(*left_kat);
            if (!parsed.empty()) config.gamepad_1p.left_kat = std::move(parsed);
        }
        if (auto left_don = (*gamepad_1p_node)["left_don"].as_array()) {
            auto parsed = parseIntArray(*left_don);
            if (!parsed.empty()) config.gamepad_1p.left_don = std::move(parsed);
        }
        if (auto right_don = (*gamepad_1p_node)["right_don"].as_array()) {
            auto parsed = parseIntArray(*right_don);
            if (!parsed.empty()) config.gamepad_1p.right_don = std::move(parsed);
        }
        if (auto right_kat = (*gamepad_1p_node)["right_kat"].as_array()) {
            auto parsed = parseIntArray(*right_kat);
            if (!parsed.empty()) config.gamepad_1p.right_kat = std::move(parsed);
        }
    }

    // Parse gamepad_2p
    if (auto left_kat = config_file["gamepad_2p"]["left_kat"].as_array()) {
        auto parsed = parseIntArray(*left_kat);
        if (!parsed.empty()) config.gamepad_2p.left_kat = std::move(parsed);
    }
    if (auto left_don = config_file["gamepad_2p"]["left_don"].as_array()) {
        auto parsed = parseIntArray(*left_don);
        if (!parsed.empty()) config.gamepad_2p.left_don = std::move(parsed);
    }
    if (auto right_don = config_file["gamepad_2p"]["right_don"].as_array()) {
        auto parsed = parseIntArray(*right_don);
        if (!parsed.empty()) config.gamepad_2p.right_don = std::move(parsed);
    }
    if (auto right_kat = config_file["gamepad_2p"]["right_kat"].as_array()) {
        auto parsed = parseIntArray(*right_kat);
        if (!parsed.empty()) config.gamepad_2p.right_kat = std::move(parsed);
    }

    // Parse audio
    config.audio.device_type = config_file["audio"]["device_type"].value_or(0);
    config.audio.sample_rate = config_file["audio"]["sample_rate"].value_or(44100);
    config.audio.buffer_size = config_file["audio"]["buffer_size"].value_or(512);
    if (auto asio_channel = config_file["audio"]["asio_channel"].as_array())
        config.audio.asio_channel = parseIntArray(*asio_channel);
    if (config.audio.asio_channel.empty())
        config.audio.asio_channel.push_back(0);

    // Parse volume
    config.volume.sound = config_file["volume"]["sound"].value_or(1.0);
    config.volume.music = config_file["volume"]["music"].value_or(1.0);
    config.volume.voice = config_file["volume"]["voice"].value_or(1.0);
    config.volume.hitsound = config_file["volume"]["hitsound"].value_or(1.0);
    config.volume.attract_mode = config_file["volume"]["attract_mode"].value_or(1.0);

    // Parse video
    config.video.fullscreen = config_file["video"]["fullscreen"].value_or(false);
    config.video.borderless = config_file["video"]["borderless"].value_or(false);
    config.video.target_fps = config_file["video"]["target_fps"].value_or(60);
    config.video.vsync = config_file["video"]["vsync"].value_or(true);

    return config;
}

static std::string getKeyStringSafe(int key_code) {
    try {
        return getKeyString(key_code);
    } catch (const std::runtime_error& e) {
        spdlog::warn("{} -- saving escaped key code {}", e.what(), key_code);
        return "code:" + std::to_string(key_code);
    }
}

void save_config(const Config& config) {
    fs::path config_path = fs::exists("dev-config.toml") ?
                            fs::path("dev-config.toml") :
                            fs::path("config.toml");

    toml::table config_table;

    // General
    config_table.insert("general", toml::table{
        {"fps_counter", config.general.fps_counter},
        {"audio_offset", config.general.audio_offset},
        {"visual_offset", config.general.visual_offset},
        {"language", config.general.language},
        {"timer_frozen", config.general.timer_frozen},
        {"song_timer", config.general.song_timer},
        {"judge_counter", config.general.judge_counter},
        {"log_level", config.general.log_level},
        {"practice_mode_bar_delay", config.general.practice_mode_bar_delay},
        {"score_method", config.general.score_method},
        {"display_bpm", config.general.display_bpm},
        {"song_limit", config.general.song_limit},
        {"webcam_number", config.general.webcam_number},
        {"player_1_id", config.general.player_1_id},
        {"player_2_id", config.general.player_2_id},
        {"touch_input", config.general.touch_input}
    });

    // Network
    config_table.insert("network", toml::table{
        {"access_code", config.network.access_code},
        {"online_play", config.network.online_play},
        {"sync_scores", config.network.sync_scores}
    });

    // Paths
    toml::array tja_path_array;
    for (const auto& path : config.paths.tja_path) {
        tja_path_array.push_back(path.string());
    }
    config_table.insert("paths", toml::table{
        {"tja_path", tja_path_array},
        {"skin", config.paths.skin.string()}
    });

    // Keys
    config_table.insert("keys", toml::table{
        {"exit_key", getKeyStringSafe(config.keys.exit_key)},
        {"fullscreen_key", getKeyStringSafe(config.keys.fullscreen_key)},
        {"borderless_key", getKeyStringSafe(config.keys.borderless_key)},
        {"pause_key", getKeyStringSafe(config.keys.pause_key)},
        {"back_key", getKeyStringSafe(config.keys.back_key)},
        {"restart_key", getKeyStringSafe(config.keys.restart_key)}
    });

    // Keys 1P
    toml::array left_kat_1p, left_don_1p, right_don_1p, right_kat_1p;
    for (int key : config.keys_1p.left_kat) left_kat_1p.push_back(getKeyStringSafe(key));
    for (int key : config.keys_1p.left_don) left_don_1p.push_back(getKeyStringSafe(key));
    for (int key : config.keys_1p.right_don) right_don_1p.push_back(getKeyStringSafe(key));
    for (int key : config.keys_1p.right_kat) right_kat_1p.push_back(getKeyStringSafe(key));

    config_table.insert("keys_1p", toml::table{
        {"left_kat", left_kat_1p},
        {"left_don", left_don_1p},
        {"right_don", right_don_1p},
        {"right_kat", right_kat_1p}
    });

    // Keys 2P
    toml::array left_kat_2p, left_don_2p, right_don_2p, right_kat_2p;
    for (int key : config.keys_2p.left_kat) left_kat_2p.push_back(getKeyStringSafe(key));
    for (int key : config.keys_2p.left_don) left_don_2p.push_back(getKeyStringSafe(key));
    for (int key : config.keys_2p.right_don) right_don_2p.push_back(getKeyStringSafe(key));
    for (int key : config.keys_2p.right_kat) right_kat_2p.push_back(getKeyStringSafe(key));

    config_table.insert("keys_2p", toml::table{
        {"left_kat", left_kat_2p},
        {"left_don", left_don_2p},
        {"right_don", right_don_2p},
        {"right_kat", right_kat_2p}
    });

    toml::array gp1_left_kat, gp1_left_don, gp1_right_don, gp1_right_kat;
    for (int btn : config.gamepad_1p.left_kat)  gp1_left_kat.push_back(btn);
    for (int btn : config.gamepad_1p.left_don)  gp1_left_don.push_back(btn);
    for (int btn : config.gamepad_1p.right_don) gp1_right_don.push_back(btn);
    for (int btn : config.gamepad_1p.right_kat) gp1_right_kat.push_back(btn);

    config_table.insert("gamepad_1p", toml::table{
        {"left_kat", gp1_left_kat},
        {"left_don", gp1_left_don},
        {"right_don", gp1_right_don},
        {"right_kat", gp1_right_kat}
    });

    toml::array gp2_left_kat, gp2_left_don, gp2_right_don, gp2_right_kat;
    for (int btn : config.gamepad_2p.left_kat)  gp2_left_kat.push_back(btn);
    for (int btn : config.gamepad_2p.left_don)  gp2_left_don.push_back(btn);
    for (int btn : config.gamepad_2p.right_don) gp2_right_don.push_back(btn);
    for (int btn : config.gamepad_2p.right_kat) gp2_right_kat.push_back(btn);

    config_table.insert("gamepad_2p", toml::table{
        {"left_kat", gp2_left_kat},
        {"left_don", gp2_left_don},
        {"right_don", gp2_right_don},
        {"right_kat", gp2_right_kat}
    });

    // Audio
    toml::array asio_channel;
    for (int ch : config.audio.asio_channel) asio_channel.push_back(ch);

    config_table.insert("audio", toml::table{
        {"device_type", config.audio.device_type},
        {"sample_rate", config.audio.sample_rate},
        {"buffer_size", config.audio.buffer_size},
        {"asio_channel", asio_channel}
    });

    // Volume
    config_table.insert("volume", toml::table{
        {"sound", config.volume.sound},
        {"music", config.volume.music},
        {"voice", config.volume.voice},
        {"hitsound", config.volume.hitsound},
        {"attract_mode", config.volume.attract_mode}
    });

    // Video
    config_table.insert("video", toml::table{
        {"fullscreen", config.video.fullscreen},
        {"borderless", config.video.borderless},
        {"target_fps", config.video.target_fps},
        {"vsync", config.video.vsync}
    });

    fs::path tmp_path = config_path;
    tmp_path += ".tmp";
    {
        std::error_code rm_ec;
        std::ofstream ofs(tmp_path, std::ios::trunc);
        if (!ofs.is_open()) {
            spdlog::error("Failed to open {} for writing", tmp_path.string());
            return;
        }
        ofs << config_table;
        ofs.close();
        if (!ofs) {
            spdlog::error("Failed to write {}", tmp_path.string());
            fs::remove(tmp_path, rm_ec);
            return;
        }
    }

    std::error_code ec;
    fs::rename(tmp_path, config_path, ec);
    if (ec) {
        spdlog::error("Failed to save config.toml: {}", ec.message());
        std::error_code rm_ec;
        fs::remove(tmp_path, rm_ec);
    }
};
