#include "box_folder.h"
#include "../../../libs/filesystem.h"
#include "../../../libs/scores.h"
#include "../../../libs/audio.h"

FolderBox::FolderBox(const fs::path& path, const BoxDef& box_def, std::map<std::pair<std::string, std::string>, fs::path>& song_files)
    : BaseBox(path, box_def), tja_count(0)
{
    this->text_name = box_def.name;
    enter_fade = std::make_unique<FadeAnimation>(166);
    refresh_scores(song_files);
}

void FolderBox::refresh_scores(std::map<std::pair<std::string, std::string>, fs::path>& song_files) {
    crown.clear();
    tja_count = 0;
    std::set<int> disqualified;

    auto update_crown = [&](const fs::path& file_path) {
        auto& hashes = scores_manager.get_hashes(file_path);
        for (int diff = 0; diff < 5; diff++) {
            if (hashes[diff].empty()) continue;
            auto score = scores_manager.get_score(hashes[diff], diff, global_data.config->general.player_1_id);

            if (!score || score->crown == Crown::NONE) {
                crown.erase(diff);
                disqualified.insert(diff);
                continue;
            }

            if (disqualified.count(diff)) continue;

            if (crown.find(diff) == crown.end())
                crown[diff] = score->crown;
            else
                crown[diff] = std::min(crown[diff], score->crown);
        }
    };

    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.path().filename() == "song_list.txt") {
            auto entries = read_song_list(entry.path());
            tja_count += (int)entries.size();
            for (const auto& e : entries)
                if (auto found = scores_manager.get_path_by_hash(e.hash))
                    update_crown(*found);
            continue;
        }
        auto ext = entry.path().extension();
        if (ext == ".tja" || ext == ".osu") {
            tja_count++;
            update_crown(entry.path());
        }
    }
}

FolderBox::~FolderBox() = default;

void FolderBox::load_text() {
    BaseBox::load_text();
    hori_name = std::make_unique<OutlinedText>(text_name, tex.skin_config[SC::SONG_HORI_NAME].font_size, ray::WHITE, ray::BLACK, false);
    tja_count_text = std::make_unique<OutlinedText>(std::to_string(tja_count), tex.skin_config[SC::SONG_TJA_COUNT].font_size, ray::WHITE, ray::BLACK, false);
    if (is_osu_folder) {
        auto it = fs::directory_iterator(path);
        while (it->path().extension() != ".jpg" && it->path().extension() != ".png") {
            it++;
        }
        box_texture = ray::LoadTexture((it->path()).string().c_str());
        ray::GenTextureMipmaps(&box_texture.value());
        ray::SetTextureFilter(box_texture.value(), ray::TEXTURE_FILTER_TRILINEAR);
    } else if (fs::exists(fs::path(path / "box.png")) && !box_texture.has_value()) {
        box_texture = ray::LoadTexture((path / "box.png").string().c_str());
        ray::GenTextureMipmaps(&box_texture.value());
        ray::SetTextureFilter(box_texture.value(), ray::TEXTURE_FILTER_TRILINEAR);
    }
    text_loaded = true;
}

void FolderBox::update(double current_time) {
    bool is_open_prev = yellow_box_opened;
    enter_fade->update(current_time);
    BaseBox::update(current_time);

    if (!is_open_prev && yellow_box_opened) {
        if (!audio.is_sound_playing("voice_enter")) {
            audio.play_sound("genre_voice_" + std::to_string((int)genre_index), VolumePreset::VOICE);
        }
    } else if (!yellow_box_opened && audio.is_sound_playing("genre_voice_" + std::to_string((int)genre_index))) {
        audio.stop_sound("genre_voice_" + std::to_string((int)genre_index));
    }
}

void FolderBox::enter_box() {
    entered = true;
    enter_fade->start();
}

void FolderBox::exit_box() {
    entered = false;
    enter_fade->reset();
}

void FolderBox::draw_closed() {}

void FolderBox::draw_open_bg(float fade) {}

void FolderBox::draw_open_fg(float fade) {}

void FolderBox::draw_open() {}
