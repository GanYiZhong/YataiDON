#pragma once

#include "../libs/screen.h"
#include "../objects/game_over/game_over.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"

class GameOverScreen : public Screen {
private:
    AllNetIcon allnet_indicator;
    CoinOverlay coin_overlay;

    std::optional<GameOverSequence> sequence;

public:
    GameOverScreen() : Screen("game_over") {
    }

    void on_screen_start() override;

    Screens on_screen_end(Screens next_screen) override;

    std::optional<Screens> update() override;

    void draw() override;
};
