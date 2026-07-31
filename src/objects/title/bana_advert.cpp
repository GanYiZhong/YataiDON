#include "bana_advert.h"
#include "../../libs/texture.h"
#include <cmath>

BanaAdvertisement::BanaAdvertisement() {
    current_state = BanaAdState::BANA_OPTIONS;
    fade_in = (FadeAnimation*)tex.get_animation(15, true);
    fade_in->start();

    resize_1 = (TextureResizeAnimation*)tex.get_animation(16, true);
    resize_2 = (TextureResizeAnimation*)tex.get_animation(16, true);

    bana_rotate = (TextureResizeAnimation*)tex.get_animation(17, true);
    bana_move = (MoveAnimation*)tex.get_animation(18, true);
    touch_fade = (FadeAnimation*)tex.get_animation(19, true);
    touch_move_up = (MoveAnimation*)tex.get_animation(20, true);
    touch_move_down = (MoveAnimation*)tex.get_animation(22, true);
    touch_resize = (TextureResizeAnimation*)tex.get_animation(21, true);
    touch_fade_out = (FadeAnimation*)tex.get_animation(23, true);

    donchan_move = (MoveAnimation*)tex.get_animation(24, true);

    get_text_scale_down = (TextureResizeAnimation*)tex.get_animation(25, true);
    get_text_scale_up = (TextureResizeAnimation*)tex.get_animation(26, true);
    get_text_scale_down_2 = (TextureResizeAnimation*)tex.get_animation(27, true);
    get_text_fade_in = (FadeAnimation*)tex.get_animation(28, true);
    get_chara_bounce_up = (MoveAnimation*)tex.get_animation(29, true);
    get_chara_bounce_down = (MoveAnimation*)tex.get_animation(30, true);
    get_chara_1_bounce_up = (MoveAnimation*)tex.get_animation(29, true);
    get_chara_1_bounce_down = (MoveAnimation*)tex.get_animation(30, true);
    get_fade_out = (FadeAnimation*)tex.get_animation(31, true);
}

void BanaAdvertisement::update(double current_ms) {
    fade_in->update(current_ms);
    resize_1->update(current_ms);
    resize_2->update(current_ms);

    bana_rotate->update(current_ms);
    bana_move->update(current_ms);
    touch_fade->update(current_ms);
    touch_move_up->update(current_ms);
    touch_move_down->update(current_ms);
    touch_resize->update(current_ms);
    touch_fade_out->update(current_ms);

    donchan_move->update(current_ms);

    get_text_scale_down->update(current_ms);
    get_text_scale_up->update(current_ms);
    get_text_scale_down_2->update(current_ms);
    get_text_fade_in->update(current_ms);
    get_chara_bounce_up->update(current_ms);
    get_chara_bounce_down->update(current_ms);
    get_chara_1_bounce_up->update(current_ms);
    get_chara_1_bounce_down->update(current_ms);
    get_fade_out->update(current_ms);

    if (current_state == BanaAdState::REWARDS) {
        if (rewards_last_ms >= 0.0)
            rewards_angle += (current_ms - rewards_last_ms) * 0.002;
        rewards_last_ms = current_ms;
    }

    switch (steps_cursor) {
        case 0:
            if (fade_in->is_finished) {
                resize_1->start();
                steps_cursor++;
            }
            break;
        case 1:
            if (resize_1->is_finished)  {
                resize_2->start();
                steps_cursor++;
            }
            break;
        case 2:
            if (resize_2->is_finished) {
                resize_1->start();
                steps_cursor++;
            }
            break;
        case 3:
            if (resize_1->is_finished)  {
                resize_2->start();
                steps_cursor++;
            }
            break;
        case 4:
            if (resize_2->is_finished) {
                timer = current_ms;
                steps_cursor++;
            }
            break;
        case 5:
            if (current_ms - timer >= 300) {
                current_state = BanaAdState::TAP_TO_READER;
                steps_cursor++;
                bana_rotate->start();
                bana_move->start();
            }
            break;
        case 6:
            if (bana_rotate->is_finished) {
                touch_fade->start();
                touch_move_up->start();
                touch_move_down->start();
                touch_resize->start();
                steps_cursor++;
            }
            break;
        case 7:
            if (touch_move_down->is_finished) {
                timer = current_ms;
                steps_cursor++;
            }
            break;
        case 8:
            if (current_ms - timer >= 300) {
                touch_fade_out->start();
                steps_cursor++;
            }
            break;
        case 9:
            if (touch_fade_out->is_finished) {
                bana_rotate->start();
                bana_move->start();
                touch_fade->reset();
                touch_move_up->reset();
                touch_move_down->reset();
                touch_resize->reset();
                touch_fade_out->reset();
                steps_cursor++;
            }
            break;
        case 10:
            if (bana_rotate->is_finished) {
                touch_fade->start();
                touch_move_up->start();
                touch_move_down->start();
                touch_resize->start();
                steps_cursor++;
            }
            break;
        case 11:
            if (touch_move_down->is_finished) {
                timer = current_ms;
                steps_cursor++;
            }
            break;
        case 12:
            if (current_ms - timer >= 600) {
                current_state = BanaAdState::REWARDS;
                steps_cursor++;
                loop_counter = 0;
                donchan_move->start();
            }
            break;
        case 13:
            if (loop_counter == 6) {
                steps_cursor++;
                loop_counter = 0;
                resize_1->start();
                current_state = BanaAdState::CARD_OR_PHONE;
            }
            if (donchan_move->is_finished) {
                donchan_move->restart();
                loop_counter++;
            }
            break;
        case 14:
            if (loop_counter == 6) {
                current_state = BanaAdState::GET;
                get_text_scale_down->start();
                get_text_scale_up->start();
                get_text_scale_down_2->start();
                get_text_fade_in->start();
                get_chara_bounce_up->start();
                get_fade_out->start();
                chara_1_timer = current_ms;
                steps_cursor++;
            }
            if (resize_1->is_finished) {
                resize_2->start();
                resize_1->reset();
            }
            if (resize_2->is_finished) {
                resize_1->start();
                resize_2->reset();
                loop_counter++;
            }
            break;
        case 15:
            if (get_fade_out->is_finished) {
                current_state = BanaAdState::BANA_OPTIONS;
                fade_in->start();
                steps_cursor = 0;
            }
            if (get_chara_bounce_up->is_finished && !get_chara_bounce_down->is_started) {
                get_chara_bounce_down->start();
            } else if (get_chara_bounce_down->is_finished) {
                get_chara_bounce_down->reset();
                get_chara_bounce_up->reset();
                get_chara_bounce_up->start();
            }

            if (chara_1_timer >= 0 && current_ms - chara_1_timer >= 116) {
                get_chara_1_bounce_up->start();
                chara_1_timer = -1.0;
            }
            if (get_chara_1_bounce_up->is_finished && !get_chara_1_bounce_down->is_started) {
                get_chara_1_bounce_down->start();
            } else if (get_chara_1_bounce_down->is_finished) {
                get_chara_1_bounce_down->reset();
                get_chara_1_bounce_up->reset();
                get_chara_1_bounce_up->start();
            }
    }
}

void BanaAdvertisement::draw(float x, float y) {}
