#include "box_manager.h"
#include "../../libs/global_data.h"

static constexpr int ENTRY_FADE_OUT_ANIM_ID = 9;

BoxManager::BoxManager(bool two_player)
    : selected_box_index(0), is_2p(two_player), costume_menu_open(false) {
    if (!global_data.config) {
        throw std::runtime_error("BoxManager: global_data.config not initialized");
    }
    const std::string lang = global_data.config->general.language;

    fade_out = dynamic_cast<FadeAnimation*>(tex.get_animation(ENTRY_FADE_OUT_ANIM_ID));
    if (!fade_out) {
        throw std::runtime_error("BoxManager: animation " + std::to_string(ENTRY_FADE_OUT_ANIM_ID) + " is not a FadeAnimation");
    }

    build_board_list();
}

void BoxManager::build_board_list() {
    const std::string lang = global_data.config->general.language;
    auto& skin = tex.skin_config;
    int font_size = skin[SC::ENTRY_BOX_TEXT].font_size;

    if (script_manager.lua) (*script_manager.lua)["__hss_entry_reset"] = true;

    boxes.clear();
    box_locations.clear();

    dan_box_index = (dan_available && !is_2p) ? 1 : -1;

    box_locations = {Screens::SONG_SELECT};
    boxes.push_back(std::make_unique<Box>(skin[SC::ENTRY_GAME].text[lang], font_size, Screens::SONG_SELECT));
    if (dan_box_index >= 0) {
        box_locations.push_back(Screens::DAN_SELECT);
        boxes.push_back(std::make_unique<Box>(dan_text, font_size, Screens::DAN_SELECT));
    }
    for (auto [screen, sc] : {std::pair{Screens::PRACTICE_SELECT, SC::ENTRY_PRACTICE},
                              std::pair{Screens::ENTRY,           SC::ENTRY_COSTUME},
                              std::pair{Screens::SETTINGS,        SC::ENTRY_SETTINGS}}) {
        box_locations.push_back(screen);
        boxes.push_back(std::make_unique<Box>(skin[sc].text[lang], font_size, screen));
    }

    num_boxes = boxes.size();

    if (is_2p) {
        for (int i = 0; i < num_boxes; i++) {
            if (box_locations[i] == Screens::SONG_SELECT)
                boxes[i]->location = Screens::SONG_SELECT_2P;
        }
    }

    float spacing_x = skin[SC::ENTRY_BOX_SPACING].x;
    float spacing_y = skin[SC::ENTRY_BOX_SPACING].y;
    is_vertical = spacing_y > 0;

    if (is_vertical) {
        float step = spacing_y;
        float total_height = (num_boxes - 1) * step;
        float start_y = tex.screen_height / 2.0f - total_height / 2.0f;
        float center_x = tex.screen_width / 2.0f - tex.textures["mode_select/box_highlight_center"]->width / 2.0f;

        for (int i = 0; i < num_boxes; i++) {
            boxes[i]->set_positions(center_x, start_y + i * step);
            if (i > 0) {
                boxes[i]->move_down();
            }
        }
    } else {
        float box_width = boxes[0]->width;
        float total_width = num_boxes * box_width + (num_boxes - 1) * spacing_x;
        float start_x = tex.screen_width / 2.0f - total_width / 2.0f;

        for (int i = 0; i < num_boxes; i++) {
            boxes[i]->set_positions(start_x + i * (box_width + spacing_x));
            if (i > 0) {
                boxes[i]->move_right();
            }
        }
    }
}

bool BoxManager::check_board_list_change() const {
    return (dan_available && !is_2p) != (dan_box_index >= 0);
}

void BoxManager::change_board_list() {
    build_board_list();
    selected_box_index = 0;
}

bool BoxManager::selection_allowed() const {
    return !(is_2p && boxes[selected_box_index]->location == Screens::DAN_SELECT);
}

void BoxManager::select_box() {
    if (!selection_allowed()) {
        audio.play_sound("kat", VolumePreset::SOUND);
        return;
    }
    fade_out->start();
}

bool BoxManager::is_box_selected() {
    return fade_out->is_started;
}

bool BoxManager::is_finished() {
    return fade_out->is_finished;
}

bool BoxManager::is_costume_box() {
    return boxes[selected_box_index]->location == Screens::ENTRY;
}

void BoxManager::open_costume_menu(PlayerNum player_num) {
    costume_menu_open = true;
    opening_player = player_num;
}

Screens BoxManager::selected_box() {
    return boxes[selected_box_index]->location;
}

void BoxManager::move_left() {
    int prev_selection = selected_box_index;
    if (boxes[prev_selection]->move->is_started && !boxes[prev_selection]->move->is_finished) {
        return;
    }
    selected_box_index = std::max(0, selected_box_index - 1);
    if (prev_selection == selected_box_index) return;
    if (is_vertical) {
        boxes[selected_box_index + 1]->move_down();
        boxes[selected_box_index]->move_down();
    } else {
        if (selected_box_index + 1 < num_boxes) {
            boxes[selected_box_index + 1]->move_right();
        }
        boxes[selected_box_index]->move_right();
    }
}

void BoxManager::move_right() {
    int prev_selection = selected_box_index;
    if (boxes[prev_selection]->move->is_started && !boxes[prev_selection]->move->is_finished) {
        return;
    }
    selected_box_index = std::min(num_boxes - 1, selected_box_index + 1);
    if (prev_selection == selected_box_index) return;
    if (is_vertical) {
        boxes[selected_box_index - 1]->move_up();
        boxes[selected_box_index]->move_up();
    } else {
        if (selected_box_index != 0) {
            boxes[selected_box_index - 1]->move_left();
        }
        boxes[selected_box_index]->move_left();
    }
}

void BoxManager::update(double current_time_ms, bool is_2p) {
    this->is_2p = is_2p;
    if (!fade_out->is_started && check_board_list_change()) change_board_list();
    for (int i = 0; i < num_boxes; i++) {
        if (box_locations[i] == Screens::SONG_SELECT)
            boxes[i]->location = this->is_2p ? Screens::SONG_SELECT_2P : Screens::SONG_SELECT;
    }
    fade_out->update(current_time_ms);
    for (int i = 0; i < num_boxes; i++) {
        boxes[i]->update(current_time_ms, i == selected_box_index);
    }
}

void BoxManager::draw() {
    for (auto& box : boxes) {
        box->draw(fade_out->attribute);
    }
}
