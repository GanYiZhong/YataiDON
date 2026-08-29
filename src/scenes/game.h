#pragma once

#include "../libs/screen.h"
#include "../libs/video.h"
#include "../objects/game/player.h"
#include "../objects/game/transition.h"
#include "../objects/game/song_info.h"
#include "../objects/game/result_transition.h"
#include "../objects/global/allnet_indicator.h"
#include <future>

class GameScreen : public Screen {
protected:
    GameScreen(const std::string& name) : Screen(name) {}

public:
    GameScreen() : Screen("game") {
    }

    ray::Shader mask_shader;
    double start_ms;
    double ms_from_start;
    double start_delay;
    double last_resync_ms;
    bool song_started;
    bool paused;
    bool score_saved;
    int pause_time;
    float bpm;

    std::optional<VideoPlayer> movie;
    std::optional<std::string> song_music;
    // Song audio decodes (and possibly resamples) on a worker thread; the
    // synchronous load blocked the main thread for seconds on long songs.
    // update() polls this and fills song_music when the load finishes.
    std::future<std::string> pending_song_load;
    std::optional<SongParser> parser;
    std::string scene_preset;
    std::vector<std::unique_ptr<Player>> players;
    SongInfo song_info;
    std::optional<Transition> transition;
    ResultTransition result_transition;
    AllNetIcon allnet_indicator;
    std::optional<Background> background;

    void on_screen_start() override;

    // ROUND 58 (r58-2p-background-polish): the identity the base
    // on_screen_start() uses when constructing Background/ResultTransition.
    // global_data.player_num is parked on P1 for a 2P session (entry.cpp
    // "join_player parks player_num back on P1"), so Game2PScreen used to let
    // the base build the 1P background -- with a SCENEPRESET chart a full
    // collab rig -- and then re-emplace the TWO_PLAYER one, briefly
    // building+destroying a collab rig (textures included) on every 2P screen
    // init (ROUND 52 found-in-passing). PracticeGameScreen had the same
    // double-construct for its "PRACTICE" preset. Subclasses override these
    // so the base builds the right object ONCE; nothing re-emplaces after.
    // DanGameScreen is unaffected: it skips GameScreen::on_screen_start and
    // builds its own "DAN" background (game_dan.cpp).
    virtual PlayerNum scene_player_num() const { return global_data.player_num; }
    virtual std::string background_scene_preset() const { return scene_preset; }

    virtual Modifiers get_player_modifiers(PlayerNum pn);

    Screens on_screen_end(Screens next_screen) override;

    void load_hitsounds();

    virtual void init_tja(fs::path song);

    void start_song(double ms_from_start);

    void poll_pending_song();

    void restart_song();

    void pause_song();

    void resync_song(double ms_from_start);

    void end_song();

    // --- 演奏スキップ (arcade 演奏オプション row 5) ------------------------------
    // Rule, read out of the CHN05 binary (EnsoInput::Process 0x1400E47D0,
    // EnsoInput::CountEnsoSkipWait 0x1400E51B0, EnsoGameManager 0x14011...) and
    // matching the arcade wordlist text `option_enso_skip_window`:
    // "つかっていない太鼓のフチを左右交互に１０回叩くと演奏を終了できます".
    //   * only the drum that is NOT playing counts (arcade: PlayerInfo.mode > 1),
    //     so 1P skips on the 2P drum and in a real 2P game nobody can skip;
    //   * only the RIM (kat) counts, never the face;
    //   * the count rises only when the rim differs from the previous one — a
    //     repeat of the same rim is ignored, not a reset;
    //   * any hit on the playing drum resets it (unless it already reached 10);
    //   * there is NO timeout of any kind;
    //   * at 10 the music is cut dead and the song ends at the normal result
    //     screen with whatever counts the player has.
    // Opt-in: only armed when the skin sets skin_flag("option_skip_row") and a
    // player in the enso has the option on, so a skin that never declares it is
    // bit-for-bit unaffected.
    static constexpr int SKIP_HITS = 10;
    PlayerNum skip_lane = PlayerNum::ALL;  // the drum that is not playing
    int    skip_count = 0;                 // alternating rim hits so far, 0..10
    int    skip_last  = -1;                // 1 = left rim, 0 = right rim (arcade encoding)
    size_t skip_play_hits = 0;             // playing-lane hit total, for the reset
    bool   skipped = false;
    std::unique_ptr<OutlinedText> skip_text;
    void init_skip();
    void update_skip();
    void poll_skip();        // the rule; only while un-skipped and un-paused
    void push_skip_state();  // the picture: Background:handle_skip + live_skip_*
    void do_skip();
    void draw_skip();

    std::optional<Screens> global_keys();

    void update_background(double current_ms);

    void save_score(int player_id, PlayerNum player_num);

    std::optional<Screens> update() override;

    void draw_players();

    void draw_overlay();

    void draw() override;
};
