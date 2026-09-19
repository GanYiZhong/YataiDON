#include "game_over.h"

void GameOverScreen::on_screen_start() {
    Screen::on_screen_start();
    sequence.emplace();
}

Screens GameOverScreen::on_screen_end(Screens next_screen) {
    sequence.reset();
    return Screen::on_screen_end(next_screen);
}

std::optional<Screens> GameOverScreen::update() {
    Screen::update();
    double current_ms = get_current_ms();
    allnet_indicator.update(current_ms);
    sequence->update(current_ms);

    if (sequence->is_finished()) {
        return on_screen_end(Screens::TITLE);
    }

    return std::nullopt;
}

void GameOverScreen::draw() {
    sequence->draw();
    coin_overlay.draw();
    allnet_indicator.draw();
}
