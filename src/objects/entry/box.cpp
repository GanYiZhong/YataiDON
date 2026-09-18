#include "box.h"

static constexpr int ENTRY_BOX_MOVE_ANIM_ID = 10;
static constexpr int ENTRY_BOX_OPEN_ANIM_ID = 11;

Box::Box(const std::string& text_str, int font_size, Screens location) : location(location) {
    x = tex.textures[MODE_SELECT::BOX]->x[0];
    y = tex.textures[MODE_SELECT::BOX]->y[0];
    width = tex.textures[MODE_SELECT::BOX]->width;
    move = dynamic_cast<MoveAnimation*>(tex.get_animation(ENTRY_BOX_MOVE_ANIM_ID));
    open = dynamic_cast<MoveAnimation*>(tex.get_animation(ENTRY_BOX_OPEN_ANIM_ID));
    if (move == nullptr || open == nullptr) {
        spdlog::error("Box: animation {} or {} is missing/not a MoveAnimation", ENTRY_BOX_MOVE_ANIM_ID, ENTRY_BOX_OPEN_ANIM_ID);
        return;
    }
    is_selected = false;
    moving_left = false;
    moving_right = false;
    moving_up = false;
    moving_down = false;
    y_pos = 0;
    static_y = 0;
    static_x = x;
    left_x = x;
    static_left = left_x;
    right_x = left_x + tex.textures[MODE_SELECT::BOX]->width - tex.textures[MODE_SELECT::BOX_HIGHLIGHT_RIGHT]->width;
    static_right = right_x;
    if (!load("EntryBox", "box", text_str, font_size)) {
        spdlog::error("Box: failed to load EntryBox/box script; draw() will be a no-op");
        return;
    }
    fn_draw = lua_object["draw"];
}

void Box::set_positions(float x, float y) {
    this->x = x;
    static_x = this->x;
    left_x = this->x;
    static_left = left_x;
    right_x = left_x + tex.textures[MODE_SELECT::BOX]->width - tex.textures[MODE_SELECT::BOX_HIGHLIGHT_RIGHT]->width;
    static_right = right_x;
    this->y_pos = y;
    this->static_y = y;
}

void Box::update(double current_ms, bool is_selected) {
    move->update(current_ms);
    if (moving_left) {
        x = static_x - move->attribute;
    } else if (moving_right) {
        x = static_x + move->attribute;
    } else if (moving_up) {
        y_pos = static_y - move->attribute;
    } else if (moving_down) {
        y_pos = static_y + move->attribute;
    }
    if (move->is_finished) {
        moving_left = false;
        moving_right = false;
        moving_up = false;
        moving_down = false;
        static_x = x;
        static_y = y_pos;
    }
    if (is_selected && !this->is_selected) {
        open->start();
    }
    this->is_selected = is_selected;
    open->update(current_ms);
    if (is_selected) {
        left_x = static_left - open->attribute;
        right_x = static_right + open->attribute;
    } else {
        left_x = static_left;
        right_x = static_right;
    }
}

void Box::move_left() {
    if (!move->is_started) {
        move->start();
    }
    moving_right = moving_up = moving_down = false;
    moving_left = true;
}

void Box::move_right() {
    if (!move->is_started) {
        move->start();
    }
    moving_left = moving_up = moving_down = false;
    moving_right = true;
}

void Box::move_up() {
    if (!move->is_started) {
        move->start();
    }
    moving_left = moving_right = moving_down = false;
    moving_up = true;
}

void Box::move_down() {
    if (!move->is_started) {
        move->start();
    }
    moving_left = moving_right = moving_up = false;
    moving_down = true;
}

void Box::draw(float fade) {
    call(fn_draw, "EntryBox:draw", x, left_x, right_x, is_selected, move->is_finished, (float)open->attribute, fade, y_pos);
}
