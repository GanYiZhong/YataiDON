#include "scores.h"
#include "color_utils.h"
#include "network.h"
#include "song_parser.h"
#include <numeric>

static void ensure_dan_table(sqlite3* db);

ScoresManager::ScoresManager(const fs::path& db_path) {
    if (sqlite3_open(db_path.string().c_str(), &db_fsd) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_fsd);
        sqlite3_close(db_fsd);
        db_fsd = nullptr;
        throw std::runtime_error("Failed to open database: " + err);
    }

    int version = 0;
    auto callback = [](void* data, int argc, char** argv, char**) -> int {
        if (argc > 0 && argv && argv[0]) *static_cast<int*>(data) = std::atoi(argv[0]);
        return 0;
    };
    sqlite3_exec(db_fsd, "PRAGMA user_version;", callback, &version, nullptr);

    std::string create_players =
        "CREATE TABLE IF NOT EXISTS players"
        "(player_id TEXT PRIMARY KEY,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "username TEXT NOT NULL UNIQUE,"
        "title TEXT NOT NULL,"
        "title_bg INTEGER NOT NULL DEFAULT 0,"
        "dan INTEGER NOT NULL DEFAULT -1,"
        "gold BOOL NOT NULL DEFAULT 0,"
        "rainbow BOOL NOT NULL DEFAULT 0,"
        "modifier_auto BOOL NOT NULL DEFAULT 0,"
        "modifier_speed INTEGER NOT NULL DEFAULT 10,"
        "modifier_display BOOL NOT NULL DEFAULT 0,"
        "modifier_inverse BOOL NOT NULL DEFAULT 0,"
        "modifier_random INTEGER NOT NULL DEFAULT 0,"
        "neiro_index INTEGER NOT NULL DEFAULT 0,"
        "chara_color_1 TEXT NOT NULL DEFAULT '#68BFC0',"
        "chara_color_2 TEXT NOT NULL DEFAULT '#F94728',"
        "chara_color_3 TEXT NOT NULL DEFAULT '#F9F0E1',"
        "chara_head_index INTEGER NOT NULL DEFAULT 0,"
        "chara_body_index INTEGER NOT NULL DEFAULT 0,"
        "chara_cos_index INTEGER NOT NULL DEFAULT 0,"
        "chara_is_costume BOOL NOT NULL DEFAULT 1,"
        "chara_paint_index INTEGER NOT NULL DEFAULT 0,"
        "chara_face_index INTEGER NOT NULL DEFAULT 0,"
        "chara_acce_index INTEGER NOT NULL DEFAULT 0,"
        "modifier_skip BOOL NOT NULL DEFAULT 0);";

    std::string create_songs =
        "CREATE TABLE IF NOT EXISTS songs"
        "(song_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "hash_0 TEXT,"
        "hash_1 TEXT,"
        "hash_2 TEXT,"
        "hash_3 TEXT,"
        "hash_4 TEXT,"
        "title TEXT NOT NULL,"
        "subtitle TEXT,"
        "UNIQUE(hash_0, hash_1, hash_2, hash_3, hash_4));";

    std::string create_scores =
        "CREATE TABLE IF NOT EXISTS scores"
        "(score_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "player_id TEXT NOT NULL REFERENCES players(player_id),"
        "hash TEXT,"
        "difficulty INTEGER NOT NULL,"
        "crown INTEGER NOT NULL,"
        "rank INTEGER NOT NULL,"
        "score INTEGER NOT NULL,"
        "good INTEGER NOT NULL,"
        "ok INTEGER NOT NULL,"
        "bad INTEGER NOT NULL,"
        "drumroll INTEGER NOT NULL,"
        "max_combo INTEGER NOT NULL,"
        "played_at INTEGER NOT NULL DEFAULT 0,"
        "modifiers TEXT NOT NULL DEFAULT '{}');";

    char* errmsg = nullptr;
    if (sqlite3_exec(db_fsd, create_players.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        spdlog::error("Failed to create players table: {}", errmsg);
        sqlite3_free(errmsg);
    }
    if (sqlite3_exec(db_fsd, create_songs.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        spdlog::error("Failed to create songs table: {}", errmsg);
        sqlite3_free(errmsg);
    }
    if (sqlite3_exec(db_fsd, create_scores.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        spdlog::error("Failed to create scores table: {}", errmsg);
        sqlite3_free(errmsg);
    }

    auto run_migration = [&](const char* sql) -> bool {
        char* mig_errmsg = nullptr;
        if (sqlite3_exec(db_fsd, sql, nullptr, nullptr, &mig_errmsg) == SQLITE_OK) return true;
        bool duplicate_column = mig_errmsg && std::string(mig_errmsg).find("duplicate column") != std::string::npos;
        if (!duplicate_column) spdlog::error("ScoresManager migration failed: {} ({})", mig_errmsg ? mig_errmsg : "?", sql);
        sqlite3_free(mig_errmsg);
        return duplicate_column;
    };

    bool migrations_ok = true;
    if (version < 2) {
        const char* migrations[] = {
            "ALTER TABLE players ADD COLUMN modifier_auto BOOL NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN title TEXT NOT NULL DEFAULT '';",
            "ALTER TABLE players ADD COLUMN title_bg INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN dan INTEGER NOT NULL DEFAULT -1;",
            "ALTER TABLE players ADD COLUMN gold BOOL NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN rainbow BOOL NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN modifier_speed INTEGER NOT NULL DEFAULT 10;",
            "ALTER TABLE players ADD COLUMN modifier_display BOOL NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN modifier_inverse BOOL NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN modifier_random INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN neiro_index INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN chara_color_1 TEXT NOT NULL DEFAULT '#68BFC0';",
            "ALTER TABLE players ADD COLUMN chara_color_2 TEXT NOT NULL DEFAULT '#F94728';",
            "ALTER TABLE players ADD COLUMN chara_color_3 TEXT NOT NULL DEFAULT '#F9F0E1';",
            "ALTER TABLE players ADD COLUMN chara_head_index INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN chara_body_index INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN chara_cos_index INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN chara_is_costume BOOL NOT NULL DEFAULT 1;",
            "ALTER TABLE players ADD COLUMN chara_paint_index INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN chara_face_index INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE players ADD COLUMN chara_acce_index INTEGER NOT NULL DEFAULT 0;",
        };
        for (const char* sql : migrations) {
            if (!run_migration(sql)) migrations_ok = false;
        }
    }
    run_migration("ALTER TABLE scores ADD COLUMN played_at INTEGER NOT NULL DEFAULT 0;");

    if (version < 4) {
        if (!run_migration("ALTER TABLE scores ADD COLUMN modifiers TEXT NOT NULL DEFAULT '{}';"))
            migrations_ok = false;
    }

    // Migration v5: Convert integer player_ids to string access codes
    if (version < 5) {
        spdlog::info("Running migration v5: Converting integer player_ids to string access codes");

        // First, check if there are integer player_ids that need conversion
        // Use GLOB for pattern matching (SQLite doesn't support [^...] in LIKE)
        // Also exclude very long numeric IDs (long numeric access codes from server)
        sqlite3_stmt* stmt;
        const char* check_query = "SELECT COUNT(*) FROM players WHERE player_id NOT GLOB '*[^0-9]*' AND player_id != '' AND player_id NOT IN ('0') AND LENGTH(player_id) <= 10;";
        int has_integer_ids = 0;
        if (sqlite3_prepare_v2(db_fsd, check_query, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                has_integer_ids = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }

        if (has_integer_ids > 0) {
            spdlog::info("Found {} integer player_ids to migrate", has_integer_ids);

            // Get all integer player_ids and their usernames
            // Only process IDs with length <= 10 (excludes long server access codes)
            const char* select_query = "SELECT player_id, username FROM players WHERE player_id NOT GLOB '*[^0-9]*' AND player_id != '' AND player_id NOT IN ('0') AND LENGTH(player_id) <= 10;";
            if (sqlite3_prepare_v2(db_fsd, select_query, -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* old_id_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    const char* username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

                    // Use strtoll with overflow checking instead of atoi
                    long long old_id = 0;
                    if (old_id_text) {
                        char* endptr = nullptr;
                        old_id = std::strtoll(old_id_text, &endptr, 10);
                        // Check for overflow or invalid input
                        if (endptr == old_id_text || *endptr != '\0' || old_id > INT_MAX || old_id < 0) {
                            spdlog::warn("Skipping invalid/overflow player_id: {}", old_id_text);
                            continue;
                        }
                    }
                    std::string new_id;

                    // Map old integer IDs to new string access codes
                    if (old_id == 1) new_id = "0";
                    else if (old_id == 2) new_id = "1";
                    else new_id = std::to_string(old_id);

                    spdlog::info("Migrating player {} (username: {}) to access code {}", old_id, username ? username : "unknown", new_id);

                    // Check if new_id already exists
                    sqlite3_stmt* check_stmt;
                    const char* check_exist = "SELECT 1 FROM players WHERE player_id = ?;";
                    if (sqlite3_prepare_v2(db_fsd, check_exist, -1, &check_stmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(check_stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
                        if (sqlite3_step(check_stmt) != SQLITE_ROW) {

                            // Update players table - use old_id_text for WHERE clause
                            const char* update_player = "UPDATE players SET player_id = ? WHERE player_id = ?;";
                            sqlite3_stmt* update_stmt;
                            if (sqlite3_prepare_v2(db_fsd, update_player, -1, &update_stmt, nullptr) == SQLITE_OK) {
                                sqlite3_bind_text(update_stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(update_stmt, 2, old_id_text, -1, SQLITE_TRANSIENT);
                                sqlite3_step(update_stmt);
                                sqlite3_finalize(update_stmt);
                            }

                            // Update scores table
                            const char* update_scores = "UPDATE scores SET player_id = ? WHERE player_id = ?;";
                            if (sqlite3_prepare_v2(db_fsd, update_scores, -1, &update_stmt, nullptr) == SQLITE_OK) {
                                sqlite3_bind_text(update_stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(update_stmt, 2, old_id_text, -1, SQLITE_TRANSIENT);
                                sqlite3_step(update_stmt);
                                sqlite3_finalize(update_stmt);
                            }

                            // Update dan_results table
                            const char* update_dan = "UPDATE dan_results SET player_id = ? WHERE player_id = ?;";
                            if (sqlite3_prepare_v2(db_fsd, update_dan, -1, &update_stmt, nullptr) == SQLITE_OK) {
                                sqlite3_bind_text(update_stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
                                sqlite3_bind_text(update_stmt, 2, old_id_text, -1, SQLITE_TRANSIENT);
                                sqlite3_step(update_stmt);
                                sqlite3_finalize(update_stmt);
                            }
                        }
                        sqlite3_finalize(check_stmt);
                    }
                }
                sqlite3_finalize(stmt);
            }

            // Also ensure "0" and "1" exist for default players if they don't
            const char* ensure_defaults =
                "INSERT OR IGNORE INTO players (player_id, username, title) VALUES ('0', 'Don-chan', 'Donder Debut!');"
                "INSERT OR IGNORE INTO players (player_id, username, title) VALUES ('1', 'Kat-chan', 'Donder Debut!');"
                "UPDATE players SET title_bg = 2, chara_color_1 = '#F94728', chara_color_2 = '#68BFC0' WHERE player_id = '1' AND username = 'Kat-chan';";
            run_migration(ensure_defaults);
        }

        if (migrations_ok) sqlite3_exec(db_fsd, "PRAGMA user_version = 5;", nullptr, nullptr, nullptr);
        else spdlog::error("ScoresManager: migration v5 failed, not raising user_version");
    }

    // Migration v6: access codes went from 24 to 20 digits (server dropped the 4 leading digits).
    if (version < 6) {
        ensure_dan_table(db_fsd);
        bool ok = run_migration(
            "BEGIN;"
            "UPDATE OR IGNORE players SET player_id = substr(player_id, 5) WHERE LENGTH(player_id) = 24 AND player_id NOT GLOB '*[^0-9]*';"
            "UPDATE scores SET player_id = substr(player_id, 5) WHERE LENGTH(player_id) = 24 AND player_id NOT GLOB '*[^0-9]*';"
            "UPDATE OR IGNORE dan_results SET player_id = substr(player_id, 5) WHERE LENGTH(player_id) = 24 AND player_id NOT GLOB '*[^0-9]*';"
            "COMMIT;");
        if (ok) sqlite3_exec(db_fsd, "PRAGMA user_version = 6;", nullptr, nullptr, nullptr);
        else {
            sqlite3_exec(db_fsd, "ROLLBACK;", nullptr, nullptr, nullptr);
            spdlog::error("ScoresManager: migration v6 failed, not raising user_version");
        }
    }

    run_migration("ALTER TABLE players ADD COLUMN modifier_skip BOOL NOT NULL DEFAULT 0;");

    // Default player "1" (2P placeholder): Kat-chan, 2P chara colors, 2P title color. Only touches the untouched default row.
    sqlite3_exec(db_fsd,
        "UPDATE players SET username = 'Kat-chan', title_bg = 1, chara_color_1 = '#F94728', chara_color_2 = '#68BFC0' "
        "WHERE player_id = '1' AND username = 'Player 2' AND NOT EXISTS (SELECT 1 FROM players WHERE username = 'Kat-chan');",
        nullptr, nullptr, nullptr);

    load_score_cache();
}

ScoresManager::~ScoresManager() {
    if (add_song_stmt) sqlite3_finalize(add_song_stmt);
    if (db_fsd) sqlite3_close(db_fsd);
}

void ScoresManager::load_score_cache() {
    std::lock_guard<std::mutex> lock(maps_mutex);
    score_cache.clear();

    sqlite3_stmt* stmt;
    const char* query =
        "SELECT player_id, hash, difficulty, score, good, ok, bad, drumroll, max_combo, crown, rank "
        "FROM scores "
        "ORDER BY crown DESC, score DESC;";
    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("load_score_cache: failed to prepare statement: {}", sqlite3_errmsg(db_fsd));
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* player_id_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string player_id = player_id_text ? player_id_text : "";
        const unsigned char* hash_text = sqlite3_column_text(stmt, 1);
        std::string hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        int difficulty = sqlite3_column_int(stmt, 2);

        // Rows are ordered best-first, so the first row seen for a key is the best one.
        auto key = std::make_tuple(hash, difficulty, player_id);
        if (score_cache.count(key)) continue;

        Score s;
        s.score     = sqlite3_column_int(stmt, 3);
        s.good      = sqlite3_column_int(stmt, 4);
        s.ok        = sqlite3_column_int(stmt, 5);
        s.bad       = sqlite3_column_int(stmt, 6);
        s.drumroll  = sqlite3_column_int(stmt, 7);
        s.max_combo = sqlite3_column_int(stmt, 8);
        s.crown     = static_cast<Crown>(sqlite3_column_int(stmt, 9));
        s.rank      = static_cast<Rank>(sqlite3_column_int(stmt, 10));
        score_cache.emplace(std::move(key), s);
    }
    sqlite3_finalize(stmt);
}


void ScoresManager::py_taiko_import(const fs::path& old_db_path) {
    // Build reverse lookup: (en_name, jp_name) -> new hashes
    using NameKey = std::pair<std::string, std::string>;
    struct PairHash {
        size_t operator()(const NameKey& k) const {
            return std::hash<std::string>{}(k.first) ^ (std::hash<std::string>{}(k.second) << 1);
        }
    };
    std::unordered_map<NameKey, std::array<std::string, 5>, PairHash> name_to_hashes;

    std::unordered_map<fs::path, std::array<std::string, 5>> path_to_hashes_snapshot;
    {
        std::lock_guard<std::mutex> lock(maps_mutex);
        path_to_hashes_snapshot = path_to_hashes;
    }

    for (const auto& [path, hashes] : path_to_hashes_snapshot) {
        try {
            SongParser parser(path);
            std::string en = parser.metadata.title.count("en") ? parser.metadata.title.at("en") : "";
            std::string ja = parser.metadata.title.count("ja") ? parser.metadata.title.at("ja") : "";
            name_to_hashes[{en, ja}] = hashes;
        } catch (const std::exception& e) {
            spdlog::warn("py_taiko_import: failed to parse metadata for {}: {}", path.string(), e.what());
        }
    }

    spdlog::info("py_taiko_import: built name lookup with {} entries", name_to_hashes.size());

    // Open old DB
    sqlite3* old_db;
    if (sqlite3_open(old_db_path.string().c_str(), &old_db) != SQLITE_OK) {
        spdlog::error("py_taiko_import: failed to open old DB at {}: {}", old_db_path.string(), sqlite3_errmsg(old_db));
        sqlite3_close(old_db);
        return;
    }

    // Ensure the target player row exists (scores import into the current 1P profile).
    sqlite3_exec(db_fsd,
        ("INSERT OR IGNORE INTO players (player_id, username, title) VALUES ('"
         + player_1 + "', 'Don-chan', 'Donder Debut!');").c_str(),
        nullptr, nullptr, nullptr);

    sqlite3_stmt* sel;
    const char* select_query =
        "SELECT en_name, jp_name, diff, score, good, ok, bad, drumroll, combo, clear "
        "FROM Scores;";

    if (sqlite3_prepare_v2(old_db, select_query, -1, &sel, nullptr) != SQLITE_OK) {
        spdlog::error("py_taiko_import: failed to prepare SELECT: {}", sqlite3_errmsg(old_db));
        sqlite3_close(old_db);
        return;
    }

    int imported = 0, skipped = 0;

    sqlite3_exec(db_fsd, "BEGIN;", nullptr, nullptr, nullptr);

    while (sqlite3_step(sel) == SQLITE_ROW) {
        const char* en_raw = reinterpret_cast<const char*>(sqlite3_column_text(sel, 0));
        const char* ja_raw = reinterpret_cast<const char*>(sqlite3_column_text(sel, 1));
        int diff           = sqlite3_column_int(sel, 2);
        int score_val      = sqlite3_column_int(sel, 3);
        int good           = sqlite3_column_int(sel, 4);
        int ok             = sqlite3_column_int(sel, 5);
        int bad            = sqlite3_column_int(sel, 6);
        int drumroll       = sqlite3_column_int(sel, 7);
        int combo          = sqlite3_column_int(sel, 8);
        int crown_val      = sqlite3_column_int(sel, 9);

        if (!en_raw || diff < 0 || diff > 4) {
            spdlog::warn("py_taiko_import: skipping row with null en_name or invalid diff {}", diff);
            skipped++;
            continue;
        }

        std::string en = en_raw;
        std::string ja = ja_raw ? ja_raw : "";

        auto it = name_to_hashes.find({en, ja});
        if (it == name_to_hashes.end()) {
            spdlog::warn("py_taiko_import: no hash match found for '{}' / '{}', skipping", en, ja);
            skipped++;
            continue;
        }

        const std::string& new_hash = it->second[diff];
        if (new_hash.empty()) {
            spdlog::warn("py_taiko_import: new hash empty for '{}' diff {}, skipping", en, diff);
            skipped++;
            continue;
        }

        // Duplicate check
        bool exists = false;
        int existing_crown = 0, existing_score = 0;
        {
            sqlite3_stmt* check_stmt;
            std::string check_query =
                "SELECT crown, score FROM scores WHERE player_id = '" + player_1
                + "' AND hash = ? AND difficulty = ? "
                  "ORDER BY crown DESC, score DESC LIMIT 1;";
            if (sqlite3_prepare_v2(db_fsd, check_query.c_str(), -1, &check_stmt, nullptr) != SQLITE_OK) {
                spdlog::warn("py_taiko_import: failed to prepare check statement, skipping");
                skipped++;
                continue;
            }
            sqlite3_bind_text(check_stmt, 1, new_hash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(check_stmt,  2, diff);
            if (sqlite3_step(check_stmt) == SQLITE_ROW) {
                exists = true;
                existing_crown = sqlite3_column_int(check_stmt, 0);
                existing_score = sqlite3_column_int(check_stmt, 1);
            }
            sqlite3_finalize(check_stmt);
        }

        bool legacy_is_better = crown_val > existing_crown ||
            (crown_val == existing_crown && score_val > existing_score);

        if (exists && !legacy_is_better) {
            spdlog::debug("py_taiko_import: existing score for '{}' diff {} is not worse, skipping", en, diff);
            skipped++;
            continue;
        }

        // Insert or update score
        {
            sqlite3_stmt* ins_stmt;
            std::string ins_query = exists ?
                "UPDATE scores SET score = ?, good = ?, ok = ?, bad = ?, drumroll = ?, max_combo = ?, crown = ? "
                "WHERE player_id = '" + player_1 + "' AND hash = ? AND difficulty = ?;" :
                "INSERT INTO scores "
                "(player_id, hash, difficulty, score, good, ok, bad, drumroll, max_combo, crown, rank) "
                "VALUES ('" + player_1 + "', ?, ?, ?, ?, ?, ?, ?, ?, ?, 0);";
            if (sqlite3_prepare_v2(db_fsd, ins_query.c_str(), -1, &ins_stmt, nullptr) != SQLITE_OK) {
                spdlog::warn("py_taiko_import: failed to prepare {} statement, skipping", exists ? "update" : "insert");
                skipped++;
                continue;
            }
            if (exists) {
                sqlite3_bind_int (ins_stmt, 1, score_val);
                sqlite3_bind_int (ins_stmt, 2, good);
                sqlite3_bind_int (ins_stmt, 3, ok);
                sqlite3_bind_int (ins_stmt, 4, bad);
                sqlite3_bind_int (ins_stmt, 5, drumroll);
                sqlite3_bind_int (ins_stmt, 6, combo);
                sqlite3_bind_int (ins_stmt, 7, crown_val);
                sqlite3_bind_text(ins_stmt, 8, new_hash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (ins_stmt, 9, diff);
            } else {
                sqlite3_bind_text(ins_stmt, 1, new_hash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(ins_stmt,  2, diff);
                sqlite3_bind_int(ins_stmt,  3, score_val);
                sqlite3_bind_int(ins_stmt,  4, good);
                sqlite3_bind_int(ins_stmt,  5, ok);
                sqlite3_bind_int(ins_stmt,  6, bad);
                sqlite3_bind_int(ins_stmt,  7, drumroll);
                sqlite3_bind_int(ins_stmt,  8, combo);
                sqlite3_bind_int(ins_stmt,  9, crown_val);
            }
            if (sqlite3_step(ins_stmt) == SQLITE_DONE) {
                imported++;
            } else {
                spdlog::warn("py_taiko_import: failed to {} score for '{}' diff {}: {}",
                              exists ? "update" : "insert", en, diff, sqlite3_errmsg(db_fsd));
                skipped++;
            }
            sqlite3_finalize(ins_stmt);
        }
    }

    sqlite3_exec(db_fsd, "COMMIT;", nullptr, nullptr, nullptr);

    sqlite3_finalize(sel);
    sqlite3_close(old_db);
    spdlog::info("py_taiko_import: done — {} imported, {} skipped", imported, skipped);

    if (imported > 0) load_score_cache();
}

void ScoresManager::export_to_hiroba(const std::string& access_code, const std::string& player_id) {
    sqlite3_stmt* stmt;
    const char* query =
        "SELECT hash, difficulty, crown, rank, score, good, ok, bad, drumroll, max_combo, played_at, modifiers "
        "FROM scores WHERE player_id = ? AND hash IS NOT NULL AND hash != '';";
    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("export_to_hiroba: failed to prepare statement: {}", sqlite3_errmsg(db_fsd));
        return;
    }
    sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* hash_text = sqlite3_column_text(stmt, 0);
        std::string hash = hash_text ? reinterpret_cast<const char*>(hash_text) : "";
        if (hash.empty()) continue;

        Score s;
        int difficulty = sqlite3_column_int(stmt, 1);
        s.crown     = static_cast<Crown>(sqlite3_column_int(stmt, 2));
        s.rank      = static_cast<Rank>(sqlite3_column_int(stmt, 3));
        s.score     = sqlite3_column_int(stmt, 4);
        s.good      = sqlite3_column_int(stmt, 5);
        s.ok        = sqlite3_column_int(stmt, 6);
        s.bad       = sqlite3_column_int(stmt, 7);
        s.drumroll  = sqlite3_column_int(stmt, 8);
        s.max_combo = sqlite3_column_int(stmt, 9);
        int64_t played_at = sqlite3_column_int64(stmt, 10);
        const unsigned char* modifiers_text = sqlite3_column_text(stmt, 11);
        std::string modifiers_json = modifiers_text ? reinterpret_cast<const char*>(modifiers_text) : "{}";

        std::map<double, InputLogType> input_log;
        network.submit_score(hash, difficulty, access_code, s, input_log, played_at, modifiers_json, false, -1);
        count++;
    }
    sqlite3_finalize(stmt);
    spdlog::info("export_to_hiroba: submitted {} scores", count);
}

int ScoresManager::apply_remote_scores(const std::vector<RemoteScore>& scores, const std::string& player_id) {
    int updated = 0;
    for (const RemoteScore& rs : scores) {
        auto local = get_score(rs.hash, rs.difficulty, player_id);
        bool remote_is_better = !local ||
            rs.score.crown > local->crown ||
            (rs.score.crown == local->crown && rs.score.score > local->score);
        if (remote_is_better) {
            save_score(rs.hash, rs.difficulty, player_id, rs.score, unix_now(), "{}");
            updated++;
        }
    }
    spdlog::info("sync_from_server: updated {} scores from hiroba", updated);
    return updated;
}

std::optional<Score> ScoresManager::get_score(const std::string& hash, int difficulty, const std::string& player_id) {
    std::lock_guard<std::mutex> lock(maps_mutex);
    auto it = score_cache.find(std::make_tuple(hash, difficulty, player_id));
    if (it != score_cache.end()) return it->second;
    return std::nullopt;
}

Score ScoresManager::save_score(const std::string& hash, int difficulty, const std::string& player_id, Score score, int64_t played_at, const std::string& modifiers_json) {
    sqlite3_stmt* stmt;

    char query[512];
    snprintf(query, sizeof(query),
        "INSERT INTO scores (player_id, hash, difficulty, score, good, ok, bad, drumroll, max_combo, crown, rank, played_at, modifiers) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("save_score: failed to prepare statement: {}", sqlite3_errmsg(db_fsd));
        return score;
    }

    sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, nullptr);
    sqlite3_bind_int(stmt, 3, difficulty);
    sqlite3_bind_int(stmt, 4, score.score);
    sqlite3_bind_int(stmt, 5, score.good);
    sqlite3_bind_int(stmt, 6, score.ok);
    sqlite3_bind_int(stmt, 7, score.bad);
    sqlite3_bind_int(stmt, 8, score.drumroll);
    sqlite3_bind_int(stmt, 9, score.max_combo);
    sqlite3_bind_int(stmt, 10, static_cast<int>(score.crown));
    sqlite3_bind_int(stmt, 11, static_cast<int>(score.rank));
    sqlite3_bind_int64(stmt, 12, played_at);
    sqlite3_bind_text(stmt, 13, modifiers_json.c_str(), -1, nullptr);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        spdlog::error("save_score: failed to insert score for hash {}: {}", hash, sqlite3_errmsg(db_fsd));
    sqlite3_finalize(stmt);
    spdlog::info("Saved score for hash: {} score: {} crown: {}", hash, score.score, (int)score.crown);

    auto key = std::make_tuple(hash, difficulty, player_id);
    std::lock_guard<std::mutex> lock(maps_mutex);
    auto it = score_cache.find(key);
    bool is_better = it == score_cache.end() ||
        score.crown > it->second.crown ||
        (score.crown == it->second.crown && score.score > it->second.score);
    if (is_better) score_cache[key] = score;

    return score;
}

void ScoresManager::add_path_binding(const fs::path& path, const std::array<std::string, 5>& hashes) {
    std::lock_guard<std::mutex> lock(maps_mutex);
    path_to_hashes[path] = hashes;
    std::string single = std::accumulate(hashes.begin(), hashes.end(), std::string{});
    if (!single.empty()) single_hash_to_path[single] = path;
    for (const std::string& hash : hashes) {
        if (!hash.empty()) diff_hash_to_path[hash] = path;
    }
}

std::optional<fs::path> ScoresManager::get_path_by_hash(const std::string& single_hash) {
    if (single_hash.empty()) return std::nullopt;
    std::lock_guard<std::mutex> lock(maps_mutex);
    auto it = single_hash_to_path.find(single_hash);
    if (it != single_hash_to_path.end()) return it->second;
    return std::nullopt;
}

std::optional<fs::path> ScoresManager::get_path_by_diff_hash(const std::string& diff_hash) {
    std::lock_guard<std::mutex> lock(maps_mutex);
    auto it = diff_hash_to_path.find(diff_hash);
    if (it != diff_hash_to_path.end()) return it->second;
    return std::nullopt;
}

std::array<std::string, 5> ScoresManager::get_hashes(const fs::path& path) {
    std::lock_guard<std::mutex> lock(maps_mutex);
    auto it = path_to_hashes.find(path);
    return it != path_to_hashes.end() ? it->second : std::array<std::string, 5>{};
}

std::string ScoresManager::get_single_hash(const fs::path& path) {
    std::lock_guard<std::mutex> lock(maps_mutex);
    auto it = path_to_hashes.find(path);
    if (it == path_to_hashes.end()) return "";
    return std::accumulate(it->second.begin(), it->second.end(), std::string{});
}

static void ensure_song_cache_table(sqlite3* db) {
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS song_cache ("
        "path TEXT PRIMARY KEY, version INTEGER NOT NULL, mtime INTEGER NOT NULL, size INTEGER NOT NULL, "
        "hash_0 TEXT, hash_1 TEXT, hash_2 TEXT, hash_3 TEXT, hash_4 TEXT, title TEXT, subtitle TEXT, "
        "titles TEXT, subtitles TEXT, levels TEXT);",
        nullptr, nullptr, nullptr);
    // tables made by version 1 lack the metadata columns (their rows are re-parsed anyway)
    static bool columns_checked = false;
    if (columns_checked) return;
    columns_checked = true;
    for (const char* col : {"titles", "subtitles", "levels"}) {
        std::string sql = std::string("ALTER TABLE song_cache ADD COLUMN ") + col + " TEXT;";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);   // fails harmlessly if present
    }
}

// language/text pairs as "lang \x1f text \x1e ..." (control characters no title uses)
static std::string encode_pairs(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out;
    for (const auto& [k, v] : pairs) {
        if (!out.empty()) out += '\x1e';
        out += k;
        out += '\x1f';
        out += v;
    }
    return out;
}

static std::vector<std::pair<std::string, std::string>> decode_pairs(const std::string& s) {
    std::vector<std::pair<std::string, std::string>> out;
    if (s.empty()) return out;
    size_t start = 0;
    for (;;) {
        size_t end = s.find('\x1e', start);
        std::string item = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t sep = item.find('\x1f');
        if (sep != std::string::npos) out.emplace_back(item.substr(0, sep), item.substr(sep + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

static std::string encode_levels(const std::array<int, 5>& lv) {
    std::string out;
    for (int i = 0; i < 5; i++) out += (i ? "," : "") + std::to_string(lv[i]);
    return out;
}

static std::array<int, 5> decode_levels(const std::string& s) {
    std::array<int, 5> lv = {-1, -1, -1, -1, -1};
    size_t start = 0;
    for (int i = 0; i < 5 && start <= s.size(); i++) {
        size_t end = s.find(',', start);
        try { lv[i] = std::stoi(s.substr(start, end == std::string::npos ? std::string::npos : end - start)); }
        catch (...) { break; }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lv;
}

void ScoresManager::set_song_meta(const std::string& path, const SongCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(song_meta_mutex);
    song_meta[path] = entry;
}

std::optional<SongCacheEntry> ScoresManager::get_song_meta(const std::string& path) {
    std::lock_guard<std::mutex> lock(song_meta_mutex);
    auto it = song_meta.find(path);
    if (it == song_meta.end()) return std::nullopt;
    return it->second;
}

std::unordered_map<std::string, SongCacheEntry> ScoresManager::load_song_cache() {
    std::unordered_map<std::string, SongCacheEntry> cache;
    ensure_song_cache_table(db_fsd);
    sqlite3_stmt* stmt;
    const char* query =
        "SELECT path, mtime, size, hash_0, hash_1, hash_2, hash_3, hash_4, title, subtitle, "
        "titles, subtitles, levels FROM song_cache WHERE version = ?;";
    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("load_song_cache: failed to prepare statement: {}", sqlite3_errmsg(db_fsd));
        return cache;
    }
    sqlite3_bind_int(stmt, 1, SONG_CACHE_VERSION);
    auto text = [&](int col) {
        const unsigned char* t = sqlite3_column_text(stmt, col);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string{};
    };
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SongCacheEntry e;
        e.mtime = sqlite3_column_int64(stmt, 1);
        e.size  = sqlite3_column_int64(stmt, 2);
        for (int i = 0; i < 5; i++) e.hashes[i] = text(3 + i);
        e.title     = text(8);
        e.subtitle  = text(9);
        e.titles    = decode_pairs(text(10));
        e.subtitles = decode_pairs(text(11));
        e.levels    = decode_levels(text(12));
        cache.emplace(text(0), std::move(e));
    }
    sqlite3_finalize(stmt);
    return cache;
}

void ScoresManager::store_song_cache(const std::string& path, const SongCacheEntry& entry) {
    ensure_song_cache_table(db_fsd);
    sqlite3_stmt* stmt;
    const char* query =
        "INSERT OR REPLACE INTO song_cache "
        "(path, version, mtime, size, hash_0, hash_1, hash_2, hash_3, hash_4, title, subtitle, "
        "titles, subtitles, levels) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("store_song_cache: failed to prepare statement: {}", sqlite3_errmsg(db_fsd));
        return;
    }
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, SONG_CACHE_VERSION);
    sqlite3_bind_int64(stmt, 3, entry.mtime);
    sqlite3_bind_int64(stmt, 4, entry.size);
    for (int i = 0; i < 5; i++) sqlite3_bind_text(stmt, 5 + i, entry.hashes[i].c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, entry.title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, entry.subtitle.c_str(), -1, SQLITE_STATIC);
    const std::string titles = encode_pairs(entry.titles), subtitles = encode_pairs(entry.subtitles);
    const std::string levels = encode_levels(entry.levels);
    sqlite3_bind_text(stmt, 12, titles.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 13, subtitles.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 14, levels.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void ScoresManager::remove_song_cache(const std::string& path) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_fsd, "DELETE FROM song_cache WHERE path = ?;", -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void ScoresManager::add_song(const std::array<std::string, 5>& hashes, const std::string& title, const std::string& subtitle) {
    // prepared once: the loading screen calls this for every chart in the library
    if (!add_song_stmt) {
        const char* query =
            "INSERT OR IGNORE INTO songs (title, subtitle, hash_0, hash_1, hash_2, hash_3, hash_4) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);";
        if (sqlite3_prepare_v2(db_fsd, query, -1, &add_song_stmt, nullptr) != SQLITE_OK) {
            spdlog::error("add_song: failed to prepare statement: {}", sqlite3_errmsg(db_fsd));
            add_song_stmt = nullptr;
            return;
        }
    }
    sqlite3_stmt* stmt = add_song_stmt;
    sqlite3_bind_text(stmt, 1, title.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, subtitle.c_str(), -1, SQLITE_STATIC);
    for (int i = 0; i < 5; i++) {
        sqlite3_bind_text(stmt, 3 + i, hashes[i].c_str(), -1, SQLITE_STATIC);
    }
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
}

void ScoresManager::remap_hashes(const std::unordered_map<std::string, std::string>& old_to_new) {
    if (old_to_new.empty()) return;

    sqlite3_exec(db_fsd, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    sqlite3_stmt* scores_stmt;
    if (sqlite3_prepare_v2(db_fsd,
            "UPDATE scores SET hash = ? WHERE hash = ?;",
            -1, &scores_stmt, nullptr) != SQLITE_OK) {
        spdlog::error("remap_hashes: failed to prepare scores statement: {}", sqlite3_errmsg(db_fsd));
        sqlite3_exec(db_fsd, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }

    std::array<sqlite3_stmt*, 5> song_stmts;
    for (int i = 0; i < 5; i++) {
        std::string q = "UPDATE songs SET hash_" + std::to_string(i)
                      + " = ? WHERE hash_" + std::to_string(i) + " = ?;";
        if (sqlite3_prepare_v2(db_fsd, q.c_str(), -1, &song_stmts[i], nullptr) != SQLITE_OK) {
            spdlog::error("remap_hashes: failed to prepare song statement {}: {}", i, sqlite3_errmsg(db_fsd));
            sqlite3_finalize(scores_stmt);
            for (int j = 0; j < i; j++) sqlite3_finalize(song_stmts[j]);
            sqlite3_exec(db_fsd, "ROLLBACK;", nullptr, nullptr, nullptr);
            return;
        }
    }

    for (const auto& [old_h, new_h] : old_to_new) {
        sqlite3_bind_text(scores_stmt, 1, new_h.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(scores_stmt, 2, old_h.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(scores_stmt);
        sqlite3_reset(scores_stmt);

        for (int i = 0; i < 5; i++) {
            sqlite3_bind_text(song_stmts[i], 1, new_h.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(song_stmts[i], 2, old_h.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(song_stmts[i]);
            sqlite3_reset(song_stmts[i]);
        }
    }

    sqlite3_finalize(scores_stmt);
    for (auto& s : song_stmts) sqlite3_finalize(s);

    sqlite3_exec(db_fsd, "COMMIT;", nullptr, nullptr, nullptr);
    spdlog::info("remap_hashes: remapped {} hash pairs", old_to_new.size());

    load_score_cache();
}

std::optional<PlayerData> ScoresManager::get_player_data(const std::string& player_id) {
    sqlite3_stmt* stmt;
    const char* query =
        "SELECT player_id, username, title, title_bg, dan, gold, rainbow,"
        " modifier_auto, modifier_speed, modifier_display, modifier_inverse, modifier_random,"
        " neiro_index, chara_color_1, chara_color_2, chara_color_3,"
        " chara_head_index, chara_body_index, chara_cos_index, chara_is_costume,"
        " chara_paint_index, chara_face_index, chara_acce_index, modifier_skip"
        " FROM players WHERE player_id = ?;";

    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("get_player: failed to prepare: {}", sqlite3_errmsg(db_fsd));
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);

        // Player not found — insert a default row with the requested player_id
        sqlite3_stmt* ins;
        const char* ins_query =
            "INSERT OR IGNORE INTO players (player_id, username, title, title_bg, chara_color_1, chara_color_2) "
            "VALUES (?, ?, 'Donder Debut!', CASE WHEN ?1 = '1' THEN 2 ELSE 0 END, "
            "CASE WHEN ?1 = '1' THEN '#F94728' ELSE '#68BFC0' END, CASE WHEN ?1 = '1' THEN '#68BFC0' ELSE '#F94728' END);";
        if (sqlite3_prepare_v2(db_fsd, ins_query, -1, &ins, nullptr) == SQLITE_OK) {
            std::string default_name = player_id == "0" ? "Don-chan" : player_id == "1" ? "Kat-chan" : "Player " + player_id;
            // username is UNIQUE: a migrated account may already own "Don-chan"/"Kat-chan", so suffix on collision
            for (int n = 1; n <= 50; n++) {
                std::string name = n == 1 ? default_name : default_name + " " + std::to_string(n);
                sqlite3_reset(ins);
                sqlite3_bind_text(ins, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins, 2, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(ins);
                if (sqlite3_changes(db_fsd) > 0) break;
            }
            sqlite3_finalize(ins);
        } else {
            spdlog::error("get_player: failed to insert default player {}: {}", player_id, sqlite3_errmsg(db_fsd));
            return std::nullopt;
        }

        // Re-query the newly inserted row
        if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
            spdlog::error("get_player: failed to re-prepare after insert: {}", sqlite3_errmsg(db_fsd));
            return std::nullopt;
        }
        sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }
    }

    auto col_str = [&](int i) -> std::string {
        auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
        return p ? p : "";
    };

    PlayerData p;
    p.player_id       = col_str(0);
    p.username        = col_str(1);
    p.title           = col_str(2);
    p.title_bg        = sqlite3_column_int(stmt, 3);
    p.dan             = sqlite3_column_int(stmt, 4);
    p.gold            = sqlite3_column_int(stmt, 5);
    p.rainbow         = sqlite3_column_int(stmt, 6);
    p.modifier_auto   = sqlite3_column_int(stmt, 7);
    p.modifier_speed  = sqlite3_column_int(stmt, 8);
    p.modifier_display  = sqlite3_column_int(stmt, 9);
    p.modifier_inverse  = sqlite3_column_int(stmt, 10);
    p.modifier_random   = sqlite3_column_int(stmt, 11);
    p.neiro_index       = sqlite3_column_int(stmt, 12);
    p.chara_color_1     = parse_hex_color(col_str(13));
    p.chara_color_2     = parse_hex_color(col_str(14));
    p.chara_color_3     = parse_hex_color(col_str(15));
    p.chara_head_index  = sqlite3_column_int(stmt, 16);
    p.chara_body_index  = sqlite3_column_int(stmt, 17);
    p.chara_cos_index   = sqlite3_column_int(stmt, 18);
    p.chara_is_costume  = sqlite3_column_int(stmt, 19);
    p.chara_paint_index = sqlite3_column_int(stmt, 20);
    p.chara_face_index  = sqlite3_column_int(stmt, 21);
    p.chara_acce_index  = sqlite3_column_int(stmt, 22);
    p.modifier_skip     = sqlite3_column_int(stmt, 23);

    sqlite3_finalize(stmt);
    return p;
}

void ScoresManager::save_player_data(const PlayerData& player) {
    sqlite3_stmt* stmt;
    const char* query =
        "UPDATE players SET"
        " username = ?, title = ?, title_bg = ?, dan = ?, gold = ?, rainbow = ?,"
        " modifier_auto = ?, modifier_speed = ?, modifier_display = ?, modifier_inverse = ?, modifier_random = ?,"
        " neiro_index = ?, chara_color_1 = ?, chara_color_2 = ?, chara_color_3 = ?,"
        " chara_head_index = ?, chara_body_index = ?, chara_cos_index = ?, chara_is_costume = ?,"
        " chara_paint_index = ?, chara_face_index = ?, chara_acce_index = ?, modifier_skip = ?"
        " WHERE player_id = ?;";

    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("save_player: failed to prepare: {}", sqlite3_errmsg(db_fsd));
        return;
    }

    std::string c1 = color_to_hex(player.chara_color_1);
    std::string c2 = color_to_hex(player.chara_color_2);
    std::string c3 = color_to_hex(player.chara_color_3);

    sqlite3_bind_text(stmt,  1, player.username.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt,  2, player.title.c_str(),     -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt,  3, player.title_bg);
    sqlite3_bind_int (stmt,  4, player.dan);
    sqlite3_bind_int (stmt,  5, player.gold);
    sqlite3_bind_int (stmt,  6, player.rainbow);
    sqlite3_bind_int (stmt,  7, player.modifier_auto);
    sqlite3_bind_int (stmt,  8, player.modifier_speed);
    sqlite3_bind_int (stmt,  9, player.modifier_display);
    sqlite3_bind_int (stmt, 10, player.modifier_inverse);
    sqlite3_bind_int (stmt, 11, player.modifier_random);
    sqlite3_bind_int (stmt, 12, player.neiro_index);
    sqlite3_bind_text(stmt, 13, c1.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 14, c2.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 15, c3.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 16, player.chara_head_index);
    sqlite3_bind_int (stmt, 17, player.chara_body_index);
    sqlite3_bind_int (stmt, 18, player.chara_cos_index);
    sqlite3_bind_int (stmt, 19, player.chara_is_costume);
    sqlite3_bind_int (stmt, 20, player.chara_paint_index);
    sqlite3_bind_int (stmt, 21, player.chara_face_index);
    sqlite3_bind_int (stmt, 22, player.chara_acce_index);
    sqlite3_bind_int (stmt, 23, player.modifier_skip);
    sqlite3_bind_text(stmt, 24, player.player_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE)
        spdlog::error("save_player: failed to update player {}: {}", player.player_id, sqlite3_errmsg(db_fsd));

    sqlite3_finalize(stmt);
}

static void ensure_dan_table(sqlite3* db) {
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS dan_results"
        "(player_id TEXT NOT NULL,"
        "course TEXT NOT NULL,"
        "dan_index INTEGER NOT NULL DEFAULT -1,"
        "rank INTEGER NOT NULL DEFAULT 0,"
        "score INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(player_id, course));",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "ALTER TABLE dan_results ADD COLUMN arrival INTEGER NOT NULL DEFAULT 0;",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "ALTER TABLE dan_results ADD COLUMN exam_bests TEXT NOT NULL DEFAULT '';",
        nullptr, nullptr, nullptr);
}

// exam_bests column: one "type:value,s1,s2,s3" per exam, joined by ';' (-1 = none)
static std::string encode_exam_bests(const std::vector<DanExamBest>& bests) {
    std::string out;
    for (size_t i = 0; i < bests.size(); i++) {
        const DanExamBest& b = bests[i];
        if (i) out += ';';
        out += b.type + ':' + std::to_string(b.value);
        for (int v : b.song) out += ',' + std::to_string(v);
    }
    return out;
}

static std::vector<DanExamBest> decode_exam_bests(const std::string& s) {
    std::vector<DanExamBest> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string::npos) end = s.size();
        const std::string item = s.substr(start, end - start);
        start = end + 1;
        DanExamBest b;
        size_t colon = item.find(':');
        if (colon == std::string::npos) { out.push_back(b); continue; }
        b.type = item.substr(0, colon);
        int vals[4] = {-1, -1, -1, -1};
        size_t pos = colon + 1;
        for (int k = 0; k < 4 && pos <= item.size(); k++) {
            size_t comma = item.find(',', pos);
            if (comma == std::string::npos) comma = item.size();
            try { vals[k] = std::stoi(item.substr(pos, comma - pos)); } catch (...) { vals[k] = -1; }
            pos = comma + 1;
        }
        b.value = vals[0];
        for (int k = 0; k < 3; k++) b.song[k] = vals[k + 1];
        out.push_back(b);
    }
    return out;
}

std::optional<DanRecord> ScoresManager::get_dan_record(const std::string& player_id, const std::string& course_title) {
    ensure_dan_table(db_fsd);
    sqlite3_stmt* stmt;
    const char* query =
        "SELECT dan_index, rank, score, arrival, exam_bests FROM dan_results WHERE player_id = ? AND course = ?;";
    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("get_dan_record: failed to prepare: {}", sqlite3_errmsg(db_fsd));
        return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, course_title.c_str(), -1, SQLITE_STATIC);
    std::optional<DanRecord> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        DanRecord r;
        r.dan_index = sqlite3_column_int(stmt, 0);
        r.rank      = sqlite3_column_int(stmt, 1);
        r.score     = sqlite3_column_int(stmt, 2);
        r.arrival   = sqlite3_column_int(stmt, 3);
        if (const unsigned char* eb = sqlite3_column_text(stmt, 4))
            r.exam_bests = decode_exam_bests(reinterpret_cast<const char*>(eb));
        out = r;
    }
    sqlite3_finalize(stmt);
    return out;
}

void ScoresManager::save_dan_record(const std::string& player_id, const std::string& course_title, const DanRecord& rec) {
    ensure_dan_table(db_fsd);
    sqlite3_stmt* stmt;
    const char* query =
        "INSERT INTO dan_results (player_id, course, dan_index, rank, score, arrival, exam_bests)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(player_id, course) DO UPDATE SET"
        " dan_index = excluded.dan_index, rank = excluded.rank, score = excluded.score,"
        " arrival = excluded.arrival, exam_bests = excluded.exam_bests;";
    if (sqlite3_prepare_v2(db_fsd, query, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("save_dan_record: failed to prepare: {}", sqlite3_errmsg(db_fsd));
        return;
    }
    sqlite3_bind_text(stmt, 1, player_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, course_title.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 3, rec.dan_index);
    sqlite3_bind_int (stmt, 4, rec.rank);
    sqlite3_bind_int (stmt, 5, rec.score);
    sqlite3_bind_int (stmt, 6, rec.arrival);
    const std::string bests = encode_exam_bests(rec.exam_bests);
    sqlite3_bind_text(stmt, 7, bests.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE)
        spdlog::error("save_dan_record: failed for {}: {}", course_title, sqlite3_errmsg(db_fsd));
    sqlite3_finalize(stmt);
}

void ScoresManager::migrate_player_id(const std::string& old_id, const std::string& new_id) {
    if (old_id == new_id) return;

    spdlog::info("Migrating player ID from {} to {}", old_id, new_id);

    // Check if new_id already exists
    sqlite3_stmt* stmt;
    const char* check_query = "SELECT 1 FROM players WHERE player_id = ?;";
    if (sqlite3_prepare_v2(db_fsd, check_query, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            spdlog::warn("Player ID {} already exists, skipping migration", new_id);
            sqlite3_finalize(stmt);
            return;
        }
        sqlite3_finalize(stmt);
    }

    // Update players table
    const char* update_player = "UPDATE players SET player_id = ? WHERE player_id = ?;";
    if (sqlite3_prepare_v2(db_fsd, update_player, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, old_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Update scores table
    const char* update_scores = "UPDATE scores SET player_id = ? WHERE player_id = ?;";
    if (sqlite3_prepare_v2(db_fsd, update_scores, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, old_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Update dan_results table
    const char* update_dan = "UPDATE dan_results SET player_id = ? WHERE player_id = ?;";
    if (sqlite3_prepare_v2(db_fsd, update_dan, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, new_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, old_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

bool ScoresManager::begin_transaction() {
    if (sqlite3_exec(db_fsd, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        spdlog::error("begin_transaction failed: {}", sqlite3_errmsg(db_fsd));
        return false;
    }
    return true;
}

bool ScoresManager::commit() {
    if (sqlite3_exec(db_fsd, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        spdlog::error("commit failed: {}", sqlite3_errmsg(db_fsd));
        return false;
    }
    return true;
}

void ScoresManager::rollback() {
    if (sqlite3_exec(db_fsd, "ROLLBACK;", nullptr, nullptr, nullptr) != SQLITE_OK)
        spdlog::error("rollback failed: {}", sqlite3_errmsg(db_fsd));
}

ScoresManager* _scores_manager_ptr = nullptr;

void init_scores_manager(bool gen3) {
    delete _scores_manager_ptr;  // calling this more than once would otherwise leak the previous manager
    _scores_manager_ptr = new ScoresManager(gen3 ? "scores_gen3.db" : "scores.db");
}
