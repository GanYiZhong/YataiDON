#pragma once

#include "../../libs/animation.h"
#include "../../libs/global_data.h"

struct TextureObject;

class ComboAnnounce {
private:
    PlayerNum player_num;
    int combo;
    double wait;
    FadeAnimation* fade;
    bool audio_played;
    TextureObject* t_announce_bg = nullptr;
    TextureObject* t_announce_digit = nullptr;
    TextureObject* t_announce_text = nullptr;
    TextureObject* t_announce_number = nullptr;
    TextureObject* t_announce_add = nullptr;

public:
    bool is_finished;

    ComboAnnounce(int combo, double current_ms, PlayerNum player_num);

    void update(double current_ms);
    void draw(float y);
};
