#include "combo_announce.h"
#include "../../libs/texture.h"
#include "../../libs/audio.h"

ComboAnnounce::ComboAnnounce(int combo, double current_ms, PlayerNum player_num)
    : combo(combo), wait(current_ms), player_num(player_num),
      is_finished(false), audio_played(false) {

    fade = (FadeAnimation*)tex.get_animation(65);
    fade->start();
}

void ComboAnnounce::update(double current_ms) {
    if (current_ms >= wait + 1666.67f && !is_finished) {
        fade->start();
        is_finished = true;
    }

    fade->update(current_ms);

    if (!audio_played && combo >= 100) {
        std::string sound_name = "combo_" + std::to_string(combo) + "_" + std::to_string(static_cast<int>(player_num)) + "p";
        audio.play_sound(sound_name, VolumePreset::VOICE);
        audio_played = true;
    }
}

void ComboAnnounce::draw(float y) {
}
