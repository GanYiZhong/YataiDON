#pragma once

#include "game.h"

class Game2PScreen : public GameScreen {
private:
    std::optional<SongParser> parser_2p;

public:
    Game2PScreen() : GameScreen("game") {}

    // ROUND 58: the base on_screen_start() constructs Background and
    // ResultTransition from this, so the TWO_PLAYER versions are built once
    // (no more base-P1-build then re-emplace -- see game.h).
    PlayerNum scene_player_num() const override { return PlayerNum::TWO_PLAYER; }

    void init_tja(fs::path song) override;
    std::optional<Screens> update() override;
};
