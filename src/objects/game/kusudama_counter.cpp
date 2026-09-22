#include "kusudama_counter.h"

KusudamaCounter::KusudamaCounter(int total)
    : balloon_total(total), balloon_count(0), is_popped(false) {
    move = dynamic_cast<MoveAnimation*>(tex.get_animation(11));
    renda_move = dynamic_cast<MoveAnimation*>(tex.get_animation(13));
    renda_fade_in = dynamic_cast<FadeAnimation*>(tex.get_animation(14));
    renda_fade_out = dynamic_cast<FadeAnimation*>(tex.get_animation(20));
    stretch = dynamic_cast<TextStretchAnimation*>(tex.get_animation(15));
    breathing = dynamic_cast<TextureResizeAnimation*>(tex.get_animation(16));
    renda_breathe = dynamic_cast<MoveAnimation*>(tex.get_animation(17));
    open = dynamic_cast<TextureChangeAnimation*>(tex.get_animation(19));
    fade_out = dynamic_cast<FadeAnimation*>(tex.get_animation(21));

    move->start();
    renda_move->start();
    renda_fade_in->start();
    renda_breathe->start();

    open->reset();
    renda_fade_out->reset();
    fade_out->reset();

    t_kusudama = tex.get_texture("kusudama/kusudama");
    t_renda = tex.get_texture("kusudama/renda");
    t_counter = tex.get_texture("kusudama/counter");
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
    move->update(current_ms);
    renda_move->update(current_ms);
    renda_fade_in->update(current_ms);
    renda_fade_out->update(current_ms);
    fade_out->update(current_ms);
    stretch->update(current_ms);
    breathing->update(current_ms);
    renda_breathe->update(current_ms);
    open->update(current_ms);
    update_count(count);
}

void KusudamaCounter::draw() {
    float y = move->attribute;
    float renda_y = renda_move->attribute + renda_breathe->attribute;
    tex.draw_texture(t_kusudama, {.frame=(int)open->attribute, .scale=(float)breathing->attribute, .center=true, .y=y, .fade=fade_out->attribute});
    tex.draw_texture(t_renda, {.y=renda_y, .fade=std::min(renda_fade_in->attribute, renda_fade_out->attribute)});

    if (move->is_finished && !is_popped) {
        int int_counter = std::max(0, balloon_total - balloon_count);
        if (int_counter == 0) return;
        std::string counter = std::to_string(int_counter);
        const float margin = tex.skin_config[SC::KUSUDAMA_COUNTER_MARGIN].x;
        const float total_width = counter.length() * margin;
        for (size_t i = 0; i < counter.size(); i++) {
            char digit = counter[i];
            tex.draw_texture(t_counter, {.frame=digit - '0', .x=-(total_width / 2.0f) + (i * margin), .y=(float)-stretch->attribute, .y2=(float)stretch->attribute});
        }
    }
}

bool KusudamaCounter::is_finished() const {
    return fade_out->is_finished;
}

bool KusudamaCounter::has_popped() const {
    return is_popped;
}
