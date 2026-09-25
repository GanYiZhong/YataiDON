#include "box_song.h"
#include "../../../libs/text.h"
#include "navigator.h"
#include "../../../libs/audio.h"
#include <thread>

namespace {
    double bgm_resume_at   = 0.0;   // 0 = nothing pending
    int    preview_holders = 0;     // focused song boxes that own the bgm slot

    template <size_t N>
    std::array<std::unique_ptr<OutlinedText>, N> build_vertical_label_set(
        const std::array<SC, N>& keys, const std::string& lang,
        float box_width, float box_height, float v_advance, float font_bonus,
        const std::array<ray::Color, N>& fill_colors, const std::array<ray::Color, N>& outline_colors,
        float outline_thickness = 0.0f)
    {
        std::array<std::string, N> labels;
        size_t max_chars = 1;
        for (size_t i = 0; i < N; i++) {
            const SkinInfo& cfg = tex.skin_config[keys[i]];
            auto it = cfg.text.find(lang);
            labels[i] = it != cfg.text.end() ? it->second
                      : !cfg.text.empty()    ? cfg.text.begin()->second
                                              : "";
            max_chars = std::max(max_chars, utf8_char_count(labels[i]));
        }
        float width_cap  = box_width - 4.0f;
        float height_cap = (box_height - 4.0f) / (1.0f + (float)(max_chars - 1) * v_advance);
        int font_size = (int)std::min(width_cap, height_cap) + font_bonus;

        std::array<std::unique_ptr<OutlinedText>, N> result;
        for (size_t i = 0; i < N; i++)
            result[i] = std::make_unique<OutlinedText>(labels[i], font_size, fill_colors[i], outline_colors[i], true, outline_thickness, 2.0f, v_advance);
        return result;
    }

    OutlinedText* diff_tower_label(int diff) {
        static std::array<std::unique_ptr<OutlinedText>, 5> cache;
        static std::string cached_lang;
        const std::string& lang = global_data.config->general.language;
        if (lang != cached_lang) {
            static constexpr std::array<SC, 5> keys = {
                SC::DIFF_TOWER_EASY, SC::DIFF_TOWER_NORMAL, SC::DIFF_TOWER_HARD,
                SC::DIFF_TOWER_ONI, SC::DIFF_TOWER_URA,
            };
            static const std::array<ray::Color, 5> fill_colors = {
                ray::BLACK, ray::BLACK, ray::BLACK, ray::BLACK, ray::WHITE,
            };
            static const std::array<ray::Color, 5> outline_colors = {
                ray::BLANK, ray::BLANK, ray::BLANK, ray::BLANK, ray::Color{0, 73, 90, 255},
            };
            const SkinInfo& box_cfg = tex.skin_config[SC::DIFF_TOWER_LABEL_BOX];
            cache = build_vertical_label_set<5>(keys, lang, box_cfg.width, box_cfg.height, 0.9f, 5.0f, fill_colors, outline_colors, 3.0f);
            cached_lang = lang;
        }
        return cache[diff].get();
    }

    OutlinedText* difficulty_bar_label(int diff) {
        static std::array<std::unique_ptr<OutlinedText>, 4> cache;
        static std::string cached_lang;
        const std::string& lang = global_data.config->general.language;
        if (lang != cached_lang) {
            static constexpr std::array<SC, 4> keys = {
                SC::DIFF_TOWER_EASY, SC::DIFF_TOWER_NORMAL, SC::DIFF_TOWER_HARD, SC::DIFF_TOWER_ONI,
            };
            static const std::array<ray::Color, 4> colors = { ray::BLACK, ray::BLACK, ray::BLACK, ray::BLACK };
            const SkinInfo& box_cfg = tex.skin_config[SC::DIFFICULTY_BAR_LABEL_BOX];
            cache = build_vertical_label_set<4>(keys, lang, box_cfg.width, box_cfg.height, 0.9f, 7.0f, colors, colors);
            cached_lang = lang;
        }
        return cache[diff].get();
    }
}

void SongBox::draw_difficulty_bar_labels(float offset, float fade_val) {
    const SkinInfo& box_cfg = tex.skin_config[SC::DIFFICULTY_BAR_LABEL_BOX];
    for (int i = 0; i < 4; i++) {
        OutlinedText* label = difficulty_bar_label(i);
        label->draw({
            .x = box_cfg.x + i*offset + box_cfg.width / 2.0f - label->width/2,
            .y = box_cfg.y,
            .y2 = std::min(label->height, box_cfg.height) - label->height,
            .fade = fade_val,
        });
    }
}

void SongBox::reset_bgm_slot() {
    bgm_resume_at   = 0.0;
    preview_holders = 0;
}

void SongBox::service_bgm_resume(double current_ms) {
    if (bgm_resume_at <= 0.0) return;
    if (preview_holders > 0) { bgm_resume_at = 0.0; return; }   // a song took it
    if (current_ms < bgm_resume_at) return;
    bgm_resume_at = 0.0;
    audio.play_sound("bgm", VolumePreset::MUSIC);
}

SongBox::SongBox(const fs::path& path, const BoxDef& box_def, SongParser parser)
    : BaseBox(path, box_def)
{
    song_genre_index = genre_index;
    song_genre_label = box_def.genre_label;

    parser.get_metadata();
    auto& titles = parser.metadata.title;
    const std::string& lang = global_data.config->general.language;
    text_name = titles.count(lang) ? titles.at(lang) : titles.count("en") ? titles.at("en") : titles.empty() ? "" : titles.begin()->second;

    auto& subtitles = parser.metadata.subtitle;
    text_subtitle = subtitles.count(lang) ? subtitles.at(lang) : subtitles.count("en") ? subtitles.at("en") : subtitles.empty() ? "" : subtitles.begin()->second;

    this->parser = std::move(parser);

    is_favorite = false;
    diff_fade_in = (FadeAnimation*)tex.get_animation(12);
    refresh_scores();

    load_textures();
}

void SongBox::load_textures() {
    BaseBox::load_textures();
    t_preimage_bg = tex.get_texture("yellow_box/preimage_bg");
    t_crown_dfc = tex.get_texture("yellow_box/crown_dfc");
    t_crown_fc = tex.get_texture("yellow_box/crown_fc");
    t_crown_clear = tex.get_texture("yellow_box/crown_clear");
    t_s_crown_dfc = tex.get_texture("yellow_box/s_crown_dfc");
    t_s_crown_fc = tex.get_texture("yellow_box/s_crown_fc");
    t_s_crown_clear = tex.get_texture("yellow_box/s_crown_clear");
    t_s_crown_outline = tex.get_texture("yellow_box/s_crown_outline");
    t_ex_data_new_audio = tex.get_texture("yellow_box/ex_data_new_audio");
    t_ex_data_old_audio = tex.get_texture("yellow_box/ex_data_old_audio");
    t_difficulty_bar = tex.get_texture("yellow_box/difficulty_bar");
    t_difficulty_bar_shadow = tex.get_texture("yellow_box/difficulty_bar_shadow");
    t_star = tex.get_texture("yellow_box/star");
    t_star_ura = tex.get_texture("yellow_box/star_ura");
    t_branch_indicator = tex.get_texture("yellow_box/branch_indicator");
    t_branch_indicator_ura = tex.get_texture("yellow_box/branch_indicator_ura");
    t_branch_indicator_diff = tex.get_texture("yellow_box/branch_indicator_diff");
    t_diff_tower = tex.get_texture("diff_select/diff_tower");
    t_diff_tower_shadow = tex.get_texture("diff_select/diff_tower_shadow");
    t_ura_oni_plate = tex.get_texture("diff_select/ura_oni_plate");
}

void SongBox::refresh_scores() {
    hashes = scores_manager.get_hashes(path);
#ifdef SUPPORT_FUMEN
    bool cheap_hash = !std::holds_alternative<FumenParser>(parser.impl);
#else
    bool cheap_hash = true;
#endif
    for (const auto& [course, course_data] : parser.metadata.course_data) {
        if (course < 0 || course >= static_cast<int>(hashes.size()))
            continue;
        if (hashes[course].empty() && cheap_hash)
            hashes[course] = parser.get_diff_hash(course);
    }
    for (int i = 0; i < 5; i++) {
        scores[i] = scores_manager.get_score(hashes[i], i, global_data.config->general.player_1_id);
        scores_p2[i] = navigator.is_2p
            ? scores_manager.get_score(hashes[i], i, global_data.config->general.player_2_id)
            : std::nullopt;
    }
    score_history.reset();
}

std::string SongBox::hash_for(int difficulty) {
    if (difficulty < 0 || difficulty >= (int)hashes.size()) return "";
    if (hashes[difficulty].empty()) {
        hashes[difficulty] = parser.get_diff_hash(difficulty);
        if (!hashes[difficulty].empty())
            scores_manager.add_path_binding(path, hashes);
    }
    return hashes[difficulty];
}

void SongBox::reset() {
    BaseBox::reset();
    diff_fade_in = (FadeAnimation*)tex.get_animation(12);
    if (audio.is_music_stream_valid("preview")) {
        audio.unload_music_stream("preview");
    }
    music_playing = false;
    preview_load.reset();
    preview_attempted = false;
    release_preview_slot();
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

void SongBox::preregister_text() {
    BaseBox::preregister_text();
    float base_sub_font = (float)tex.skin_config[SC::YB_SUBTITLE].font_size;
    float sub_font = utf8_char_count(text_subtitle) >= 30 ? base_sub_font - 10.0f * tex.screen_scale : base_sub_font;
    font_manager.register_text(text_subtitle, (int)sub_font);
    float base_name_font = (float)tex.skin_config[SC::SONG_BOX_NAME].font_size;
    float name_font = utf8_char_count(text_name) >= 30 ? base_name_font - 10.0f * tex.screen_scale : base_name_font;
    font_manager.register_text(text_name, (int)name_font);
    font_manager.register_text("BPM\n0123456789", tex.skin_config[SC::SONG_BOX_BPM].font_size);
}

void SongBox::load_text() {
    BaseBox::load_text();
    float base_sub_font = (float)tex.skin_config[SC::YB_SUBTITLE].font_size;
    float font_size = base_sub_font;
    float sub_outline = 5.0f;
    if (utf8_char_count(text_subtitle) >= 30) {
        font_size = base_sub_font - 10.0f * tex.screen_scale;
        sub_outline = 5.0f * (font_size / base_sub_font);
    }
    subtitle = make_unique<OutlinedText>(text_subtitle, (int)font_size, ray::WHITE, ray::BLACK, true, sub_outline);

    float base_name_font = (float)tex.skin_config[SC::SONG_BOX_NAME].font_size;
    font_size = base_name_font;
    float name_outline = 5.0f;
    if (utf8_char_count(text_name) >= 30) {
        font_size = base_name_font - 10.0f * tex.screen_scale;
        name_outline = 5.0f * (font_size / base_name_font);
    }
    name_black = make_unique<OutlinedText>(text_name, (int)font_size, ray::WHITE, ray::BLACK, true, name_outline);
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

    auto wave_ext = parser.metadata.wave.extension();
    bool is_bank = wave_ext == ".nus3bank" || wave_ext == ".nub";

    if (is_bank && yellow_box.has_value() && !music_playing && !preview_load &&
        !preview_attempted && get_current_ms() - bar_open_started_at > 250 &&
        fs::exists(parser.metadata.wave)) {
        preview_attempted = true;
        preview_load = std::make_shared<PreviewLoad>();
        std::thread([state = preview_load, wave = parser.metadata.wave] {
            state->ok = audio.prepare_nus3bank_pcm(wave, state->pcm, true);
            state->done.store(true, std::memory_order_release);
        }).detach();
    }

    if (!is_bank && yellow_box.has_value() && (yellow_box->left_out != nullptr) && yellow_box->left_out->is_finished && fs::exists(parser.metadata.wave) && !music_playing) {
        music_playing = true;
        audio.stop_sound("bgm");
        audio.load_music_stream(parser.metadata.wave, "preview");
        if (audio.is_music_stream_valid("preview")) {
            audio.play_music_stream("preview", VolumePreset::MUSIC);
            audio.seek_music_stream("preview", parser.metadata.demostart);
        }
    }

    if (preview_load && preview_load->done.load(std::memory_order_acquire) &&
        yellow_box.has_value() && (yellow_box->left_out != nullptr) &&
        yellow_box->left_out->is_finished && !music_playing) {
        auto state = std::move(preview_load);
        if (state->ok) {
            music_playing = true;
            audio.stop_sound("bgm");
            float demo_start = state->pcm.preview_ms > 0
                             ? state->pcm.preview_ms / 1000.0f
                             : parser.metadata.demostart;
            audio.load_music_stream_prepared(std::move(state->pcm), "preview");
            if (audio.is_music_stream_valid("preview")) {
                audio.play_music_stream("preview", VolumePreset::MUSIC);
                audio.seek_music_stream("preview", demo_start);
            }
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
    if (!holds_preview_slot && fs::exists(parser.metadata.wave)) {
        holds_preview_slot = true;
        preview_holders++;
    }
}

void SongBox::release_preview_slot() {
    if (!holds_preview_slot) return;
    holds_preview_slot = false;
    if (preview_holders > 0) preview_holders--;
}

void SongBox::close_box() {
    BaseBox::close_box();
    box_opened_at = 0.0;
    preview_load.reset();
    preview_attempted = false;
    release_preview_slot();
    if (music_playing) {
        if (audio.is_music_stream_valid("preview")) {
            audio.stop_music_stream("preview");
            audio.unload_music_stream("preview");
        }
        bgm_resume_at = get_current_ms() + 330.0;
        music_playing = false;
    }
}

void SongBox::draw_score_history() {
    if (!score_history) return;
    if (!yellow_box_opened) return;
    if (get_current_ms() < box_opened_at + 3000.0) return;
    score_history->draw();
}

void SongBox::enter_box() {
    yellow_box->create_anim_2();
    diff_fade_in->start();
}

void SongBox::draw_closed() {
    BaseBox::draw_closed();

    if (!text_loaded) return;
    float bx = box_x();
    float by = box_y();
    float name_x = bx + tex.skin_config[SC::SONG_BOX_NAME].x - (int)(this->name->width / 2);
    float name_y = tex.skin_config[SC::SONG_BOX_NAME].y + by;
    float name_h = std::min((float)this->name->height, tex.skin_config[SC::SONG_BOX_NAME].height);
    this->name->draw({.x = name_x, .y = name_y, .y2 = name_h - this->name->height, .fade=fade->attribute});

    if (preimage.has_value()) {
        tex.draw_texture(t_preimage_bg, {.x=bx, .y=by, .fade=fade->attribute});
        ray::Rectangle src = {0, 0, (float)preimage->width, (float)preimage->height};
        ray::Rectangle dest = {bx + tex.skin_config[SC::PREIMAGE].x, tex.skin_config[SC::PREIMAGE].y + by, tex.skin_config[SC::PREIMAGE].width, tex.skin_config[SC::PREIMAGE].height};
        ray::DrawTexturePro(preimage.value(), src, dest, {0,0}, 0, ray::Fade(ray::WHITE, fade->attribute));
    } else if (parser.ex_data.limited_time)
        tex.draw_texture(tex.get_texture("yellow_box/ex_data_limited_time_balloon_" + global_data.config->general.language), {.x=bx, .y=by, .fade=fade->attribute});
    else if (is_new)
        tex.draw_texture(tex.get_texture("yellow_box/ex_data_new_song_balloon_" + global_data.config->general.language), {.x=bx, .y=by, .fade=fade->attribute});

    draw_box_crown(bx, by, fade->attribute);
}

void SongBox::draw_box_crown(float x, float y, double fade_val) {
    auto highest_crown = [&](const std::array<std::optional<Score>, 5>& s, int& frame) -> std::optional<Score> {
        int highest_key = -1;
        for (int i = 0; i < (int)s.size(); ++i) {
            if (s[i].has_value() && parser.metadata.course_data.count(i)) highest_key = std::max(highest_key, i);
        }
        if (highest_key < 0) return std::nullopt;
        frame = std::min((int)Difficulty::URA, highest_key);
        return s[highest_key];
    };
    auto draw_one = [&](const Score& score, int frame, float px) {
        if      (score.crown == Crown::DFC)   tex.draw_texture(t_crown_dfc,   {.frame=frame, .x=px, .y=y, .fade=fade_val});
        else if (score.crown == Crown::FC)    tex.draw_texture(t_crown_fc,    {.frame=frame, .x=px, .y=y, .fade=fade_val});
        else if (score.crown >= Crown::CLEAR) tex.draw_texture(t_crown_clear, {.frame=frame, .x=px, .y=y, .fade=fade_val});
    };

    int frame_1p = 0, frame_2p = 0;
    std::optional<Score> score_1p = highest_crown(scores, frame_1p);
    std::optional<Score> score_2p = navigator.is_2p ? highest_crown(scores_p2, frame_2p) : std::nullopt;

    if (score_2p.has_value()) {
        float half = t_crown_dfc->width * 0.35f;
        if (score_1p.has_value()) draw_one(score_1p.value(), frame_1p, x - half);
        draw_one(score_2p.value(), frame_2p, x + half);
    } else if (score_1p.has_value()) {
        draw_one(score_1p.value(), frame_1p, x);
    }
}

void SongBox::draw_diff_crown(int diff, float x, float y, double fade_val) {
    auto draw_one = [&](const std::optional<Score>& s, float px) {
        if (!s.has_value()) return;
        if      (s->crown == Crown::DFC)   tex.draw_texture(t_s_crown_dfc,   {.x=px, .y=y, .fade=fade_val});
        else if (s->crown == Crown::FC)    tex.draw_texture(t_s_crown_fc,    {.x=px, .y=y, .fade=fade_val});
        else if (s->crown >= Crown::CLEAR) tex.draw_texture(t_s_crown_clear, {.x=px, .y=y, .fade=fade_val});
    };
    if (navigator.is_2p) {
        float half = t_s_crown_dfc->width * 0.35f;
        draw_one(scores[diff],    x - half);
        draw_one(scores_p2[diff], x + half);
    } else {
        draw_one(scores[diff], x);
    }
}

void SongBox::draw_diff_outline(float x, float y, double fade_val) {
    if (navigator.is_2p) {
        float half = t_s_crown_dfc->width * 0.35f;
        tex.draw_texture(t_s_crown_outline, {.x=x - half, .y=y, .fade=fade_val});
        tex.draw_texture(t_s_crown_outline, {.x=x + half, .y=y, .fade=fade_val});
    } else {
        tex.draw_texture(t_s_crown_outline, {.x=x, .y=y, .fade=fade_val});
    }
}

void SongBox::draw_diff_select() {
    BaseBox::draw_diff_select();
    tex.draw_texture(tex.get_texture("diff_select/back_" + global_data.config->general.language),   {.fade=diff_fade_in->attribute});
    tex.draw_texture(tex.get_texture("diff_select/option_" + global_data.config->general.language), {.fade=diff_fade_in->attribute});
    tex.draw_texture(tex.get_texture("diff_select/neiro_" + global_data.config->general.language),  {.fade=diff_fade_in->attribute});

    float offset_x     = tex.skin_config[SC::YB_DIFF_OFFSET_DIFF_SELECT].x;
    float offset_y     = tex.skin_config[SC::YB_DIFF_OFFSET_DIFF_SELECT].y;
    float crown_offset = tex.skin_config[SC::YB_DIFF_OFFSET_CROWN].x;

    for (const auto& [diff, course] : parser.metadata.course_data) {
        if (Difficulty(diff) >= Difficulty::URA) continue;
        float cx = (diff * offset_x) + crown_offset;
        draw_diff_outline(cx, offset_y, std::min((float)diff_fade_in->attribute, 0.25f));
        draw_diff_crown(diff, cx, offset_y, diff_fade_in->attribute);
    }

    for (int i = 0; i < 4; i++) {
        int label_index = i;
        if (i == (int)Difficulty::ONI && is_ura) {
            tex.draw_texture(t_diff_tower,    {.frame=4, .x=i*offset_x, .fade=diff_fade_in->attribute});
            tex.draw_texture(t_ura_oni_plate, {.fade=diff_fade_in->attribute});
            label_index = 4;
        } else {
            tex.draw_texture(t_diff_tower, {.frame=i, .x=i*offset_x, .fade=diff_fade_in->attribute});
        }
        if (!parser.metadata.course_data.count(i))
            tex.draw_texture(t_diff_tower_shadow, {.frame=i, .x=i*offset_x, .fade=std::min((float)diff_fade_in->attribute, 0.25f)});

        OutlinedText* label = diff_tower_label(label_index);
        const SkinInfo& box_cfg = tex.skin_config[SC::DIFF_TOWER_LABEL_BOX];
        label->draw({
            .x = box_cfg.x + i*offset_x + (box_cfg.width - label->width) / 2.0f,
            .y = box_cfg.y,
            .fade = diff_fade_in->attribute
        });
    }

    float star_offset_y = tex.skin_config[SC::YB_DIFF_OFFSET_CROWN].y;
    for (const auto& [course_diff, course] : parser.metadata.course_data) {
        if ((course_diff == (int)Difficulty::URA && !is_ura) ||
            (course_diff == (int)Difficulty::ONI && is_ura))
            continue;
        for (int j = 0; j < course.level; j++)
            tex.draw_texture(t_star_ura, {.x=std::min(course_diff, (int)Difficulty::ONI)*offset_x, .y=j*star_offset_y, .fade=diff_fade_in->attribute});
        if (course.is_branching && ((int)(get_current_ms() / 1000)) % 2 == 0) {
            TextureObject* bt = (course_diff == (int)Difficulty::URA) ? t_branch_indicator_ura : t_branch_indicator_diff;
            tex.draw_texture(bt, {.x=std::min(course_diff, (int)Difficulty::ONI)*offset_x, .fade=diff_fade_in->attribute});
        }
    }
    draw_text();
}

void SongBox::draw_text() {
    float x = position + (yellow_box->right_out->attribute*0.90 - (yellow_box->right_out->start_position*0.90)) + yellow_box->right_out_2->attribute - yellow_box->right_out_2->start_position;
    float h = std::min((float)subtitle->height, tex.skin_config[SC::YB_SUBTITLE].height);
    subtitle->draw({.x = x + tex.skin_config[SC::YB_SUBTITLE].x, .y=tex.skin_config[SC::YB_SUBTITLE].y - h + (float)yellow_box->top_y_out->attribute - yellow_box->top_y_out->start_position, .y2 =h - subtitle->height, .fade=open_fade->attribute});
    float name_h = std::min((float)this->name->height, tex.skin_config[SC::SONG_BOX_NAME].height) - this->name->height;
    float name_x = x + tex.skin_config[SC::SONG_BOX_NAME].x - (int)(this->name->width / 2);
    float name_y = tex.skin_config[SC::SONG_BOX_NAME].y + (float)yellow_box->top_y_out->attribute - yellow_box->top_y_out->start_position;
    name_black->draw({.x=name_x, .y=name_y, .y2=name_h, .fade=open_fade->attribute});
    name->draw({.x=name_x, .y=name_y, .y2=name_h, .fade=1 - open_fade->attribute});
}

void SongBox::draw_open() {
    tex.draw_texture(t_shadow_bottom_left, {.x=position, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_bottom, {.x=position, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_bottom_right, {.x=position, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_right, {.x=position, .fade=open_fade->attribute, .index=1});
    tex.draw_texture(t_shadow_top_right, {.x=position, .fade=open_fade->attribute, .index=1});
    if (yellow_box.has_value())
        yellow_box->draw();

    float offset = tex.skin_config[SC::YB_DIFF_OFFSET].x;

    for (const auto& [diff, course] : parser.metadata.course_data) {
        if (Difficulty(diff) >= Difficulty::URA) continue;
        draw_diff_outline(diff*offset, 0.0f, std::min((float)open_fade->attribute, 0.25f));
        draw_diff_crown(diff, diff*offset, 0.0f, open_fade->attribute);
    }

    if      (parser.ex_data.new_audio)     tex.draw_texture(t_ex_data_new_audio,     {.fade=open_fade->attribute});
    else if (parser.ex_data.old_audio)     tex.draw_texture(t_ex_data_old_audio,     {.fade=open_fade->attribute});
    else if (parser.ex_data.limited_time)  tex.draw_texture(tex.get_texture("yellow_box/ex_data_limited_time_" + global_data.config->general.language),  {.fade=open_fade->attribute});
    else if (is_new)      tex.draw_texture(tex.get_texture("yellow_box/ex_data_new_song_" + global_data.config->general.language),      {.fade=open_fade->attribute});
    if (global_data.config->general.display_bpm) {
        bpm_text->draw({.x = tex.skin_config[SC::SONG_BOX_BPM].x, .y = tex.skin_config[SC::SONG_BOX_BPM].y, .fade=open_fade->attribute});
    }

    if (is_favorite)
        tex.draw_texture(tex.get_texture("yellow_box/favorite_" + std::to_string((int)global_data.player_num) + "p_" + global_data.config->general.language), {.fade=open_fade->attribute});

    for (int i = 0; i < 4; i++) {
        tex.draw_texture(t_difficulty_bar, {.frame=i, .x=i*offset, .fade=open_fade->attribute});
        if (!parser.metadata.course_data.count(i))
            tex.draw_texture(t_difficulty_bar_shadow, {.frame=i, .x=i*offset, .fade=std::min((float)open_fade->attribute, 0.25f)});
    }
    draw_difficulty_bar_labels(offset, open_fade->attribute);

    float offset_y = tex.skin_config[SC::YB_DIFF_OFFSET].y;
    for (const auto& [diff, course] : parser.metadata.course_data) {
        if (Difficulty(diff) >= Difficulty::URA) continue;
        for (int j = 0; j < course.level; j++)
            tex.draw_texture(t_star, {.x=diff*offset, .y=j*offset_y, .fade=open_fade->attribute});
        if (course.is_branching && ((int)(get_current_ms() / 1000)) % 2 == 0)
            tex.draw_texture(t_branch_indicator, {.x=diff*offset, .fade=open_fade->attribute});
    }
    draw_text();
}
