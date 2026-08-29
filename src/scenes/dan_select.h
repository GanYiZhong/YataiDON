#pragma once

#include "../libs/screen.h"
#include "../objects/song_select/file_navigator/box_dan.h"
#include "../objects/global/allnet_indicator.h"
#include "../objects/global/coin_overlay.h"
#include "../objects/global/indicator.h"
#include "../objects/song_select/modifier.h"
#include <sol/sol.hpp>

class DanNavigator {
public:
    std::vector<std::unique_ptr<DanBox>> boxes;
    int selected_index = 0;

    bool paint_tried = false;
    bool paint_ok    = false;
    sol::table lua_paint;
    sol::protected_function fn_draw_cursor;
    void load_paint_surface();

    double last_moved = 0;

    void init(const std::vector<fs::path>& song_paths);
    void move_left();
    void move_right();
    void skip(int delta);
    DanBox* get_current();
    void update(double current_ms);
    void draw();

private:
    static constexpr float BOX_CENTER = 594.0f;
    static constexpr float BASE_SPACING = 150.0f;
    static constexpr float SIDE_OFFSET_L = 200.0f;
    static constexpr float SIDE_OFFSET_R = 500.0f;

    struct RibbonLayout {
        bool  legacy  = true;
        float center  = BOX_CENTER;
        float spacing = BASE_SPACING;
        float side_l  = SIDE_OFFSET_L;
        float side_r  = SIDE_OFFSET_R;
    };
    RibbonLayout ribbon_layout() const;

    void set_positions(bool init, float duration);

    int total_notes_for(const std::vector<DanSongEntry>& songs);

    Exam parse_exam(const rapidjson::Value& e);

    std::optional<DanSongEntry> load_song_entry(const rapidjson::Value& chart);

    std::unique_ptr<DanBox> load_dan_box(const fs::path& json_path);
};

class DanSelectScreen : public Screen {
public:
    DanSelectScreen() : Screen("dan_select") {}

    void on_screen_start() override;
    Screens on_screen_end(Screens next_screen) override;
    std::optional<Screens> update() override;
    void draw() override;

private:
    DanNavigator dan_navigator;
    CoinOverlay coin_overlay;
    AllNetIcon allnet_indicator;
    std::unique_ptr<Indicator> indicator;
    SongSelectState state = SongSelectState::BROWSING;

    enum ConfirmEntry { CONFIRM_OPTION = 0, CONFIRM_YES = 1, CONFIRM_NO = 2 };
    int confirm_index = CONFIRM_NO;
    FadeAnimation* confirm_fade = nullptr;

    double confirm_opened_at = 0;

    PlayerData dan_player_data;
    std::optional<ModifierSelector> modifier_selector;

    double last_moved = 0;

    bool wheel_locked = false;
    double wheel_tick_epoch = 0;
    long long wheel_tick_seen = -1;

    void handle_input_browsing(double current_ms);
    std::optional<Screens> handle_input_selected();

    void draw_confirm_overlay();
};
