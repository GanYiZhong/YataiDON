#pragma once
#include "song_select.h"

class PracticeSongSelectScreen : public SongSelectScreen {
protected:
    Screens get_game_screen_target() override { return Screens::GAME_PRACTICE; }
    bool hides_dan() override { return true; }
    // 練習モード is a single-seat screen with no arcade counterpart; the cabinet's
    // 2P-join offer only exists on the 演奏ゲーム song select.
    bool allows_second_player_join() override { return false; }

public:
    PracticeSongSelectScreen() : SongSelectScreen("song_select") {}
};
