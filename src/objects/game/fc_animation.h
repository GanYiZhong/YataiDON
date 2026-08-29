#pragma once

#include "../../libs/animation.h"

class FCAnimation {
private:
    bool is_2p;
    FadeAnimation* bachio_fade_in;
    TextureChangeAnimation* bachio_texture_change;
    TextureChangeAnimation* bachio_out;
    MoveAnimation* bachio_move_out;
    std::vector<FadeAnimation*> clear_separate_fade_in;
    std::vector<TextStretchAnimation*> clear_separate_stretch;
    FadeAnimation* clear_highlight_fade_in;
    MoveAnimation* fc_highlight_up;
    FadeAnimation* fc_highlight_fade_out;
    MoveAnimation* bachio_move_out_2;
    MoveAnimation* bachio_move_up;
    FadeAnimation* fan_fade_in;
    TextureChangeAnimation* fan_texture_change;
    bool draw_clear_full;
    std::string name;
    int frame;
    // Banner texture set + sounds: the ドンダフルコンボ (all-良) variant when the skin
    // ships one, otherwise the plain フルコンボ set.
    uint32_t combo_tex;
    uint32_t combo_highlight_tex;
    uint32_t combo_overlay_tex;
    std::string combo_sound;
    std::string combo_voice;
    // Optional backing panel behind the banner. The arcade's dondafullMc puts a
    // 1424x264 gold panel at depth 0 of the clip and fades it in over 15 frames
    // together with the lettering; a skin opts in purely by shipping
    // `game/ending_donderful/background`. `has_panel` is false for any skin that
    // does not, which is every skin but the arcade port.
    bool has_panel;
    uint32_t panel_tex;
    FadeAnimation* panel_fade_in;

public:
    // donderful: the play was all-良 (no 可, no 不可). Falls back to the plain
    // full-combo art/sound when the skin does not provide the donderful set.
    FCAnimation(bool is_2p, bool donderful = false);

    void update(double current_ms);
    void draw();
};
