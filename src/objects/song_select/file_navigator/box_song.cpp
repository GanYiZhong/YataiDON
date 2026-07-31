#include "box_song.h"
#include "../../../libs/audio.h"

SongBox::SongBox(const fs::path& path, const BoxDef& box_def, SongParser parser)
    : BaseBox(path, box_def)
{
    this->parser = parser;
    parser.get_metadata();
    auto& titles = parser.metadata.title;
    const std::string& lang = global_data.config->general.language;
    text_name = titles.count(lang) ? titles.at(lang) : titles.count("en") ? titles.at("en") : titles.empty() ? "" : titles.begin()->second;

    auto& subtitles = parser.metadata.subtitle;
    text_subtitle = subtitles.count(lang) ? subtitles.at(lang) : subtitles.count("en") ? subtitles.at("en") : subtitles.empty() ? "" : subtitles.begin()->second;

    is_favorite = false;
    diff_fade_in = (FadeAnimation*)tex.get_animation(12);
    refresh_scores();
}

void SongBox::refresh_scores() {
    hashes = scores_manager.get_hashes(path);
    for (const auto& [course, course_data] : parser.metadata.course_data) {
        if (course < 0 || course >= static_cast<int>(hashes.size()))
            continue;
        if (hashes[course].empty())
            hashes[course] = parser.get_diff_hash(course);
    }
    for (int i = 0; i < 5; i++) {
        scores[i] = scores_manager.get_score(hashes[i], i, global_data.config->general.player_1_id);
    }
    score_history.reset();
}

void SongBox::reset() {
    BaseBox::reset();
    diff_fade_in = (FadeAnimation*)tex.get_animation(12);
    if (audio.is_music_stream_valid("preview")) {
        audio.unload_music_stream("preview");
    }
    music_playing = false;
    score_history.reset();
    box_opened_at = 0.0;
}

std::vector<Difficulty> SongBox::get_diffs() {
    std::vector<Difficulty> diffs;
    for (const auto& [diff, level] : parser.metadata.course_data) {
        diffs.push_back(Difficulty(diff));
    }
    return diffs;
}

void SongBox::load_text() {
    BaseBox::load_text();
    float font_size = utf8_char_count(text_subtitle) < 30
        ? tex.skin_config[SC::YB_SUBTITLE].font_size
        : tex.skin_config[SC::YB_SUBTITLE].font_size - (int)(10 * tex.screen_scale);
    subtitle = make_unique<OutlinedText>(text_subtitle, font_size, ray::WHITE, ray::BLACK, true);

    font_size = tex.skin_config[SC::SONG_BOX_NAME].font_size;
    if (utf8_char_count(text_name) >= 30)
        font_size -= (int)(10 * tex.screen_scale);
    name_black = make_unique<OutlinedText>(text_name, font_size, ray::WHITE, ray::BLACK, true);
    bpm_text = make_unique<OutlinedText>("BPM\n" + std::to_string(static_cast<int>(parser.metadata.bpm)), tex.skin_config[SC::SONG_BOX_BPM].font_size, ray::WHITE, ray::BLACK, false);
    if (exists(parser.metadata.preimage)) {
        preimage = ray::LoadTexture(parser.metadata.preimage.string().c_str());
        ray::GenTextureMipmaps(&preimage.value());
        ray::SetTextureFilter(preimage.value(), ray::TEXTURE_FILTER_TRILINEAR);
    }
    text_loaded = true;
}

void SongBox::update(double current_time) {
    BaseBox::update(current_time);
    diff_fade_in->update(current_time);

    if (yellow_box.has_value() && (yellow_box->left_out != nullptr) && yellow_box->left_out->is_finished && fs::exists(parser.metadata.wave) && !music_playing) {
        music_playing = true;
        audio.stop_sound("bgm");
        audio.load_music_stream(parser.metadata.wave, "preview");
        if (audio.is_music_stream_valid("preview")) {
            audio.play_music_stream("preview", VolumePreset::MUSIC);
            audio.seek_music_stream("preview", parser.metadata.demostart);
        }
    }

    if (!score_history) {
        for (const auto& s : scores) {
            if (s.has_value()) {
                score_history = std::make_unique<ScoreHistory>(scores, current_time);
                break;
            }
        }
    }

    if (score_history)
        score_history->update(current_time);
}

void SongBox::expand_box() {
    BaseBox::expand_box();
    box_opened_at = get_current_ms();
}

void SongBox::close_box() {
    BaseBox::close_box();
    box_opened_at = 0.0;
    if (music_playing) {
        if (audio.is_music_stream_valid("preview")) {
            audio.stop_music_stream("preview");
            audio.unload_music_stream("preview");
        }
        audio.play_sound("bgm", VolumePreset::MUSIC);
        music_playing = false;
    }
}

void SongBox::draw_score_history() {}

void SongBox::enter_box() {
    yellow_box->create_anim_2();
    diff_fade_in->start();
}

void SongBox::draw_closed() {}

void SongBox::draw_diff_select() {}

void SongBox::draw_text() {}

void SongBox::draw_open() {}
