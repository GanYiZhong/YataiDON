#pragma once

#include "../../libs/text.h"

// The practice pause menu (issue #41): opened with the second drum's don
// while paused, stepped through with kat, confirmed with don. Owns its own
// visual state and drawing; PracticeGameScreen owns the game-flow side
// (audio, screen transitions, the scrobble/jump-point state) and drives
// this object through activate()/confirm()/step().
class PracticeMenu {
public:
    enum class Dialog { NONE, AUTO, RESTART, ANOTHER, END };

    // What the player asked for, resolved from either a bar pick (jump/mark)
    // or a confirmed sub-dialog. PracticeGameScreen turns this into the
    // actual game-flow action.
    enum class Action { NONE, END_GAME, ANOTHER_SONG, RESTART, JUMP_TO_MARK, SET_MARK, AUTO_ON, AUTO_OFF };

    bool open = false;
    int index = 0;
    Dialog dialog = Dialog::NONE;
    int dialog_sel = 0;   // 0 = left option, 1 = right option

    void open_menu();
    void close();

    // Kat: cycles the menu bars, or flips the two dialog options.
    void step(bool right);

    // Don while no dialog is up: activates the highlighted bar. Auto play's
    // bar needs the current auto state to preset its dialog toggle.
    Action activate(bool auto_on);

    // Don while a dialog is up: resolves it.
    Action confirm();

    void draw() const;
    void draw_dialog() const;

private:
    std::vector<std::unique_ptr<OutlinedText>> menu_text;
    std::unique_ptr<OutlinedText> dlg_title, dlg_left, dlg_right;

    void build_text();
    void open_dialog(Dialog which, bool auto_on);
};
