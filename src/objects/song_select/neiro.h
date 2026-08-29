#pragma once

#include "../../libs/global_data.h"
#include "../../libs/scores.h"
#include "../../libs/text.h"

class NeiroSelector {
private:
    PlayerNum player_num;
    PlayerData* player;
    int selected_sound;
    int direction;
    std::vector<std::string> sounds;
    std::string curr_sound;

    FadeAnimation* blue_arrow_fade;
    MoveAnimation* blue_arrow_move;
    MoveAnimation* move_sideways;
    FadeAnimation* fade_sideways;

    std::unique_ptr<OutlinedText> text;
    std::unique_ptr<OutlinedText> text_2;

    void load_sound();

public:
    // Data-out for skins, matching ModifierSelector::lua_rows(): the hit-sound
    // set names and which one the cursor is on.
    const std::vector<std::string>& lua_names() const { return sounds; }
    int lua_index() const { return selected_sound; }

    bool is_finished;
    bool is_confirmed;
    // Same split as ModifierSelector: `move` is the slide currently running and
    // becomes move_out (animation 39, when the skin defines it) on confirm.
    MoveAnimation* move;
    MoveAnimation* move_out = nullptr;

    NeiroSelector(PlayerNum player_num, PlayerData* player);
    void update(double current_ms);
    void left();
    void right();
    void confirm();
    void draw();
};
