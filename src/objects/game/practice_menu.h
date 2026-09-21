#pragma once

#include <array>
#include "../../libs/text.h"

class PracticeMenu {
public:
    enum class Dialog { NONE, AUTO, RESTART, ANOTHER, END };

    enum class Action { NONE, END_GAME, ANOTHER_SONG, RESTART, JUMP_TO_MARK, SET_MARK, AUTO_ON, AUTO_OFF };

    static constexpr int MARK_SLOTS = 5;

    bool open = false;
    int index = 0;
    Dialog dialog = Dialog::NONE;
    int dialog_sel = 0;   // 0 = left option, 1 = right option

    bool editing_marks = false;
    bool jumping_marks = false;

    void init_textures();

    void open_menu();
    void close();

    void step(bool right);

    Action activate(bool auto_on);

    Action confirm();

    // Jump-point editor, entered from the SET_MARK menu row.
    void open_mark_edit();
    void close_mark_edit();

    // Free-roam jump-point navigation, entered from the JUMP_TO_MARK menu row.
    void open_jump_mode();

    void draw() const;
    void draw_dialog() const;

private:
    std::vector<std::unique_ptr<OutlinedText>> menu_text;
    std::unique_ptr<OutlinedText> dlg_title, dlg_left, dlg_right;

    void build_text();
    void open_dialog(Dialog which, bool auto_on);

    TextureObject* t_menu_panel = nullptr;
    TextureObject* t_menu_bar = nullptr;
    TextureObject* t_menu_bar_selected = nullptr;
    TextureObject* t_menu_auto_label = nullptr;
    TextureObject* t_menu_toggle = nullptr;
    TextureObject* t_menu_button = nullptr;
    TextureObject* t_menu_button_selected = nullptr;
};
