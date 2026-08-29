#pragma once
#include "song_select.h"

class SongSelect2PScreen : public SongSelectScreen {
private:
    std::unique_ptr<SongSelectPlayer> player_2;

    void handle_input_browsing(double current_ms) override;
    void handle_input_selecting() override;
    void select_song(SongBox* song) override;
    void draw_overlays() override;

protected:
    bool is_2p_screen() override { return true; }
    Screens get_game_screen_target() override { return Screens::GAME_2P; }

    // ROUND 17 -- 段位道場 is a ONE-PLAYER mode on the cabinet, and the cabinet
    // expresses that by REMOVAL, not by greying:
    //
    //   * `entry/mode_select.lua:231-241` builds the mode board's list and only
    //     appends `GameMode.kDani` when
    //         Cabinet.DaniDojoAvailable()
    //         and ( P1 is a kCard user spending a credit and P2.user_type == kNone
    //            or P2 is a kCard user spending a credit and P1.user_type == kNone )
    //     i.e. exactly ONE seat occupied. With both seats in, the 段位道場 board is
    //     never added to `menu_list` at all.
    //   * `song_select/song_select_all.lua:1541-1543` does the same to the WHEEL:
    //     `RemoveDani()` `table.remove`s the "dani" entry from `tbl_genreName`.
    //   * `CardTouchError.kSingleOnly` (`entry/entry_main.lua:2197`) is the
    //     cabinet's own name for "this mode is single-play only".
    //
    // Our port has no 段位道場 entry on ENTRY -- the dojo is reached through the
    // wheel folder -- so the wheel is the only place the rule can land, and
    // `navigator.hide_dan` (already used by 特訓モード) is exactly `RemoveDani()`.
    //
    // This also settles the mid-song-select 2P join (`songselect_2p_join`,
    // ROUND 15): the join returns to ENTRY and comes back as THIS screen, whose
    // `on_screen_start` calls the base and therefore re-inits the navigator with
    // hide_dan set. A 1P player parked on the dan folder when the second seat
    // joins is never stranded inside a now-forbidden folder -- the folder is gone
    // and the cursor is rebuilt, which is what the cabinet's own "both players
    // re-run mode select" does.
    bool hides_dan() override { return true; }

public:
    SongSelect2PScreen() : SongSelectScreen("song_select") {}

    void on_screen_start() override;
    std::optional<Screens> update() override;
    void draw() override;
};
