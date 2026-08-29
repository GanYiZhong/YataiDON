#pragma once

#include "config.h"
#include "ray.h"
#include "parsers/tja.h"

namespace fs = std::filesystem;

namespace ScoreMethod {
    const std::string GEN3 = "gen3";
    const std::string SHINUCHI = "shinuchi";
}

enum class Difficulty {
    BACK = -3,
    MODIFIER = -2,
    NEIRO = -1,
    EASY = 0,
    NORMAL = 1,
    HARD = 2,
    ONI = 3,
    URA = 4,
    TOWER = 5,
    DAN = 6
};

enum class Crown {
    NONE = 0,
    CLEAR = 1,
    FC = 2,
    DFC = 3
};

//underscore to avoid conflict with raylib colors
enum class Rank {
    _NONE = 0,
    _WHITE = 1,
    _BRONZE = 2,
    _SILVER = 3,
    _GOLD = 4,
    _PINK = 5,
    _PURPLE = 6,
    _RAINBOW = 7
};

struct Exam {
    std::string type;   // "gauge","combo","judgebad","judgegood","judgeperfect","hit","score"
    int red = 0;
    int gold = 0;
    std::string range;  // "less" or "more"

    // ROUND 19. The cabinet's condition rows come in two shapes, and which one a
    // row uses is COURSE DATA, not a property of the condition type: 39.06
    // `script_lua/dani_select/DaniData.lua:104` reads
    // `daniInfoData.borders[idx].is_gothrough` into `theme_is_continuous`, and
    // `dani_select_theme_disp_data.lua:129` switches the row art on it --
    //   is_gothrough == true  -> one full-width bar, label frame `song_total`
    //                            (the gold tomoe badge); the value is the sum
    //                            over all three songs
    //   is_gothrough == false -> three per-song cells, labels `song_no_1/2/3`
    //                            (the green/blue/pink 1st/2nd/3rd chips)
    // The same split drives `dani_enso_detail.nulm` sprite 239's `all` / `01`
    // labels in play and `DaniResult.lua`'s g_odaiType_ 0 / 3 on the result.
    //
    // 39.06 does NOT ship the per-course values: `Cabinet.DaniInfo(level)` is
    // served by the cabinet/ALL.Net, and there is no dani table under
    // `Data/x64/datatable`. So this is a dan.json field ("gothrough") and the
    // default is `true` -- which is exactly what this engine already measures
    // (Player counters run cumulatively across the whole course).
    bool gothrough = true;
};

struct DanSongEntry {
    fs::path song_path;
    int genre_index = 0;
    int difficulty = 0;
    int level = 0;
    // ROUND 57 (r57-dani-leftovers): dan.json chart-level optional
    // `"hidden": true` = the arcade's per-song `is_hiddens[idx]`
    // (Cabinet.DaniInfo / GaidenDaniInfo): the song is shown as ？？？？？？ on
    // the DAN_SELECT board until the player has REACHED it (dani_select
    // DaniData.SetDaniBestScore: song_isOpen[idx] = true for
    // idx <= arrival_song_cnt once clear_level > kNoPlay). Absent -> false.
    bool hidden = false;
};

struct DanResultSong {
    int selected_difficulty = 0;
    int diff_level = 0;
    std::string song_title = "default_title";
    int genre_index = 0;
    int good = 0;
    int ok = 0;
    int bad = 0;
    int drumroll = 0;
    // ROUND 57: the course entry's `hidden` flag carried onto the result row —
    // an UNREACHED hidden song whose stored best arrival never got there
    // either is masked ？？？？？？ on page 1 (DaniResultSongBase.SetUp:
    // `unreach and g_arrivalSongCnt_ < songNum+1 and isHidden -> "hidden"`).
    bool hidden = false;
    // ROUND 47 -- the cabinet's g_unreach_[idx] (DaniResult.lua SetUnReach): a
    // song the fail-out never reached carries ZERO judgements and an unreach
    // flag, NOT an all-notes-BAD fabrication. DAN_RESULT dims such a row.
    bool unreached = false;
};

struct DanResultExam {
    float progress = 0.0f;
    int counter_value = 0;
    std::string bar_texture = "exam_red";
    bool failed = false;
    // ROUND 19: per-song breakdown, only meaningful when the exam is NOT
    // `gothrough` (the cabinet's `01` row with its 1st/2nd/3rd sub-bars).
    int   song_value[3]    = {0, 0, 0};
    float song_progress[3] = {0.0f, 0.0f, 0.0f};
    int   song_count       = 0;
    // ROUND 47 -- the arcade's per-exam verdict (DaniResult.lua
    // CheckResultDetail return value): 0 = fail, 1 = red pass (>= border /
    // < border), 2 = gold pass (>= gold border / < gold border). A per-song
    // exam takes the arcade's all-three rule: all three songs gold -> 2, all
    // three at least red -> 1, else 0.
    int tier = 0;
};

struct DanResultData {
    int dan_color = 0;
    // ROUND 19: which of the 27 `rank_plate` frames this course's plaque is.
    // -1 = no plaque art declared, keep the old colour plate + text title.
    int dan_rank = -1;
    // ROUND 50 (r50-dani-visual-completion): the course's nameplate-dan-chip
    // index (dan.json "dan_index", 0..24 into the 25-frame nameplate
    // `dan_emblem` sheet; falls back to the course directory's numeric prefix,
    // see dan_select.cpp). -1 = unmapped -> no nameplate update / no
    // persistence keyed rank chip, celebration text suppressed.
    int dan_index = -1;
    // ROUND 57 (r57-dani-leftovers): the HIGHEST dan_index in the loaded
    // course library (DAN_SELECT computes it) = the arcade's g_maxDaniNum_.
    // The congrats popup (dani_result_congrats) triggers only on the first
    // pass of THAT course (DaniResult.lua:327). -1 = unknown -> never.
    int dan_index_max = -1;
    // ROUND 57: dan.json course-level optional `"gaiden": true` =
    // Cabinet.GaidenDaniInfo. A gaiden pass never sets g_isShodan_ (no 昇段
    // celebration), never updates the nameplate dan chip, and never fires the
    // congrats popup (DaniResult.lua:318 `and g_isGaiden_ == false`); its
    // best result still persists in dan_results (the engine's course-title
    // keying doubles as the cabinet's separate gaiden_rank[] store).
    bool is_gaiden = false;
    std::string dan_title = "default_title";
    int score = 0;
    float gauge_length = 0.0f;
    int max_combo = 0;
    std::vector<DanResultSong> songs;
    std::vector<Exam> exams;
    std::vector<DanResultExam> exam_data;
    // ROUND 47 -- the arcade's overall course verdict, DaniResult.lua
    // SetOdaiResult's g_odaiResult_ 0..6:
    //   0 fail | 1 red | 2 red+FC (no 不可) | 3 red+全良 (no 可 no 不可)
    //   | 4 gold | 5 gold+FC | 6 gold+全良
    // -1 = not computed (a result produced by an older build): DAN_RESULT then
    // falls back to its pre-R47 any_failed/all_gold logic.
    int odai_result = -1;
};

struct ResultData {
    int score = 0;
    int good = 0;
    int ok = 0;
    int bad = 0;
    int max_combo = 0;
    int total_drumroll = 0;
    float gauge_length = 0.0f;
    int prev_score = 0;
};

struct SessionData {
    fs::path selected_song;
    std::string song_hash;
    std::vector<DanSongEntry> selected_dan;
    std::vector<Exam> selected_dan_exam;
    int dan_color = 0;
    int dan_rank = -1;          // ROUND 19: dan.json "rank_art", see DanResultData
    int dan_index = -1;         // ROUND 50: dan.json "dan_index", see DanResultData
    int dan_index_max = -1;     // ROUND 57: library max dan_index, see DanResultData
    bool dan_gaiden = false;    // ROUND 57: dan.json "gaiden", see DanResultData
    int selected_difficulty = 0;
    std::string song_title = "default_title";
    std::string song_subtitle = "default_subtitle";
    int genre_index = 0;
    ResultData result_data;
    DanResultData dan_result_data;
};

struct CameraConfig {
    ray::Vector2 offset = {0.0f, 0.0f};
    float zoom = 1.0f;
    float h_scale = 1.0f;
    float v_scale = 1.0f;
    float rotation = 0.0f;
    ray::Color border_color = ray::BLACK;
};

struct GlobalData {
    int songs_played = 0;
    // Name of the screen currently running (screens_to_string), so skin
    // scripts shared across screens can tell where they are drawn.
    std::string current_screen = "LOADING";
    // True only while a screen re-draws its coin overlay on top of the song-loading
    // (rainbow) transition; exposed to Lua as tex.in_transition() so skins can draw a
    // reduced overlay (arcade shows only the credit text over the loading screen).
    bool in_transition = false;
    // Which phase of the TITLE attract loop is running
    // ("OP_VIDEO" / "WARNING" / "ATTRACT_VIDEO" / "ATTRACT_CAMERA"), and the
    // engine-ms at which that phase started. TITLE is one Screens value but four
    // visually distinct arcade scenes, so this is the only way an automated
    // fidelity check can tell them apart or time them. Empty off the title screen.
    std::string title_state = "";
    double      title_state_start_ms = 0.0;
    // LIVE gameplay counters for player 1, written once per frame by
    // Player::update. `session_data[..].result_data` is only filled when the
    // song ends, so before this there was no way for an automated fidelity
    // check to capture "the frame where the combo is 9" or "a frame inside a
    // gogo section" — it had to guess with wall-clock sleeps, which the
    // 1-3 s chart-load jitter made unreliable. Reset to 0 by GameScreen.
    // Read-only data-out; nothing in the game reads these back.
    int  live_combo = 0;
    int  live_score = 0;
    int  live_drumroll = 0;
    bool live_gogo = false;
    // ROUND 52 (r52-lua-divergence-fixes): live soul-gauge data-out for the
    // automation `state` command, same read-only contract as the counters
    // above -- lets a fever/norma-edge test drive adaptively instead of
    // guessing with wall-clock phases.
    double live_soul = 0.0;
    bool live_is_clear = false;
    bool live_is_rainbow = false;
    // 演奏スキップ, same contract: the live alternating-rim count (0..10) and
    // whether the skip has fired. -1 = the feature is not armed for this enso.
    int  live_skip_count = -1;
    bool live_skip_used = false;
    // `--auto` on the command line. The auto-play modifier otherwise comes only
    // from the player's save data (player_data_to_modifiers), so an automated
    // fidelity run had no way to force it and silently played a whole chart with
    // no hits when someone changed that saved setting. Off unless the flag is given.
    bool force_auto_play = false;
    bool returned_from_result = false;
    // ROUND 15 - the cabinet's mid-song-select 2P join.
    // `song_select/song_select_all.lua` `SecondPlayerJoinToEntry` (l.1609) sets
    // `nextMode = SceneType.kEntry` when the un-joined seat's drum is hit on the
    // 1st or 2nd song select, and the ENTRY scene it lands in boots with the
    // engine global `StartModeSelect == true` (`entry/entry_main.lua` l.318),
    // which `PlayDataManager.Entry(idx, kCoin)`s every seat still at kNone and
    // goes STRAIGHT to mode select - no second credit-wait screen, and no second
    // press from the newcomer.  These two fields are that `StartModeSelect`:
    // SONG SELECT raises the flag and names the seat that is ALREADY in, ENTRY
    // consumes it once in `on_screen_start` and rebuilds that seat instead of
    // clearing the roster, so `first_login_player` (hence every player_id lookup,
    // nameplate, costume and modifier set) is never rewritten by the late joiner.
    bool entry_join_pending = false;
    PlayerNum entry_joined_seat = PlayerNum::P1;
    CameraConfig camera;
    Config* config = nullptr;  // Using pointer, initialize appropriately
    int total_songs = 0;
    PlayerNum player_num = PlayerNum::P1;
    PlayerNum first_login_player = PlayerNum::P1;
    int input_locked = 0;
    std::vector<SessionData> session_data = std::vector<SessionData>(3);
    // Difficulty each player last confirmed, indexed like session_data;
    // -1 until they pick one. Only used to place the difficulty cursor, so
    // it lives for the run and is deliberately not written to the config.
    // Not part of SessionData: that is reset after every song.
    std::vector<int> last_difficulty = std::vector<int>(3, -1);

    GlobalData() {
        // Initialize vectors with default-constructed elements
        session_data.resize(3);
    }
};

void reset_session();
int get_player_id(PlayerNum player_num);

extern GlobalData global_data;
