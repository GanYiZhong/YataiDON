#include "title_idle.h"
#include "../../libs/texture.h"
#include <cmath>
#include <spdlog/spdlog.h>

TitleIdle::TitleIdle() {
    start_ms = get_current_ms();
    map = get_clip_map("title_idle_map");
    if (!map) return;
    table = get_sample_table(map->table);
    if (!table) return;

    col_tx = table->field("tx");
    col_ty = table->field("ty");
    col_sx = table->field("sx");
    col_sy = table->field("sy");
    col_a  = table->field("a");
    if (col_tx < 0 || col_ty < 0 || col_sx < 0 || col_sy < 0 || col_a < 0) {
        spdlog::warn("[title_idle] table {} lacks tx/ty/sx/sy/a - static fallback",
                     map->table);
        return;
    }

    for (const auto& d : map->draws) {
        const SampleTrack* trk = table->track(d.track);
        if (!trk) {
            spdlog::warn("[title_idle] track {} not in table {} - static fallback",
                         d.track, map->table);
            items.clear();
            return;
        }
        if (!tex.has_texture(d.tex)) {
            spdlog::warn("[title_idle] texture {} missing - static fallback", d.tex);
            items.clear();
            return;
        }
        items.push_back({trk, static_cast<uint32_t>(tex.get_enum(d.tex)),
                         d.ox, d.oy});
    }
}

void TitleIdle::draw(double current_ms) {
    if (items.empty()) return;

    double f = (current_ms - start_ms) / table->ms_per_frame();
    // intro once, then the loop window forever (the arcade clip's `loop`
    // label is frame 60; title_idle_map.lua carries both windows).
    if (map->intro1 >= map->intro0) {
        f += map->intro0;
        if (f > map->intro1) {
            double span = map->loop1 - map->loop0 + 1.0;
            f = map->loop0 + std::fmod(f - map->loop0, span);
        }
    } else {
        double span = map->loop1 - map->loop0 + 1.0;
        f = map->loop0 + std::fmod(f - map->loop0, span);
    }

    for (const auto& it : items) {
        // presence gate: a track only exists on the display list inside its
        // own row window (ANIM_COVERAGE.md, ROUND 53 exporter limitation note)
        if (f < it.trk->first_frame() || f > it.trk->last_frame()) continue;
        float a  = static_cast<float>(table->sample(*it.trk, col_a, f));
        if (a <= 0.003f) continue;
        float sx = static_cast<float>(table->sample(*it.trk, col_sx, f));
        float sy = static_cast<float>(table->sample(*it.trk, col_sy, f));
        if (sx <= 0.0f || sy <= 0.0f) continue;
        float tx = static_cast<float>(table->sample(*it.trk, col_tx, f));
        float ty = static_cast<float>(table->sample(*it.trk, col_ty, f));

        // draw_texture dest: pos = base + params.x, w = tex_w*scale + x2,
        // h = tex_h*scale + y2.  scale carries sx; y2 corrects h for sy
        // (h = th*sx + th*(sy-sx) = th*sy), so non-uniform scaling is exact.
        auto tex_it = tex.textures.find(it.tex_id);
        const float th = (tex_it != tex.textures.end())
                             ? static_cast<float>(tex_it->second->height) : 0.0f;
        tex.draw_texture(it.tex_id, {
            .scale = sx,
            .x = tx + sx * it.ox,
            .y = ty + sy * it.oy,
            .y2 = th * (sy - sx),
            .fade = a,
        });
    }
}
