#pragma once

#include "../../libs/global_data.h"
#include "../../libs/scores.h"
#include "../../libs/text.h"

class ModifierSelector {
private:
    static const std::map<std::string, std::string> TEX_MAP;
    static const std::array<std::string, 5> BASE_MOD_NAMES;

    // BASE_MOD_NAMES, plus the opt-in "skip" row (skin_config "option_skip_row")
    // and the opt-in "neiro" row (skin_config "option_neiro_row"), in the arcade's
    // order: ... ランダム / 演奏スキップ / ... / 音色.
    std::vector<std::string> mod_names;

    PlayerNum player_num;
    PlayerData* player;
    int current_mod_index;
    std::string language;
    int direction;

    std::vector<Modifiers> mods;

    FadeAnimation* blue_arrow_fade;
    MoveAnimation* blue_arrow_move;
    MoveAnimation* move_sideways;
    FadeAnimation* fade_sideways;

    std::vector<std::unique_ptr<OutlinedText>> text_name;
    std::unique_ptr<OutlinedText> text_true;
    std::unique_ptr<OutlinedText> text_false;
    std::unique_ptr<OutlinedText> text_speed;
    std::unique_ptr<OutlinedText> text_kimagure;
    std::unique_ptr<OutlinedText> text_detarame;

    std::unique_ptr<OutlinedText> text_true_2;
    std::unique_ptr<OutlinedText> text_false_2;
    std::unique_ptr<OutlinedText> text_speed_2;
    std::unique_ptr<OutlinedText> text_kimagure_2;
    std::unique_ptr<OutlinedText> text_detarame_2;

    // 音色 row: the hit-sound set names from the skin's neiro_list.txt plus the
    // trailing "no sound" entry, mirroring NeiroSelector (same player field).
    std::vector<std::string> neiro_names;
    int neiro_index = 0;
    std::string neiro_preview;
    std::unique_ptr<OutlinedText> text_neiro;
    std::unique_ptr<OutlinedText> text_neiro_2;

    bool has_row(const std::string& name) const {
        return std::find(mod_names.begin(), mod_names.end(), name) != mod_names.end();
    }
    bool has_neiro_row() const { return has_row("neiro"); }
    void load_neiro_names();
    void step_neiro(int dir);

    std::string row_label(const std::string& name) const;
    bool get_bool(int mod_index);
    void set_bool(int mod_index, bool value);

    std::unique_ptr<OutlinedText> make_text(const std::string& str);
    void start_text_animation(int direction);
    void draw_animated_text(const std::unique_ptr<OutlinedText>& text_primary, const std::unique_ptr<OutlinedText>& text_secondary, float x, float y, bool should_animate);

public:
    // One row of the 演奏オプション panel as Lua sees it. Data only: the engine
    // still draws the panel, this just lets a skin repaint / decorate it
    // (arcade paints changed values in a black box and greys passed rows).
    struct ModRow {
        std::string name;   // "auto" | "speed" | "display" | "inverse" | "random" | "skip" | "neiro"
        std::string value;  // exactly the string this row prints right now
        std::string label;  // the localised row NAME this row prints (演奏スキップ, 音色, ...)
        // The row's raw state, so a skin does not have to parse `value` (which
        // is localised): 0/1 for a bool row, 0/1/2 for random, modifier_speed
        // (10 = x1.0) for speed, the 0-based neiro index for neiro.
        int  state;
        bool changed;       // differs from the engine default for that row
        bool enabled;       // still editable: panel not confirmed and cursor has not passed it
        // The row exists but the engine will ignore it, so the arcade greys it
        // out (song_select_option_main.lua: `if player_num >= 2 then
        // mc_OptionSelector_*[kSelectSkip]:GrayOut()`). Today that is only
        // 演奏スキップ with both drums in the enso - GameScreen::arm_skip gives
        // up when no lane is free, which is the same rule.
        bool greyed;
    };
    // True when the row is inert for this session and the arcade greys it out.
    // Currently only the skip row in a 2P enso: no free drum, no skip. Shared by
    // the Lua data-out AND the input path, so a greyed row cannot be toggled.
    bool row_greyed(const std::string& name) const;
    std::vector<ModRow> lua_rows();
    int  lua_index() const { return current_mod_index; }
    int  lua_row_count() const { return (int)mod_names.size(); }
    // The left/right value-change tween the panel is playing right now:
    // direction (+1 = the new value slides in from the left, -1 from the
    // right), the cross-fade 0..1, and whether it is still running. The arcade
    // uses it for the 5 px outward nudge of the arrow that was pressed.
    int   lua_change_dir() const { return direction; }
    float lua_change_fade() const { return (float)fade_sideways->attribute; }
    bool  lua_change_active() const { return !move_sideways->is_finished; }

    bool is_finished;
    bool is_confirmed;
    // The slide currently running. Points at the slide-IN curve until the panel
    // is confirmed and at move_out (when the skin defines animation 39)
    // afterwards: the arcade board overshoots at the END going in and at the
    // START coming out, which one animation cannot express.
    MoveAnimation* move;
    MoveAnimation* move_out = nullptr;

    ModifierSelector(PlayerNum player_num, PlayerData* player);
    void update(double current_ms);
    void confirm();
    void left();
    void right();
    void draw();
};
