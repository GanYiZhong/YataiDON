#include "gauge.h"
#include <cmath>
#include "../../libs/texture.h"

Gauge::Gauge(int total_notes, int difficulty, int level, PlayerNum player_num)
    : player_num(player_num) {
    this->difficulty = std::min((int)Difficulty::ONI, difficulty);
    // Per-difficulty norma in cells of 50: easy 30, normal 35, hard 35, oni/ura 40
    // -> 6000 / 7000 / 7000 / 8000 soul points.
    clear_points = this->difficulty <= (int)Difficulty::EASY   ? 6000
                 : this->difficulty <= (int)Difficulty::HARD   ? 7000
                                                              : 8000;
    GaugeTable table_row = table[this->difficulty][std::min(9, level - 1)];
    good_points = (int)std::ceil(1000000 / (total_notes * table_row.soul_percent));
    ok_points   = (int)std::round(good_points * table_row.ok_multiplier);
    bad_points  = (int)std::round(good_points * table_row.bad_multiplier);
    points = 0;

    // Clear-zone art tier follows the norma cell: Hard shares the normal (35-cell) art.
    if (this->difficulty == (int)Difficulty::EASY)      string_diff = "_easy";
    else if (this->difficulty <= (int)Difficulty::HARD) string_diff = "_normal";
    else                                                 string_diff = "_hard";

    tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
    gauge_update_anim    = (FadeAnimation*)tex.get_animation(10);
}

void Gauge::add_good() {
    if (gauge_update_anim) gauge_update_anim->start();
    previous_points = points;
    points = std::max(0, std::min(max_points, points + good_points));
}

void Gauge::add_ok() {
    if (gauge_update_anim) gauge_update_anim->start();
    previous_points = points;
    points = std::max(0, std::min(max_points, points + ok_points));
}

void Gauge::add_bad() {
    previous_points = points;
    points = std::max(0, std::min(max_points, points + bad_points));

    if (previous_points == max_points && points < max_points) {
        if (rainbow_fade_in.has_value()) rainbow_fade_in.reset();
        rainbow_start_ms = -1.0;
        rainbow_frac     = 0.0f;
    }
}

void Gauge::update(double current_ms) {
    if (get_is_rainbow() && !rainbow_fade_in.has_value()) {
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
    bool mirrored = y > tex.screen_height / 2.0f;
    Mirror mirror = mirrored ? Mirror::VERTICAL : Mirror::NONE;

    tex.draw_texture(tex.get_enum("gauge/border" + string_diff), {.mirror = mirror, .y = y, .index = mirrored});

    tex.draw_texture(tex.get_enum("gauge/" + (std::to_string((int)player_num) + "p_unfilled" + string_diff)),
                      {.mirror = mirror, .y = y, .index = mirrored});

    const SkinInfo* cells_cfg = tex.skin_entry("gauge_cells");
    const int bar_units = (cells_cfg && cells_cfg->x > 0) ? (int)std::lround(cells_cfg->x) : 87;
    int gauge_length_int = points * bar_units / max_points;
    int previous_length_int = previous_points * bar_units / max_points;
    int clear_point = clear_points * bar_units / max_points;
    float bar_width  = tex.textures[tex.get_enum("gauge/" + std::to_string((int)player_num) + "p_bar")]->width;

    const bool cell_fade_in = tex.options[SCO::GAUGE_CELL_FADE_IN];
    const bool cell_pending = gauge_length_int <= bar_units && gauge_length_int > previous_length_int
                              && gauge_update_anim && gauge_update_anim->is_started && !gauge_update_anim->is_finished;
    const int  solid_length = (cell_fade_in && cell_pending) ? gauge_length_int - 1 : gauge_length_int;
    const float anim_alpha  = gauge_update_anim ? (float)gauge_update_anim->attribute : 0.0f;
    const float cell_alpha  = cell_fade_in ? 1.0f - anim_alpha : anim_alpha;

    if (solid_length > 0)
        tex.draw_texture(tex.get_enum("gauge/" + (std::to_string((int)player_num) + "p_bar")),
                          {.y = y, .x2 = std::min(solid_length * bar_width, (clear_point - 1) * bar_width) - bar_width, .index = mirrored});

    // The transition piece is the first gold cell (index clear_point-1, the rounded
    // cap of the clear zone). Light it exactly when the gauge is cleared: on grids
    // where clear_points falls mid-cell (87 cells: 8000 -> 69.6) a cell-count test
    // lights it up to a cell early or late relative to the クリア state.
    const bool clear_cap_lit = get_is_clear() && !(cell_fade_in && cell_pending && gauge_length_int == clear_point);
    if (clear_cap_lit)
        tex.draw_texture(GAUGE::BAR_CLEAR_TRANSITION,
                          {.mirror = mirror, .x = (clear_point - 1) * bar_width, .y = y, .index = mirrored});

    // Gold zone = cells clear_point .. solid_length-1. The piece texture is already one
    // cell wide, so the stretch is (cells - 1) * bar_width, like the red bar above;
    // without the -bar_width the strip ran one cell ahead of the fill.
    if (solid_length > clear_point) {
        const float gold_x2 = (solid_length - clear_point) * bar_width - bar_width;
        tex.draw_texture(GAUGE::BAR_CLEAR_TOP,
                          {.mirror = mirror, .x = clear_point * bar_width, .y = y,
                           .x2 = gold_x2, .index = mirrored});
        tex.draw_texture(GAUGE::BAR_CLEAR_BOTTOM,
                          {.x = clear_point * bar_width, .y = y,
                           .x2 = gold_x2, .index = mirrored});
    }

    if (get_is_rainbow() && rainbow_fade_in.has_value()) {
        float fade    = rainbow_fade_in.value()->attribute;
        int   frame_a = (int)rainbow_frac % 8;
        int   frame_b = (frame_a + 1) % 8;
        float t       = rainbow_frac - (int)rainbow_frac;
        tex.draw_texture(tex.get_enum("gauge/rainbow" + string_diff),
                          {.frame = frame_a, .mirror = mirror, .y = y, .fade = fade, .index = mirrored});
        tex.draw_texture(tex.get_enum("gauge/rainbow" + string_diff),
                          {.frame = frame_b, .mirror = mirror, .y = y, .fade = fade * t, .index = mirrored});
    }

    // Flash mode: the sprite fades out over the solid cell. Fade-in mode: it IS the
    // cell while the animation runs, and must vanish once the solid bar takes over
    // (otherwise it stays at full alpha on the last cell — visible in the gold zone
    // and over the rainbow).
    const bool show_gauge_up = cell_fade_in ? cell_pending
                                            : (gauge_length_int <= bar_units && gauge_length_int > previous_length_int);
    if (show_gauge_up) {
        // The gauge-up sprite belongs on the cell that was just filled (index
        // gauge_length_int - 1), not on the empty cell after it.
        const float fade_x = (gauge_length_int - 1) * bar_width;
        if (gauge_length_int == clear_point) {
            tex.draw_texture(GAUGE::BAR_CLEAR_TRANSITION_FADE,
                              {.mirror = mirror, .x = fade_x, .y = y,
                               .fade = cell_alpha, .index = mirrored});
        } else if (gauge_length_int > clear_point) {
            tex.draw_texture(GAUGE::BAR_CLEAR_FADE,
                              {.x = fade_x, .y = y,
                               .fade = cell_alpha, .index = mirrored});
        } else {
            tex.draw_texture(tex.get_enum("gauge/" + (std::to_string((int)player_num) + "p_bar_fade")),
                              {.x = fade_x, .y = y,
                               .fade = cell_alpha, .index = mirrored});
        }
    }

    tex.draw_texture(tex.get_enum("gauge/overlay" + string_diff),
                      {.mirror = mirror, .y = y, .fade = 0.15f, .index = mirrored});

    // クリア label / 魂 light up with the cleared state itself, and the label frame
    // follows the clear-zone art tier (easy / normal+hard / oni), not the raw difficulty.
    const int art_tier = (string_diff == "_easy") ? 0 : (string_diff == "_normal") ? 1 : 2;
    if (get_is_clear()) {
        tex.draw_texture(tex.get_enum("gauge/clear_" + global_data.config->general.language),
                          {.y = y, .index = art_tier + (mirrored * 3)});
        if (get_is_rainbow()) {
            tex.draw_texture(GAUGE::TAMASHII_FIRE,
                              {.frame = (int)tamashii_fire_change->attribute, .scale = 0.75f,
                               .center = true, .y = y, .index = mirrored});
        }
        tex.draw_texture(GAUGE::TAMASHII, {.y = y, .index = mirrored});
        int fire_frame = (int)tamashii_fire_change->attribute;
        if (get_is_rainbow() && (fire_frame == 0 || fire_frame == 1 || fire_frame == 4 || fire_frame == 5))
            tex.draw_texture(GAUGE::TAMASHII_OVERLAY, {.y = y, .fade = 0.5f, .index = mirrored});
    } else {
        tex.draw_texture(tex.get_enum("gauge/clear_dark_" + global_data.config->general.language),
                          {.y = y, .index = art_tier + (mirrored * 3)});
        tex.draw_texture(GAUGE::TAMASHII_DARK, {.y = y, .index = mirrored});
    }
}
