#pragma once
#include "song_select.h"

class PracticeSongSelectScreen : public SongSelectScreen {
protected:
    Screens get_game_screen_target() override { return Screens::GAME_PRACTICE; }
    bool hides_dan() override { return true; }

public:
    PracticeSongSelectScreen() : SongSelectScreen("song_select") {}
};
