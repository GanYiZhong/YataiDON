#pragma once

#include "box.h"

class BoxManager {
private:
    std::vector<Screens> box_locations;
    std::vector<std::unique_ptr<Box>> boxes;
    int num_boxes;
    int selected_box_index;
    FadeAnimation* fade_out;
    bool is_2p;
    bool is_vertical;
    // ROUND 83 — index of the 段位道場 board, or -1 when the skin/library gate refused it.
    int dan_box_index = -1;
    // ROUND 86 — the CONTENT half of the dojo gate (skin declares `entry_dan` AND the
    // course library is on disk), probed once in the constructor because it walks the
    // library roots. The SEAT half (`is_2p`) is re-evaluated every frame instead, which
    // is what makes the list rebuildable; see box_manager.cpp.
    bool dan_available = false;
    std::string dan_text;

    void build_board_list();          // mode_select.lua CreateBoardList
    void change_board_list();         // mode_select.lua ChangeBoardList

public:
    bool costume_menu_open;
    PlayerNum opening_player = PlayerNum::P1;

    // `two_player` is the seat state on the very first frame — the cabinet's
    // CreateBoardList is handed both playdatas from the start, so a session that is
    // already 2P must never build the board at all (not build it and refuse it later).
    explicit BoxManager(bool two_player = false);
    bool check_board_list_change() const;   // mode_select.lua CheckBoardListChange
    bool selection_allowed() const;   // ROUND 83 — IsOnePlayerOnly(kDani), backstop only
    void select_box();
    bool is_box_selected();
    bool is_finished();
    bool is_costume_box();
    void open_costume_menu(PlayerNum player_num = PlayerNum::P1);
    Screens selected_box();
    void move_left();
    void move_right();
    void update(double current_time_ms, bool is_2p);
    void draw();
};
