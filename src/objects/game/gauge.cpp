#include "gauge.h"
#include "../../libs/texture.h"

Gauge::Gauge(GaugeMode mode, PlayerNum player_num, int total_notes, int difficulty, int level)
    : mode(mode), player_num(player_num), total_notes(total_notes),
      gauge_length(0), previous_length(0), is_clear(false), is_rainbow(false),
      tamashii_fire_change(nullptr), gauge_update_anim(nullptr) {

    if (mode == GaugeMode::NORMAL) {
        gauge_max = 87.0f;
        this->difficulty = std::min((int)Difficulty::ONI, difficulty);
        this->level = std::min(10, level);

        clear_start = {52, 60, 69, 69};

        if (this->difficulty == (int)Difficulty::HARD) {
            string_diff = "_hard";
        } else if (this->difficulty == (int)Difficulty::NORMAL) {
            string_diff = "_normal";
        } else if (this->difficulty == (int)Difficulty::EASY) {
            string_diff = "_easy";
        } else {
            string_diff = "_hard";
        }

        table = {
            {
                {36.0f, 0.75f, -0.5f},
                {38.0f, 0.75f, -0.5f},
                {38.0f, 0.75f, -0.5f},
                {44.0f, 0.75f, -0.5f},
                {44.0f, 0.75f, -0.5f},
            },
            {
                {45.939f, 0.75f, -0.5f},
                {45.939f, 0.75f, -0.5f},
                {48.676f, 0.75f, -0.5f},
                {49.232f, 0.75f, -0.75f},
                {52.5f, 0.75f, -1.0f},
                {52.5f, 0.75f, -1.0f},
                {52.5f, 0.75f, -1.0f},
            },
            {
                {54.325f, 0.75f, -0.75f},
                {54.325f, 0.75f, -0.75f},
                {50.774f, 0.75f, -1.0f},
                {48.410f, 0.75f, -1.17f},
                {47.246f, 0.75f, -1.25f},
                {48.120f, 0.75f, -1.25f},
                {48.120f, 0.75f, -1.25f},
                {48.120f, 0.75f, -1.25f},
            },
            {
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.0f, 0.5f, -2.0f},
                {61.428f, 0.5f, -2.0f},
                {61.428f, 0.5f, -2.0f},
            }
        };

        tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
        gauge_update_anim    = (FadeAnimation*)tex.get_animation(10);
    } else {
        // DAN: animations loaded lazily in update() since this object may be
        // constructed before the texture system is ready (class-member initializer)
        gauge_max = 89.0f;
        this->difficulty = 0;
        this->level      = 1;
    }
}

void Gauge::add_good() {
    if (gauge_update_anim) gauge_update_anim->start();
    previous_length = (int)gauge_length;

    if (mode == GaugeMode::NORMAL) {
        gauge_length += (1.0f / total_notes) *
                        (100.0f * (clear_start[difficulty] / table[difficulty][level - 1].clear_rate));
    } else {
        gauge_length  += (1.0f / (total_notes * (gauge_max / 100.0f))) * 100.0f;
        visual_length  = gauge_length * tex.textures[GAUGE_DAN::_1P_BAR]->width;
    }
    if (gauge_length > gauge_max) gauge_length = gauge_max;
}

void Gauge::add_ok() {
    if (gauge_update_anim) gauge_update_anim->start();
    previous_length = (int)gauge_length;

    if (mode == GaugeMode::NORMAL) {
        gauge_length += ((1.0f * table[difficulty][level - 1].ok_multiplier) / total_notes) *
                        (100.0f * (clear_start[difficulty] / table[difficulty][level - 1].clear_rate));
    } else {
        gauge_length  += (0.5f / (total_notes * (gauge_max / 100.0f))) * 100.0f;
        visual_length  = gauge_length * tex.textures[GAUGE_DAN::_1P_BAR]->width;
    }
    if (gauge_length > gauge_max) gauge_length = gauge_max;
}

void Gauge::add_bad() {
    previous_length = (int)gauge_length;

    if (mode == GaugeMode::NORMAL) {
        gauge_length += ((1.0f * table[difficulty][level - 1].bad_multiplier) / total_notes) *
                        (100.0f * (clear_start[difficulty] / table[difficulty][level - 1].clear_rate));
        if (gauge_length < 0) gauge_length = 0;
        if (previous_length == gauge_max && gauge_length < gauge_max) {
            if (rainbow_fade_in.has_value()) rainbow_fade_in.reset();
            rainbow_start_ms = -1.0;
            rainbow_frac     = 0.0f;
        }
    } else {
        gauge_length  -= (2.0f / (total_notes * (gauge_max / 100.0f))) * 100.0f;
        if (gauge_length < 0) gauge_length = 0;
        visual_length  = gauge_length * tex.textures[GAUGE_DAN::_1P_BAR]->width;
    }
}

void Gauge::update(double current_ms) {
    if (mode == GaugeMode::DAN && !anims_loaded) {
        tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
        gauge_update_anim    = (FadeAnimation*)tex.get_animation(10);
        anims_loaded = true;
    }

    is_rainbow = (gauge_length == gauge_max);
    is_clear   = (mode == GaugeMode::NORMAL)
                 ? gauge_length > clear_start[std::min(difficulty, (int)Difficulty::HARD)] - 1
                 : is_rainbow;

    if (gauge_length == gauge_max && !rainbow_fade_in.has_value()) {
        rainbow_fade_in = (FadeAnimation*)tex.get_animation(63);
        rainbow_fade_in.value()->start();
        rainbow_start_ms = current_ms;
    }

    if (gauge_update_anim)    gauge_update_anim->update(current_ms);
    if (tamashii_fire_change) tamashii_fire_change->update(current_ms);

    if (rainbow_fade_in.has_value()) {
        rainbow_fade_in.value()->update(current_ms);
        rainbow_frac = (float)fmod((current_ms - rainbow_start_ms) / 75.0, 8.0);
    }
}

void Gauge::draw(float y) {
}
