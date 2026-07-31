#include "kusudama_counter.h"
#include "../../libs/texture.h"

KusudamaCounter::KusudamaCounter(int total)
    : balloon_total(total), balloon_count(0), is_popped(false) {
    move_down = (MoveAnimation*)tex.get_animation(11);
    move_up = (MoveAnimation*)tex.get_animation(12);
    renda_move_up = (MoveAnimation*)tex.get_animation(13);
    renda_move_down = (MoveAnimation*)tex.get_animation(18);
    renda_fade_in = (FadeAnimation*)tex.get_animation(14);
    renda_fade_out = (FadeAnimation*)tex.get_animation(20);
    stretch = (TextStretchAnimation*)tex.get_animation(15);
    breathing = (TextureResizeAnimation*)tex.get_animation(16);
    renda_breathe = (MoveAnimation*)tex.get_animation(17);
    open = (TextureChangeAnimation*)tex.get_animation(19);
    fade_out = (FadeAnimation*)tex.get_animation(21);

    move_down->start();
    move_up->start();
    renda_move_up->start();
    renda_move_down->start();
    renda_fade_in->start();

    open->reset();
    renda_fade_out->reset();
    fade_out->reset();
}

void KusudamaCounter::update_count(int count) {
    if (balloon_count != count) {
        balloon_count = count;
        stretch->start();
        breathing->start();
        if (balloon_count == balloon_total) {
            is_popped = true;
            open->start();
            renda_fade_out->start();
            fade_out->start();
        }
    }
}

void KusudamaCounter::update(double current_ms, int count) {
    move_down->update(current_ms);
    move_up->update(current_ms);
    renda_move_up->update(current_ms);
    renda_move_down->update(current_ms);
    renda_fade_in->update(current_ms);
    renda_fade_out->update(current_ms);
    fade_out->update(current_ms);
    stretch->update(current_ms);
    breathing->update(current_ms);
    renda_breathe->update(current_ms);
    open->update(current_ms);
    if (count != 0) update_count(count);
}

void KusudamaCounter::draw() {
}

bool KusudamaCounter::is_finished() const {
    return fade_out->is_finished;
}
