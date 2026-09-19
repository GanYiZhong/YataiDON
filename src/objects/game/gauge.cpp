#include "gauge.h"
#include <cmath>

Gauge::Gauge(int total_notes, int difficulty, int level, PlayerNum player_num)
    : player_num(player_num) {
    this->difficulty = std::clamp(difficulty, 0, (int)Difficulty::ONI);
    clear_points = this->difficulty <= (int)Difficulty::EASY   ? 6000
                 : this->difficulty <= (int)Difficulty::HARD   ? 7000
                                                              : 8000;
    const GaugeTable& table_row = table[this->difficulty][std::clamp(level - 1, 0, 9)];
    const double denom = std::max(1, total_notes) * (double)table_row.soul_percent;
    good_points = (denom > 0.0) ? 1'000'000.0 / denom : 0;
    ok_points   = good_points * table_row.ok_multiplier;
    bad_points  = good_points * table_row.bad_multiplier;
    points = 0;

    if (this->difficulty == (int)Difficulty::EASY)      string_diff = "_easy";
    else if (this->difficulty <= (int)Difficulty::HARD) string_diff = "_normal";
    else                                                 string_diff = "_hard";

    tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
    gauge_update_anim    = (FadeAnimation*)tex.get_animation(10);

    const std::string p = std::to_string((int)player_num) + "p_";
    t_border = tex.get_texture("gauge/border" + string_diff);
    t_unfilled = tex.get_texture("gauge/" + p + "unfilled" + string_diff);
    t_bar = tex.get_texture("gauge/" + p + "bar");
    t_bar_clear_transition = tex.get_texture("gauge/bar_clear_transition");
    t_bar_clear_top = tex.get_texture("gauge/bar_clear_top");
    t_bar_clear_bottom = tex.get_texture("gauge/bar_clear_bottom");
    t_rainbow = tex.get_texture("gauge/rainbow" + string_diff);
    t_bar_clear_transition_fade = tex.get_texture("gauge/bar_clear_transition_fade");
    t_bar_clear_fade = tex.get_texture("gauge/bar_clear_fade");
    t_bar_fade = tex.get_texture("gauge/" + p + "bar_fade");
    t_overlay = tex.get_texture("gauge/overlay" + string_diff);
    t_tamashii_fire = tex.get_texture("gauge/tamashii_fire");
    t_tamashii = tex.get_texture("gauge/tamashii");
    t_tamashii_overlay = tex.get_texture("gauge/tamashii_overlay");
    t_tamashii_dark = tex.get_texture("gauge/tamashii_dark");

    t_dan_bar = tex.get_texture("gauge_dan/" + p + "bar");
    t_dan_bar_fade = tex.get_texture("gauge_dan/" + p + "bar_fade");
    t_dan_border = tex.get_texture("gauge_dan/border");
    t_dan_unfilled = tex.get_texture("gauge_dan/" + p + "unfilled");
    t_dan_rainbow = tex.get_texture("gauge_dan/rainbow");
    t_dan_overlay = tex.get_texture("gauge_dan/overlay");
    t_dan_tamashii_fire = tex.get_texture("gauge_dan/tamashii_fire");
    t_dan_tamashii = tex.get_texture("gauge_dan/tamashii");
    t_dan_tamashii_overlay = tex.get_texture("gauge_dan/tamashii_overlay");
    t_dan_tamashii_dark = tex.get_texture("gauge_dan/tamashii_dark");
    t_clear = tex.get_texture("gauge/clear_" + global_data.config->general.language);
    t_clear_dark = tex.get_texture("gauge/clear_dark_" + global_data.config->general.language);
}

Gauge Gauge::dan(const std::vector<DanSongEntry>& songs, int total_notes, PlayerNum player_num) {
    // a missing LEVEL in the tja arrives as 0. treat it as oni 10 like player does
    auto diff_of  = [](const DanSongEntry& s) { return s.level <= 0 ? (int)Difficulty::ONI : s.difficulty; };
    auto level_of = [](const DanSongEntry& s) { return s.level <= 0 ? 10 : s.level; };

    const DanSongEntry& first = songs.at(0);
    Gauge g(total_notes, diff_of(first), level_of(first), player_num);
    g.dan_mode     = true;
    g.string_diff  = "";
    g.clear_points = g.max_points;   // no norma zone, the course bar is full or it isn't

    // one rate for the whole course. harmonic mean of the songs soul percentages is what
    // the arcade does when it breaks the gauge into thirds. ok/bad just averaged
    GaugeTable row{0.0, 0.0, 0.0};
    double inv_sum = 0.0;
    for (const DanSongEntry& s : songs) {
        const int d = std::clamp(diff_of(s), 0, (int)Difficulty::ONI);
        const GaugeTable& r = g.table[d][std::clamp(level_of(s) - 1, 0, 9)];
        inv_sum            += 1.0 / r.soul_percent;
        row.ok_multiplier  += r.ok_multiplier  / songs.size();
        row.bad_multiplier += r.bad_multiplier / songs.size();
    }
    row.soul_percent = songs.size() / inv_sum;

    const double denom = std::max(1, total_notes) * row.soul_percent;
    g.good_points = (denom > 0.0) ? 1'000'000.0 / denom : 0;
    g.ok_points   = g.good_points * row.ok_multiplier;
    g.bad_points  = g.good_points * row.bad_multiplier;
    return g;
}

void Gauge::apply_points_clamped(double delta) {
    previous_points = points;
    points = std::clamp(points + delta, 0.0, (double)max_points);
    if (std::abs(points - max_points) < POINTS_EPS) points = max_points;
    if (std::abs(points - clear_points) < POINTS_EPS) points = clear_points;
}

void Gauge::add_good() {
    if (gauge_update_anim) gauge_update_anim->start();
    apply_points_clamped(good_points);
}

void Gauge::add_ok() {
    if (gauge_update_anim) gauge_update_anim->start();
    apply_points_clamped(ok_points);
}

void Gauge::add_bad() {
    apply_points_clamped(bad_points);

    //this comparison is safe because apply_points_clamped snaps points onto max_points when within POINTS_EPS
    const bool was_full = previous_points >= max_points;
    if (was_full && points < max_points) {
        if (rainbow_fade_in.has_value() && rainbow_fade_in.value()) rainbow_fade_in.value()->pause();
        rainbow_fade_in.reset();
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
    if (dan_mode) { draw_dan(); return; }
    bool mirrored = y > tex.screen_height / 2.0f;
    Mirror mirror = mirrored ? Mirror::VERTICAL : Mirror::NONE;

    tex.draw_texture(t_border, {.mirror = mirror, .y = y, .index = mirrored});

    tex.draw_texture(t_unfilled, {.mirror = mirror, .y = y, .index = mirrored});

    const SkinInfo* cells_cfg = tex.skin_entry("gauge_cells");
    const int bar_units = (cells_cfg && cells_cfg->x > 0) ? (int)std::lround(cells_cfg->x) : 87;

    // explicit floor: points is now double, so the truncation would otherwise be an implicit narrowing
    int gauge_length_int = (int)std::floor(points * bar_units / max_points);
    int previous_length_int = (int)std::floor(previous_points * bar_units / max_points);

    int clear_point = std::clamp(clear_points * bar_units / max_points, 1, bar_units);
    const float bar_width = t_bar->width;

    const bool cell_fade_in = tex.options[SCO::GAUGE_CELL_FADE_IN];
    const bool cell_pending = gauge_length_int <= bar_units && gauge_length_int > previous_length_int
                              && gauge_update_anim && gauge_update_anim->is_started && !gauge_update_anim->is_finished;
    const int  solid_length = (cell_fade_in && cell_pending) ? gauge_length_int - 1 : gauge_length_int;
    const float anim_alpha  = gauge_update_anim ? (float)gauge_update_anim->attribute : 0.0f;
    const float cell_alpha  = cell_fade_in ? 1.0f - anim_alpha : anim_alpha;

    if (solid_length > 0)
        tex.draw_texture(t_bar,
                          {.y = y, .x2 = std::min(solid_length * bar_width, (clear_point - 1) * bar_width) - bar_width, .index = mirrored});

    // The transition piece is the first gold cell (index clear_point-1, the rounded
    // cap of the clear zone). Light it exactly when the gauge is cleared: on grids
    // where clear_points falls mid-cell (87 cells: 8000 -> 69.6) a cell-count test
    // lights it up to a cell early or late relative to the クリア state.
    const bool clear_cap_lit = get_is_clear() && !(cell_fade_in && cell_pending && gauge_length_int == clear_point);
    if (clear_cap_lit)
        tex.draw_texture(t_bar_clear_transition,
                          {.mirror = mirror, .x = (clear_point - 1) * bar_width, .y = y, .index = mirrored});

    // Gold zone = cells clear_point .. solid_length-1. The piece texture is already one
    // cell wide, so the stretch is (cells - 1) * bar_width, like the red bar above;
    // without the -bar_width the strip ran one cell ahead of the fill.
    if (solid_length > clear_point) {
        const float gold_x2 = (solid_length - clear_point) * bar_width - bar_width;
        tex.draw_texture(t_bar_clear_top,
                          {.mirror = mirror, .x = clear_point * bar_width, .y = y,
                           .x2 = gold_x2, .index = mirrored});
        tex.draw_texture(t_bar_clear_bottom,
                          {.x = clear_point * bar_width, .y = y,
                           .x2 = gold_x2, .index = mirrored});
    }

    if (get_is_rainbow() && rainbow_fade_in.has_value()) {
        float fade    = rainbow_fade_in.value()->attribute;
        int   frame_a = (int)rainbow_frac % 8;
        int   frame_b = (frame_a + 1) % 8;
        float t       = rainbow_frac - (int)rainbow_frac;
        tex.draw_texture(t_rainbow,
                          {.frame = frame_a, .mirror = mirror, .y = y, .fade = fade, .index = mirrored});
        tex.draw_texture(t_rainbow,
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
            tex.draw_texture(t_bar_clear_transition_fade,
                              {.mirror = mirror, .x = fade_x, .y = y,
                               .fade = cell_alpha, .index = mirrored});
        } else if (gauge_length_int > clear_point) {
            tex.draw_texture(t_bar_clear_fade,
                              {.x = fade_x, .y = y,
                               .fade = cell_alpha, .index = mirrored});
        } else {
            tex.draw_texture(t_bar_fade,
                              {.x = fade_x, .y = y,
                               .fade = cell_alpha, .index = mirrored});
        }
    }

    tex.draw_texture(t_overlay,
                      {.mirror = mirror, .y = y, .fade = 0.15f, .index = mirrored});

    // クリア label / 魂 light up with the cleared state itself, and the label frame
    // follows the clear-zone art tier (easy / normal+hard / oni), not the raw difficulty.
    const int art_tier = (string_diff == "_easy") ? 0 : (string_diff == "_normal") ? 1 : 2;
    if (get_is_clear()) {
        tex.draw_texture(t_clear, {.y = y, .index = art_tier + (mirrored * 3)});
        if (get_is_rainbow()) {
            tex.draw_texture(t_tamashii_fire,
                              {.frame = tamashii_fire_change ? (int)tamashii_fire_change->attribute : 0, .scale = 0.75f,
                               .center = true, .y = y, .index = mirrored});
        }
        tex.draw_texture(t_tamashii, {.y = y, .index = mirrored});
        int fire_frame = tamashii_fire_change ? (int)tamashii_fire_change->attribute : 0;
        if (get_is_rainbow() && (fire_frame == 0 || fire_frame == 1 || fire_frame == 4 || fire_frame == 5))
            tex.draw_texture(t_tamashii_overlay, {.y = y, .fade = 0.5f, .index = mirrored});
    } else {
        tex.draw_texture(t_clear_dark, {.y = y, .index = art_tier + (mirrored * 3)});
        tex.draw_texture(t_tamashii_dark, {.y = y, .index = mirrored});
    }
}

// The dan gauge lives at the absolute positions of game/gauge_dan/texture.json, so it
// takes no lane offset; the fill is one bar-texture-width per cell like the normal gauge.
void Gauge::draw_dan() {
    TextureObject* const bar_id  = t_dan_bar;
    TextureObject* const fade_id = t_dan_bar_fade;
    tex.draw_texture(t_dan_border, {});
    tex.draw_texture(t_dan_unfilled, {});

    const SkinInfo* cells_cfg = tex.skin_entry("gauge_cells");
    const int bar_units = (cells_cfg && cells_cfg->x > 0) ? (int)std::lround(cells_cfg->x) : 87;

    // explicit floor: points is now double, so the truncation would otherwise be an implicit narrowing
    const int gauge_length_int    = (int)std::floor(points * bar_units / max_points);
    const int previous_length_int = (int)std::floor(previous_points * bar_units / max_points);

    const float bar_width = bar_id->width;

    const bool cell_fade_in = tex.options[SCO::GAUGE_CELL_FADE_IN];
    const bool cell_pending = gauge_length_int <= bar_units && gauge_length_int > previous_length_int
                              && gauge_update_anim && gauge_update_anim->is_started && !gauge_update_anim->is_finished;
    const int  solid_length = (cell_fade_in && cell_pending) ? gauge_length_int - 1 : gauge_length_int;
    const float anim_alpha  = gauge_update_anim ? (float)gauge_update_anim->attribute : 0.0f;
    const float cell_alpha  = cell_fade_in ? 1.0f - anim_alpha : anim_alpha;

    if (solid_length > 0)
        tex.draw_texture(bar_id, {.x2 = solid_length * bar_width - bar_width});

    if (get_is_rainbow() && rainbow_fade_in.has_value()) {
        const float fade = rainbow_fade_in.value()->attribute;
        const int frame_a = (int)rainbow_frac % 8;
        const int frame_b = (frame_a + 1) % 8;
        const float t = rainbow_frac - (int)rainbow_frac;
        tex.draw_texture(t_dan_rainbow, {.frame = frame_a, .fade = fade});
        tex.draw_texture(t_dan_rainbow, {.frame = frame_b, .fade = fade * t});
    }

    const bool show_gauge_up = cell_fade_in ? cell_pending
                                            : (gauge_length_int <= bar_units && gauge_length_int > previous_length_int);
    if (show_gauge_up && gauge_length_int > 0)
        tex.draw_texture(fade_id, {.x = (gauge_length_int - 1) * bar_width, .fade = cell_alpha});

    tex.draw_texture(t_dan_overlay, {.fade = 0.15f});

    if (get_is_rainbow()) {
        const int f = tamashii_fire_change ? (int)tamashii_fire_change->attribute : 0;
        tex.draw_texture(t_dan_tamashii_fire, {.frame = f, .scale = 0.75f, .center = true});
        tex.draw_texture(t_dan_tamashii, {});
        if (f == 0 || f == 1 || f == 4 || f == 5)
            tex.draw_texture(t_dan_tamashii_overlay, {.fade = 0.5f});
    } else {
        tex.draw_texture(t_dan_tamashii_dark, {});
    }
}
