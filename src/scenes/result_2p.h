#pragma once

#include "result.h"

class Result2PScreen : public ResultScreen {
private:
    std::optional<ResultPlayer> player_2;

protected:
    // ROUND 19: the cabinet has ONE `Main.m_State` driving both boards -- `Update()`
    // hands the same `isEffectSkip_` to `scoreBase1p_` and `scoreBase2p_`, and
    // `WaitNextScene` tests `Input.AVAILABLE_OK`, i.e. EITHER seat.  So one press
    // collapses/advances for both players, and the screen is only "revealed" once the
    // slower board is.  (`AVAILABLE_*` vs the per-seat `PLAYER1_*`/`PLAYER2_*` the QR
    // state uses -- ResultMain.lua lines 784/788 -- is the cabinet drawing exactly that
    // distinction on this same screen.)
    double reveal_end_ms() override;

public:
    Result2PScreen() : ResultScreen() {}

    void on_screen_start() override;
    std::optional<Screens> update() override;
    void draw() override;
};
