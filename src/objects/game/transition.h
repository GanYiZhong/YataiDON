#pragma once

#include "../../libs/text.h"
#include "../../libs/script.h"

// The song-loading (rainbow) transition. Optionally scripted: a skin that
// ships Scripts/.../transition.lua defining `SongTransition` can replace the
// backdrop motion (`draw_bg`) and/or the title block (`draw_info`) with its
// own. A skin without the script behaves exactly as before.
class Transition : public LuaScript {
private:
    sol::protected_function fn_update, fn_draw_bg, fn_draw_info;

    // ROUND 17 -- the 段位道場 loading screen.
    //
    // The cabinet does NOT use the rainbow curtain for a dan run. `LoadingHelper`
    // picks `loading/loading_song` only for an ordinary song; the dojo has its own
    // movie, `enso_dani/loading_dani/loading_dani_song.nulm`: a night scene (moon
    // + cloud bands + a sakura bough + four characters seen from behind) that pans
    // slowly upward, with the rank plaque fading in on the right. Until this round
    // `DanGameScreen::init_dan` did `transition.emplace("", "", true)` -- the
    // ordinary rainbow rig with an EMPTY title band, which is what the player saw.
    //
    // Enabled per run by `set_dan`; -1 means "not a dan run" and every existing
    // caller is untouched.
    int  dan_color   = -1;
    double dan_start_ms = 0.0;
    std::unique_ptr<OutlinedText> dan_rank_text;
    void draw_dan(float total_offset);

    bool is_second;
    std::unique_ptr<OutlinedText> title;
    std::unique_ptr<OutlinedText> subtitle;
    std::optional<ray::Texture2D> loading_graphic;
    void draw_song_info();
    void draw_default(float total_offset);

    MoveAnimation* rainbow_up;
    MoveAnimation* mini_up;
    MoveAnimation* chara_down;
    FadeAnimation* song_info_fade;
    FadeAnimation* song_info_fade_out;
public:

    Transition(const std::string& title, const std::string& subtitle, bool is_second);
    ~Transition();
    void start();
    void add_loading_graphic(const std::string& path);
    // `color` indexes the seven arcade rank plaques (the same 0..6 grouping
    // DAN_INFO::FRAME already uses); `rank_name` is the course's own name, drawn
    // over the plaque. The cabinet bakes 21 rank kanji instead, one per arcade
    // rank -- we cannot use those verbatim because our course list is not the
    // arcade's 20 (this library ships 十級..六級, which 39.06 has no chip for), so
    // the plaque art is the cabinet's and the lettering is ours, exactly as the
    // in-play plaque (`DanGameScreen::draw_dan_info` / `hori_name`) already does.
    void set_dan(int color, const std::string& rank_name);
    void update(double current_ms);
    void draw();

    bool is_finished();
};
