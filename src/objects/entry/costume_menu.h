#pragma once

#include "../../libs/script.h"
#include "../../libs/global_data.h"
#include "../../libs/text.h"
#include <memory>
#include <vector>
#include <unordered_map>

enum class CostumePickStage { NONE, HEAD, BODY };

class CostumeMenu : public LuaScript {
public:
    PlayerNum player_num;
    bool is_2p = false;
    int selected_index = 0;
    bool costume_select_mode = false;
    int costume_icon_index = 0;

    bool confirmed = false;

    CostumeMenu(PlayerNum player_num);
    ~CostumeMenu();
    void update(double current_time_ms);
    void handle_input();
    void draw(float x = 0.0f, float y = 0.0f);
    std::optional<int> get_index();
    std::string get_costume_name() const;

    CostumePickStage get_pick_stage() const { return pick_stage; }
    int get_picked_head_id() const { return picked_head_id; }

    // --- arcade preset slots (kPreset_Default/Cool/Cute/Joke/Coice) -----------
    // Opt-in: only armed when the skin declares skin_config "costume_preset_slots"
    // with x >= 1. Skins that don't (PyTaikoGreen) keep the old behaviour, where a
    // don on those five rows does nothing at all.
    //
    // The cabinet calls Don.ChangePresetDon(player, preset) on every cursor move of
    // this row, so the Don already wears the preset while the cursor sits on it, and
    // PlayDataManager.DecideCostumeSelect(player, preset) on decide. It reads the
    // outfit out of a 7-rows-per-player table in save data whose populate site was
    // never traced, so the outfit itself is drawn here instead (see ENGINE_BINDINGS).
    std::optional<int> get_preset_cos_id() const { return preset_cos_id; }
    int get_preset_seq() const { return preset_seq; }

private:
    sol::protected_function fn_update;
    sol::protected_function fn_draw_bg;
    sol::protected_function fn_draw_fg;

    std::vector<ray::Texture2D> costume_icons;
    std::vector<int> costume_ids;
    std::unordered_map<int, std::string> costume_names;
    std::unique_ptr<OutlinedText> costume_name_text;
    int costume_name_text_index = -1;

    CostumePickStage pick_stage = CostumePickStage::NONE;
    int picked_head_id = -1;

    void load_costume_icons(const std::string& subdir, const std::string& json_key);

    bool presets_enabled = false;
    std::optional<int> preset_cos_id;      // outfit the highlighted preset dressed the Don in
    int preset_seq = 0;                    // bumps on every (re-)roll; the skin's smoke trigger
    std::vector<int> preset_pool;          // every id in Models/costume_icon
    std::unordered_map<uint32_t, std::vector<int>> preset_sets;  // item -> membership set
    bool preset_data_loaded = false;
    std::unordered_map<uint32_t, int> preset_rolled;  // sticky roll per preset, this visit
    void load_preset_data();
    void apply_preset(uint32_t item);
    static bool is_preset_item(uint32_t item);

    static constexpr int NUM_ITEMS = 7;
    static constexpr std::array<uint32_t, NUM_ITEMS> ITEMS = {
        COSTUME_SELECT::COSTUME,
        COSTUME_SELECT::DEFAULT,
        COSTUME_SELECT::HEAD_BODY,
        COSTUME_SELECT::PRESET_1,
        COSTUME_SELECT::PRESET_2,
        COSTUME_SELECT::PRESET_3,
        COSTUME_SELECT::RANDOM_ITEM,
    };
};
