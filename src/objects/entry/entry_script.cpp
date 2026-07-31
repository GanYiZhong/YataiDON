#include "entry_script.h"

EntryScript::EntryScript() {}

void EntryScript::update(double current_ms) {}
void EntryScript::start_side_select() {}
void EntryScript::restart_side_select() {}
void EntryScript::draw_background() {}
void EntryScript::draw_side_select() {}
void EntryScript::draw_side_select_buttons(int side) {}
void EntryScript::draw_footer(bool p1_joined, bool p2_joined) {}
void EntryScript::draw_player_entry() {}

float EntryScript::get_side_select_fade() {
    return 1.0f;
}
