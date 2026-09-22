#pragma once

#include "../../libs/texture.h"
#include "../../libs/screen.h"
#include <algorithm>
#include <cmath>
#include <map>

class DebugMenu {
public:
    static const int TAB_COUNT = 4;
    static constexpr float PANEL_WIDTH      = 360.0f;
    static constexpr float TAB_HEIGHT       = 36.0f;
    static constexpr float ROW_HEIGHT       = 20.0f;
    static constexpr float SCROLLBAR_WIDTH  = 6.0f;
    static constexpr float EDIT_ROW_HEIGHT   = 24.0f;
    static constexpr float VERDICT_TOP       = 44.0f;
    static constexpr float VERDICT_HEIGHT    = 34.0f;
    static constexpr float EDIT_FIELDS_TOP   = VERDICT_TOP + VERDICT_HEIGHT + 4.0f;
    static constexpr float EDIT_PANEL_HEIGHT = EDIT_FIELDS_TOP + 4 * EDIT_ROW_HEIGHT + 6.0f;
    static constexpr float FRAME_CELL_WIDTH      = 56.0f;
    static constexpr float FRAME_THUMB_SIZE      = 40.0f;
    static constexpr float FRAME_BTN_ROW_HEIGHT  = 20.0f;
    static constexpr float FRAMES_HEADER_HEIGHT  = 20.0f;
    static constexpr float FRAME_ROW_HEIGHT      = FRAME_THUMB_SIZE + 4.0f + FRAME_BTN_ROW_HEIGHT;

    bool open = false;
    int active_tab = 0;
    int hovered_log_index = -1;
    int scroll_offset = 0;

    bool has_selection = false;
    std::string selected_name;
    int selected_tex_index = 0;
    int selected_log_index = -1;

    int editing_field = -1;
    std::string edit_buffer;

    std::optional<Screens> requested_screen;

    static constexpr Screens ALL_SCREENS[] = {
        Screens::TITLE, Screens::ENTRY, Screens::SONG_SELECT, Screens::GAME, Screens::GAME_2P,
        Screens::RESULT, Screens::RESULT_2P, Screens::SONG_SELECT_2P, Screens::DAN_SELECT,
        Screens::GAME_DAN, Screens::DAN_RESULT, Screens::PRACTICE_SELECT, Screens::GAME_PRACTICE,
        Screens::SETTINGS, Screens::LOADING, Screens::INPUT_CALI, Screens::GAME_OVER, Screens::INPUT_TEST
    };

    void clear_selection() {
        commit_edit();
        has_selection = false;
        selected_name.clear();
        selected_log_index = -1;
    }

    FramedTexture* get_selected_framed() const {
        return dynamic_cast<FramedTexture*>(selected_obj());
    }

    static int frame_grid_cols() { return std::max(1, (int)(PANEL_WIDTH / FRAME_CELL_WIDTH)); }

    static float frames_section_height(int frame_count) {
        int rows = (frame_count + frame_grid_cols() - 1) / frame_grid_cols();
        return FRAMES_HEADER_HEIGHT + rows * FRAME_ROW_HEIGHT;
    }

    float edit_panel_height() const {
        FramedTexture* framed = get_selected_framed();
        if (framed && framed->frame_count() > 1) return EDIT_PANEL_HEIGHT + frames_section_height(framed->frame_count());
        return EDIT_PANEL_HEIGHT;
    }

    void commit_edit() {
        if (editing_field < 0) return;
        int* value = field_ptr(editing_field);
        if (value && !edit_buffer.empty() && edit_buffer != "-") {
            try { *value = std::stoi(edit_buffer); } catch (...) {}
        }
        editing_field = -1;
        edit_buffer.clear();
    }

    void update(const ray::Camera2D& camera) {
        if (ray::IsKeyPressed(ray::KEY_F7)) {
            open = !open;
            if (open) ray::ShowCursor(); else ray::HideCursor();
        }

        debug_draw_log_prev.swap(debug_draw_log);
        debug_draw_log.clear();

        const bool textures_tab_active = open && active_tab == 0;
        debug_log_draws = textures_tab_active;
        if (!open) { commit_edit(); return; }

        const float panel_x   = tex.screen_width - PANEL_WIDTH;
        const float tab_width = PANEL_WIDTH / TAB_COUNT;
        const ray::Vector2 mouse = ray::GetScreenToWorld2D(ray::GetMousePosition(), camera);
        const bool clicked = ray::IsMouseButtonPressed(ray::MOUSE_BUTTON_LEFT);
        const bool mouse_over_panel = mouse.x >= panel_x;

        if (clicked) commit_edit();

        if (editing_field < 0 && ray::IsKeyPressed(ray::KEY_TAB)) active_tab = (active_tab + 1) % TAB_COUNT;

        if (clicked && mouse_over_panel && mouse.y < TAB_HEIGHT) {
            int hit = (int)((mouse.x - panel_x) / tab_width);
            if (hit >= 0 && hit < TAB_COUNT) active_tab = hit;
        }

        if (active_tab == 1) {
            if (clicked && mouse_over_panel && mouse.y >= TAB_HEIGHT) {
                int row = (int)((mouse.y - TAB_HEIGHT) / ROW_HEIGHT);
                if (row >= 0 && row < (int)std::size(ALL_SCREENS)) requested_screen = ALL_SCREENS[row];
            }
            return;
        }

        if (!textures_tab_active) return;

        rebuild_visible_rows();

        const float list_top    = TAB_HEIGHT;
        const float list_bottom = tex.screen_height - edit_panel_height();
        const int   row_count   = (int)visible_rows.size();
        const int   rows_shown  = std::max(0, (int)((list_bottom - list_top) / ROW_HEIGHT));
        const int   max_scroll  = std::max(0, row_count - rows_shown);
        const bool  mouse_in_list = mouse_over_panel && mouse.y >= list_top && mouse.y < list_bottom;

        if (mouse_in_list) {
            float wheel = ray::GetMouseWheelMove();
            if (wheel != 0.0f) scroll_offset -= (int)(wheel * 3.0f);
        }
        scroll_offset = std::clamp(scroll_offset, 0, max_scroll);

        hovered_log_index = -1;
        if (!mouse_over_panel) {
            for (int i = (int)debug_draw_log_prev.size() - 1; i >= 0; i--) {
                const ray::Rectangle& r = debug_draw_log_prev[i].rect;
                if (mouse.x >= r.x && mouse.x <= r.x + r.width &&
                    mouse.y >= r.y && mouse.y <= r.y + r.height) {
                    hovered_log_index = i;
                    break;
                }
            }
        }

        if (clicked && mouse_in_list) {
            int row = (int)((mouse.y - list_top) / ROW_HEIGHT) + scroll_offset;
            if (row >= 0 && row < row_count) {
                const VisualRow& vr = visible_rows[row];
                if (vr.is_header) {
                    if (expanded_subsets.count(vr.subset)) expanded_subsets.erase(vr.subset);
                    else expanded_subsets.insert(vr.subset);
                } else {
                    const DrawLogEntry& e = debug_draw_log_prev[vr.log_index];
                    has_selection = true;
                    selected_name = e.name;
                    selected_tex_index = e.index;
                    selected_log_index = vr.log_index;
                }
            }
        }

        if (clicked && has_selection && mouse_over_panel && mouse.y >= list_bottom) {
            const int step = ray::IsKeyDown(ray::KEY_LEFT_SHIFT) ? 10 : 1;
            for (int i = 0; i < 4; i++) {
                int* value = field_ptr(i);
                if (!value) continue;
                FieldButtons b = field_buttons(i, panel_x, list_bottom);
                if (in_rect(mouse, b.minus)) *value -= step;
                else if (in_rect(mouse, b.plus)) *value += step;
                else if (in_rect(mouse, b.value)) {
                    editing_field = i;
                    edit_buffer = std::to_string(*value);
                }
            }
        }

        if (FramedTexture* framed = get_selected_framed()) {
            int frame_count = framed->frame_count();
            if (clicked && frame_count > 1 && mouse_over_panel && mouse.y >= list_bottom + EDIT_PANEL_HEIGHT) {
                const float frames_top = list_bottom + EDIT_PANEL_HEIGHT;
                for (int idx = 0; idx < frame_count; idx++) {
                    FrameCellButtons b = frame_cell_buttons(idx, panel_x, frames_top);
                    if (in_rect(mouse, b.left) && idx > 0) {
                        std::swap(framed->textures[idx], framed->textures[idx - 1]);
                        break;
                    } else if (in_rect(mouse, b.right) && idx < frame_count - 1) {
                        std::swap(framed->textures[idx], framed->textures[idx + 1]);
                        break;
                    }
                }
            }
        }

        if (editing_field >= 0) {
            if (ray::IsKeyPressed(ray::KEY_ESCAPE)) {
                editing_field = -1;
                edit_buffer.clear();
            } else if (ray::IsKeyPressed(ray::KEY_ENTER)) {
                commit_edit();
            } else {
                if (ray::IsKeyPressed(ray::KEY_BACKSPACE) && !edit_buffer.empty()) edit_buffer.pop_back();
                int ch;
                while ((ch = ray::GetCharPressed()) > 0) {
                    bool is_digit = ch >= '0' && ch <= '9';
                    bool is_sign  = ch == '-' && edit_buffer.empty();
                    if ((is_digit || is_sign) && edit_buffer.size() < 8) edit_buffer += (char)ch;
                }
            }
        }
    }

    void draw() {
        if (!open) return;

        const float screen_w  = (float)tex.screen_width;
        const float screen_h  = (float)tex.screen_height;
        const float panel_x   = screen_w - PANEL_WIDTH;
        const float tab_width = PANEL_WIDTH / TAB_COUNT;

        ray::DrawRectangle((int)panel_x, 0, (int)PANEL_WIDTH, (int)screen_h, ray::Fade(ray::BLACK, 0.85f));

        static const char* tab_labels[TAB_COUNT] = {"Textures", "Scenes", "", ""};
        for (int i = 0; i < TAB_COUNT; i++) {
            float tab_x = panel_x + i * tab_width;
            ray::Color tab_color = (i == active_tab) ? ray::Fade(ray::WHITE, 0.3f) : ray::Fade(ray::WHITE, 0.1f);
            ray::DrawRectangle((int)tab_x, 0, (int)tab_width, (int)TAB_HEIGHT, tab_color);
            ray::DrawRectangleLines((int)tab_x, 0, (int)tab_width, (int)TAB_HEIGHT, ray::Fade(ray::WHITE, 0.4f));

            const char* label = tab_labels[i];
            int label_w = ray::MeasureText(label, 16);
            int label_x = (int)(tab_x + (tab_width - label_w) * 0.5f);
            int label_y = (int)((TAB_HEIGHT - 16) * 0.5f);
            ray::DrawText(label, label_x, label_y, 16, ray::WHITE);
        }

        if (active_tab == 0) draw_textures_tab(panel_x, screen_h);
        else if (active_tab == 1) draw_scenes_tab(panel_x);
    }

private:
    void draw_scenes_tab(float panel_x) {
        for (size_t i = 0; i < std::size(ALL_SCREENS); i++) {
            float row_y = TAB_HEIGHT + i * ROW_HEIGHT;
            std::string name = screens_to_string(ALL_SCREENS[i]);
            bool is_current = name == global_data.current_screen;
            bool is_pending = requested_screen.has_value() && *requested_screen == ALL_SCREENS[i];

            if (is_current) {
                ray::DrawRectangle((int)panel_x, (int)row_y, (int)PANEL_WIDTH, (int)ROW_HEIGHT, ray::Fade(ray::SKYBLUE, 0.35f));
            } else if (is_pending) {
                ray::DrawRectangle((int)panel_x, (int)row_y, (int)PANEL_WIDTH, (int)ROW_HEIGHT, ray::Fade(ray::YELLOW, 0.3f));
            }
            ray::Color color = is_current ? ray::SKYBLUE : (is_pending ? ray::YELLOW : ray::WHITE);
            ray::DrawText(name.c_str(), (int)panel_x + 4, (int)row_y + 3, 14, color);
        }
    }

    bool is_selected_entry(const DrawLogEntry& entry) const {
        return entry.name == selected_name && entry.index == selected_tex_index;
    }

    const DrawLogEntry* find_selected_entry() const {
        if (!has_selection) return nullptr;
        if (selected_log_index >= 0 && selected_log_index < (int)debug_draw_log_prev.size()) {
            const DrawLogEntry& entry = debug_draw_log_prev[selected_log_index];
            if (is_selected_entry(entry)) return &entry;
        }
        for (const DrawLogEntry& entry : debug_draw_log_prev)
            if (is_selected_entry(entry)) return &entry;
        return nullptr;
    }

    TextureObject* selected_obj() const {
        const DrawLogEntry* entry = find_selected_entry();
        return entry ? entry->tex_obj : nullptr;
    }

    enum class Position { Json, JsonPlusOffset, NoJson, Unknown };

    static Position position_of(const DrawLogEntry& entry) {
        if (!entry.tex_obj) return Position::NoJson;
        if (entry.origin.x != 0 || entry.origin.y != 0 || entry.rotation != 0) return Position::Unknown;
        if (entry.offset_x == 0 && entry.offset_y == 0 && !(entry.center && entry.scale != 1.0f)) return Position::Json;
        return Position::JsonPlusOffset;
    }

    void draw_verdict(const DrawLogEntry& entry, float panel_x, float top) {
        ray::Color color = ray::GRAY;
        std::string headline, advice;
        switch (position_of(entry)) {
            case Position::Json:
                color = ray::GREEN;
                headline = "position comes from texture.json";
                break;
            case Position::JsonPlusOffset:
                color = ray::ORANGE;
                headline = ray::TextFormat("caller adds x%+.0f y%+.0f to the json base", entry.offset_x, entry.offset_y);
                break;
            case Position::NoJson:
                color = ray::RED;
                headline = "no texture.json behind this draw";
                advice = "x/y here do nothing; the position is set in code";
                break;
            case Position::Unknown:
                headline = "origin/rotation in use";
                advice = "the box and these numbers are approximate";
                break;
        }
        if (advice.empty())
            advice = entry.scale != 1.0f
                ? ray::TextFormat("x/y move it 1:1; x2/y2 change by x%.2f", entry.scale)
                : "x/y move it 1:1 whatever the caller adds";
        ray::DrawRectangle((int)panel_x, (int)top, (int)PANEL_WIDTH, (int)VERDICT_HEIGHT, ray::Fade(color, 0.3f));
        ray::DrawText(headline.c_str(), (int)panel_x + 6, (int)top + 3, 12, ray::WHITE);
        ray::DrawText(advice.c_str(), (int)panel_x + 6, (int)top + 18, 12, ray::Fade(ray::WHITE, 0.75f));
    }

    struct FieldButtons { ray::Rectangle minus, plus, value; };

    struct VisualRow {
        bool is_header;
        std::string subset;
        std::string label;
        int log_index = -1;
    };
    std::vector<VisualRow> visible_rows;
    std::unordered_set<std::string> expanded_subsets;

    static std::pair<std::string, std::string> split_subset(const DrawLogEntry& e) {
        if (e.tex_obj) {
            size_t slash = e.name.find('/');
            if (slash != std::string::npos) return {e.name.substr(0, slash), e.name.substr(slash + 1)};
        }
        return {"[lua]", e.name};
    }

    void rebuild_visible_rows() {
        visible_rows.clear();
        std::map<std::string, std::vector<int>> groups;
        for (int i = 0; i < (int)debug_draw_log_prev.size(); i++) {
            groups[split_subset(debug_draw_log_prev[i]).first].push_back(i);
        }
        for (auto& [subset, indices] : groups) {
            std::sort(indices.begin(), indices.end(), [](int a, int b) {
                return split_subset(debug_draw_log_prev[a]).second < split_subset(debug_draw_log_prev[b]).second;
            });
            visible_rows.push_back({true, subset, subset + " (" + std::to_string(indices.size()) + ")", -1});
            if (expanded_subsets.count(subset)) {
                for (int idx : indices) {
                    visible_rows.push_back({false, "", split_subset(debug_draw_log_prev[idx]).second, idx});
                }
            }
        }
    }

    static bool in_rect(ray::Vector2 p, const ray::Rectangle& r) {
        return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
    }

    int* field_ptr(int field_idx) {
        TextureObject* obj = selected_obj();
        if (!obj) return nullptr;
        size_t idx = (size_t)selected_tex_index;
        switch (field_idx) {
            case 0: return idx < obj->x.size()  ? &obj->x[idx]  : nullptr;
            case 1: return idx < obj->y.size()  ? &obj->y[idx]  : nullptr;
            case 2: return idx < obj->x2.size() ? &obj->x2[idx] : nullptr;
            case 3: return idx < obj->y2.size() ? &obj->y2[idx] : nullptr;
        }
        return nullptr;
    }

    static const char* field_label(int field_idx) {
        static const char* labels[4] = {"x", "y", "x2", "y2"};
        return labels[field_idx];
    }

    static FieldButtons field_buttons(int field_idx, float panel_x, float edit_top) {
        float row_y = edit_top + EDIT_FIELDS_TOP + field_idx * EDIT_ROW_HEIGHT;
        float btn_size = EDIT_ROW_HEIGHT - 6.0f;
        ray::Rectangle minus = {panel_x + 40, row_y + 3, btn_size, btn_size};
        ray::Rectangle value = {minus.x + btn_size + 6, row_y + 2, 60.0f, btn_size + 2};
        ray::Rectangle plus  = {value.x + value.width + 6, row_y + 3, btn_size, btn_size};
        return {minus, plus, value};
    }

    struct FrameCellButtons { ray::Rectangle left, right; };

    static FrameCellButtons frame_cell_buttons(int idx, float panel_x, float frames_top) {
        int cols = frame_grid_cols();
        float cell_x = panel_x + (idx % cols) * FRAME_CELL_WIDTH;
        float row_y  = frames_top + FRAMES_HEADER_HEIGHT + (idx / cols) * FRAME_ROW_HEIGHT;
        float btn_row_y = row_y + FRAME_THUMB_SIZE + 4.0f;
        float btn_size = FRAME_BTN_ROW_HEIGHT - 4.0f;
        ray::Rectangle left  = {cell_x + 2, btn_row_y, btn_size, btn_size};
        ray::Rectangle right = {cell_x + FRAME_CELL_WIDTH - btn_size - 2, btn_row_y, btn_size, btn_size};
        return {left, right};
    }

    const ray::Rectangle* find_selected_rect() const {
        const DrawLogEntry* entry = find_selected_entry();
        return entry ? &entry->rect : nullptr;
    }

    void draw_textures_tab(float panel_x, float screen_h) {
        const float list_top    = TAB_HEIGHT;
        const float list_bottom = screen_h - edit_panel_height();
        const int   row_count   = (int)visible_rows.size();
        const int   rows_shown  = std::max(0, (int)((list_bottom - list_top) / ROW_HEIGHT));

        const DrawLogEntry* selected = find_selected_entry();

        ray::BeginScissorMode((int)panel_x, (int)list_top, (int)PANEL_WIDTH, (int)(list_bottom - list_top));
        for (int row = 0; row < rows_shown; row++) {
            int ridx = row + scroll_offset;
            if (ridx >= row_count) break;
            const VisualRow& vr = visible_rows[ridx];
            float row_y = list_top + row * ROW_HEIGHT;

            if (vr.is_header) {
                bool expanded = expanded_subsets.count(vr.subset) != 0;
                ray::DrawRectangle((int)panel_x, (int)row_y, (int)PANEL_WIDTH, (int)ROW_HEIGHT, ray::Fade(ray::WHITE, 0.12f));
                ray::DrawText(expanded ? "v" : ">", (int)panel_x + 4, (int)row_y + 3, 14, ray::WHITE);
                ray::DrawText(vr.label.c_str(), (int)panel_x + 18, (int)row_y + 3, 14, ray::WHITE);
                continue;
            }

            const DrawLogEntry& e = debug_draw_log_prev[vr.log_index];
            bool is_hovered  = (vr.log_index == hovered_log_index);
            bool is_selected = (&e == selected);
            if (is_selected) {
                ray::DrawRectangle((int)panel_x, (int)row_y, (int)PANEL_WIDTH, (int)ROW_HEIGHT, ray::Fade(ray::SKYBLUE, 0.35f));
            } else if (is_hovered) {
                ray::DrawRectangle((int)panel_x, (int)row_y, (int)PANEL_WIDTH, (int)ROW_HEIGHT, ray::Fade(ray::YELLOW, 0.3f));
            }
            ray::Color color = is_selected ? ray::SKYBLUE : (is_hovered ? ray::YELLOW : ray::WHITE);
            ray::DrawText(vr.label.c_str(), (int)panel_x + 20, (int)row_y + 3, 14, color);
        }
        ray::EndScissorMode();

        if (row_count > rows_shown) {
            float track_x = panel_x + PANEL_WIDTH - SCROLLBAR_WIDTH;
            ray::DrawRectangle((int)track_x, (int)list_top, (int)SCROLLBAR_WIDTH, (int)(list_bottom - list_top), ray::Fade(ray::WHITE, 0.1f));
            float list_height = list_bottom - list_top;
            float thumb_h = std::max(12.0f, list_height * ((float)rows_shown / row_count));
            int max_scroll = row_count - rows_shown;
            float thumb_y = list_top + (max_scroll > 0 ? (scroll_offset / (float)max_scroll) * (list_height - thumb_h) : 0.0f);
            ray::DrawRectangle((int)track_x, (int)thumb_y, (int)SCROLLBAR_WIDTH, (int)thumb_h, ray::Fade(ray::WHITE, 0.5f));
        }

        const ray::Rectangle* box_rect = nullptr;
        std::string box_name;
        if (hovered_log_index >= 0) {
            box_rect = &debug_draw_log_prev[hovered_log_index].rect;
            box_name = debug_draw_log_prev[hovered_log_index].name;
        } else if (const ray::Rectangle* r = find_selected_rect()) {
            box_rect = r;
            box_name = selected_name;
        }
        if (box_rect) {
            ray::DrawRectangleLinesEx(*box_rect, 2.0f, ray::YELLOW);
            float label_y = box_rect->y - 20.0f;
            if (label_y < 0) label_y = box_rect->y + box_rect->height + 2.0f;
            int label_w = ray::MeasureText(box_name.c_str(), 18);
            ray::DrawRectangle((int)box_rect->x - 2, (int)label_y - 2, label_w + 4, 22, ray::Fade(ray::BLACK, 0.8f));
            ray::DrawText(box_name.c_str(), (int)box_rect->x, (int)label_y, 18, ray::YELLOW);
        }

        draw_edit_panel(panel_x, list_bottom);
    }

    void draw_edit_panel(float panel_x, float edit_top) {
        ray::DrawRectangle((int)panel_x, (int)edit_top, (int)PANEL_WIDTH, (int)edit_panel_height(), ray::Fade(ray::WHITE, 0.05f));
        ray::DrawLine((int)panel_x, (int)edit_top, (int)(panel_x + PANEL_WIDTH), (int)edit_top, ray::Fade(ray::WHITE, 0.4f));

        if (!has_selection) {
            ray::DrawText("Click a texture to select it", (int)panel_x + 8, (int)edit_top + 8, 14, ray::GRAY);
            return;
        }

        ray::DrawText(selected_name.c_str(), (int)panel_x + 8, (int)edit_top + 8, 16, ray::WHITE);

        const DrawLogEntry* entry = find_selected_entry();
        if (!entry) {
            ray::DrawText("(not drawn this frame)", (int)panel_x + 8, (int)edit_top + 26, 14, ray::GRAY);
            return;
        }
        draw_verdict(*entry, panel_x, edit_top + VERDICT_TOP);

        TextureObject* obj = entry->tex_obj;
        if (!obj) return;

        const char* info = ray::TextFormat("%dx%d px, %d frame(s)", obj->width,
                                            obj->height, obj->frame_count());
        ray::DrawText(info, (int)panel_x + 8, (int)edit_top + 26, 14, ray::GRAY);

        for (int i = 0; i < 4; i++) {
            int* value = field_ptr(i);
            if (!value) continue;
            FieldButtons b = field_buttons(i, panel_x, edit_top);
            float row_y = edit_top + EDIT_FIELDS_TOP + i * EDIT_ROW_HEIGHT;

            ray::DrawText(field_label(i), (int)panel_x + 8, (int)row_y + 4, 14, ray::WHITE);

            ray::DrawRectangleRec(b.minus, ray::Fade(ray::WHITE, 0.2f));
            ray::DrawText("-", (int)b.minus.x + 6, (int)b.minus.y + 1, 16, ray::WHITE);
            ray::DrawRectangleRec(b.plus, ray::Fade(ray::WHITE, 0.2f));
            ray::DrawText("+", (int)b.plus.x + 5, (int)b.plus.y + 1, 16, ray::WHITE);

            bool is_editing = (editing_field == i);
            ray::DrawRectangleRec(b.value, ray::Fade(ray::WHITE, is_editing ? 0.25f : 0.1f));
            ray::DrawRectangleLinesEx(b.value, 1.0f, is_editing ? ray::SKYBLUE : ray::Fade(ray::WHITE, 0.4f));

            std::string text = is_editing ? edit_buffer : std::to_string(*value);
            if (is_editing && std::fmod(ray::GetTime(), 1.0) < 0.5) text += "|";
            ray::DrawText(text.c_str(), (int)b.value.x + 4, (int)b.value.y + 4, 14, ray::WHITE);
        }

        draw_frames_section(panel_x, edit_top);
    }

    void draw_frames_section(float panel_x, float edit_top) {
        FramedTexture* framed = get_selected_framed();
        if (!framed || framed->frame_count() <= 1) return;

        const int frame_count = framed->frame_count();
        const float frames_top = edit_top + EDIT_PANEL_HEIGHT;
        const int cols = frame_grid_cols();

        ray::DrawText(ray::TextFormat("Frames (%d)", frame_count), (int)panel_x + 8, (int)frames_top + 2, 14, ray::WHITE);

        for (int idx = 0; idx < frame_count; idx++) {
            float cell_x = panel_x + (idx % cols) * FRAME_CELL_WIDTH;
            float row_y  = frames_top + FRAMES_HEADER_HEIGHT + (idx / cols) * FRAME_ROW_HEIGHT;

            ray::Rectangle thumb_bg = {cell_x + (FRAME_CELL_WIDTH - FRAME_THUMB_SIZE) * 0.5f, row_y,
                                        FRAME_THUMB_SIZE, FRAME_THUMB_SIZE};
            ray::DrawRectangleRec(thumb_bg, ray::Fade(ray::BLACK, 0.5f));
            const ray::Texture2D& t = framed->textures[idx];
            ray::Rectangle src = {0, 0, (float)t.width, (float)t.height};
            ray::DrawTexturePro(t, src, thumb_bg, {0, 0}, 0, ray::WHITE);
            ray::DrawRectangleLinesEx(thumb_bg, 1.0f, ray::Fade(ray::WHITE, 0.4f));

            FrameCellButtons b = frame_cell_buttons(idx, panel_x, frames_top);
            ray::DrawRectangleRec(b.left, ray::Fade(ray::WHITE, idx > 0 ? 0.2f : 0.05f));
            ray::DrawText("<", (int)b.left.x + 4, (int)b.left.y, 14, idx > 0 ? ray::WHITE : ray::GRAY);

            const char* index_text = ray::TextFormat("%d", idx);
            int index_w = ray::MeasureText(index_text, 14);
            ray::DrawText(index_text, (int)(cell_x + (FRAME_CELL_WIDTH - index_w) * 0.5f), (int)b.left.y + 2, 14, ray::WHITE);

            ray::DrawRectangleRec(b.right, ray::Fade(ray::WHITE, idx < frame_count - 1 ? 0.2f : 0.05f));
            ray::DrawText(">", (int)b.right.x + 4, (int)b.right.y, 14, idx < frame_count - 1 ? ray::WHITE : ray::GRAY);
        }
    }
};

inline DebugMenu debug_menu;
