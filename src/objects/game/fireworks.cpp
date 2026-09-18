#include "fireworks.h"
#include "../../libs/texture.h"
#include <algorithm>

static constexpr int GOGO_EXPLOSION_ANIM_ID = 23;

Fireworks::Fireworks() {
    explosion_anim = dynamic_cast<TextureChangeAnimation*>(tex.get_animation(GOGO_EXPLOSION_ANIM_ID, true));
    if (!explosion_anim) {
        throw std::runtime_error("Animation " + std::to_string(GOGO_EXPLOSION_ANIM_ID) + " is not a TextureChangeAnimation");
    }

    explosion_anim->start();
}

void Fireworks::update(double current_ms) {
    if (!explosion_anim) return;
    explosion_anim->update(current_ms);
}

void Fireworks::draw() {
    if (!explosion_anim) return;
    if (!explosion_anim->is_finished) {
        int slots = 5;
        int mirror_from = -1;
        constexpr int MAX_EXPLOSION_SLOTS = 32;
        if (const SkinInfo* s = tex.skin_entry("gogo_explosion_slots"); s && s->x > 0) {
            slots = std::clamp(static_cast<int>(s->x), 1, MAX_EXPLOSION_SLOTS);
            if (s->y > 0) mirror_from = std::min(static_cast<int>(s->y), slots);
        }
        for (int i = 0; i < slots; i++) {
            tex.draw_texture(GOGO_TIME::EXPLOSION, {
                .frame = (int)explosion_anim->attribute,
                .mirror = (mirror_from >= 0 && i >= mirror_from) ? Mirror::HORIZONTAL : Mirror::NONE,
                .index = i});
        }
    }
}

bool Fireworks::is_finished() {
    return !explosion_anim || explosion_anim->is_finished;
}
