#include "fireworks.h"
#include "../../libs/texture.h"

Fireworks::Fireworks() {
    explosion_anim = (TextureChangeAnimation*)tex.get_animation(23, true);

    explosion_anim->start();
}

void Fireworks::update(double current_ms) {
    explosion_anim->update(current_ms);
}

void Fireworks::draw() {
}

bool Fireworks::is_finished() {
    return explosion_anim->is_finished;
}
