#include "box_dan.h"
#include "../../../libs/song_parser.h"
#include "../../../libs/scores.h"

DanBox::DanBox(const fs::path& path, const std::string& title, int color,
               const std::vector<DanSongEntry>& songs_in,
               const std::vector<Exam>& exams_in, int total_notes_in)
    : BaseBox(path, BoxDef{title, static_cast<TextureIndex>(color), GenreIndex::DAN, "", std::nullopt, std::nullopt})
    , dan_title(title), dan_color(color)
    , songs(songs_in), exams(exams_in), total_notes(total_notes_in)
{
    text_name = title;
}

// ROUND 16 (r16-dan): the long-text shrink used to be a flat
// `font -= 10 * screen_scale`, which mixes two pixel spaces. `screen_scale` is
// the PARENT-to-screen factor (1.5 for a 1080p child over 720p PyTaikoGreen) and
// exists so the parent's 720p skin_config numbers land on screen; a CHILD skin's
// font_size is already in screen pixels, so 10 parent-px came off it as 15 real
// px. YataiDON-HSS-Zhong's `dan_subtitle.font_size` is 22, so any subtitle of 30
// bytes or more was rendered at **7 px** with a 3 px outline -- the unreadable
// smear r15-qa filed as defect #6's third symptom (`A Cruel Angel's Thesis`,
// whose ruby is 41 bytes). The cabinet's own text fields auto-shrink to fit
// their box, so a proportional shrink with a floor is also the closer behaviour.
// The floor is what changes anything: the subtraction is kept verbatim so the
// parent skin's larger sizes are bit-identical.
static float dan_shrink_font(float font_size) {
    return std::max(font_size - 10.0f * tex.screen_scale, font_size * 0.6f);
}

void DanBox::load_text() {
    BaseBox::load_text();  // populates name (vertical) for closed state
    const SkinInfo* chip_slot = tex.skin_entry("dan_chip_name");
    float base_font = (chip_slot && chip_slot->font_size > 0)
                    ? (float)chip_slot->font_size
                    : (float)tex.skin_config[SC::SONG_BOX_NAME].font_size;
    // ROUND 27 (r27-danselect-subtitle): dan_shrink_font() shrinks the FONT but
    // OutlinedText's outline_thickness default (5.0f, i.e. 7.5px on-screen at this
    // skin's screen_scale 1.5) is constant regardless of font_size. Round 16 only
    // fixed the >=30-byte case from 7px to a 13px floor (0.6x of 22px), but never
    // touched the outline -- so a 30+ byte subtitle still renders at 13px glyphs
    // wrapped in a 7.5px outline (58% of the glyph size), which blots the letter
    // interiors into the outline and makes the word look truncated/melted at both
    // ends. Scaling outline_thickness by the SAME ratio the font was shrunk by
    // keeps the stroke:outline proportion identical to the unshrunk case (34%),
    // which is legible. Same bug, same fix, on the closed-box `dan_chip_name` --
    // it goes through the identical dan_shrink_font() path.
    float name_outline = 5.0f;
    if (utf8_char_count(text_name) >= 30) {
        float shrunk = dan_shrink_font(base_font);
        name_outline = 5.0f * (shrunk / base_font);
        base_font = shrunk;
    }
    name = std::make_unique<OutlinedText>(text_name, (int)base_font, ray::WHITE, ray::BLACK, true, name_outline);
    int font_size = tex.skin_config[SC::DAN_TITLE].font_size;
    hori_name = std::make_unique<OutlinedText>(dan_title, font_size, ray::WHITE, ray::BLACK, false);

    // ROUND 57 (r57-dani-leftovers) — hidden songs (dan.json chart-level
    // `"hidden": true` = the arcade's is_hiddens[idx]): masked as the
    // wordlist's dani_select_song_secret (？？？？？？, subtitle suppressed)
    // until the player has REACHED the song — DaniData.SetDaniBestScore opens
    // song_isOpen[idx] for idx <= arrival_song_cnt once the course has been
    // played at all (clear_level > kNoPlay). The engine's equivalents:
    // dan_results.rank > 0 and its ROUND 57 `arrival` column.
    int revealed = 0;
    {
        bool any_hidden = false;
        for (const auto& e : songs) any_hidden |= e.hidden;
        if (any_hidden) {
            auto rec = scores_manager.get_dan_record(
                get_player_id(global_data.player_num), dan_title);
            if (rec && rec->rank > 0) revealed = rec->arrival;
        }
    }

    const std::string& lang = global_data.config->general.language;
    int song_idx = 0;
    for (auto& entry : songs) {
        SongParser sp(entry.song_path);
        std::string title_str = sp.metadata.title.count(lang) ? sp.metadata.title.at(lang) : sp.metadata.title.at("en");
        std::string sub_str   = sp.metadata.subtitle.count(lang) ? sp.metadata.subtitle.at(lang) : "";
        if (entry.hidden && song_idx >= revealed) {
            title_str = "？？？？？？";
            sub_str.clear();
        }
        song_idx++;

        int base_sub_font = tex.skin_config[SC::DAN_SUBTITLE].font_size;
        int sub_font = base_sub_font;
        float sub_outline = 5.0f;
        if (sub_str.size() >= 30) {
            float shrunk = dan_shrink_font((float)base_sub_font);
            sub_outline = 5.0f * (shrunk / (float)base_sub_font);
            sub_font = (int)shrunk;
        }

        // The per-song title/subtitle were hard-coded vertical, which is right
        // for the engine's own layout (three tall cards spread across the board)
        // and wrong for the arcade's (three 1380x104 rows stacked down it). Opt
        // in with skin_config "dan_title_horizontal": {"x": 1}; a skin that does
        // not declare it -- PyTaikoGreen -- keeps vertical, unchanged.
        const bool vertical = !tex.skin_flag("dan_title_horizontal");
        song_texts.push_back({
            std::make_unique<OutlinedText>(title_str, font_size, ray::WHITE, ray::BLACK, vertical),
            std::make_unique<OutlinedText>(sub_str,   sub_font,  ray::WHITE, ray::BLACK, vertical, sub_outline)
        });
    }
    text_loaded = true;
}

void DanBox::update(double current_ms) {
    BaseBox::update(current_ms);
    if (yellow_box.has_value() && yellow_box_opened && !yellow_box->is_diff_select)
        yellow_box->create_anim_2();
}

// ROUND 17 -- the 88x184 ribbon CHIP, split out of draw_closed().
//
// `draw_open()` never drew one, so the selected course left a HOLE in the ribbon
// where the cabinet has a chip plus the cursor glow on top of it. On the cabinet
// the strip is continuous: all twenty chips sit at ty 0 at every resting label of
// `dani_select.nulm` board_all/normal (verified at frames 195/255/270/405/480) and
// the selection is marked ONLY by cursor_big and the two arrow chips.
void DanBox::draw_chip() {
    tex.draw_texture(YELLOW_BOX::SHADOW_BOTTOM_LEFT,  {.x=position, .fade=fade->attribute, .index=0});
    tex.draw_texture(YELLOW_BOX::SHADOW_BOTTOM,       {.x=position, .fade=fade->attribute, .index=0});
    tex.draw_texture(YELLOW_BOX::SHADOW_BOTTOM_RIGHT, {.x=position, .fade=fade->attribute, .index=0});
    tex.draw_texture(YELLOW_BOX::SHADOW_RIGHT,        {.x=position, .fade=fade->attribute, .index=0});
    tex.draw_texture(YELLOW_BOX::SHADOW_TOP_RIGHT,    {.x=position, .fade=fade->attribute, .index=0});

    tex.draw_texture(BOX::FOLDER, {.frame=dan_color, .x=position, .fade=fade->attribute});

    if (text_loaded && name) {
        // ROUND 16 (r16-dan): a closed dan box is the arcade's 88x184 rank
        // ribbon CHIP, not a song-wheel box, and its label has to be centred in
        // that chip. `song_box_name` is shared with the song wheel, so round 14
        // had to leave the kanji flush against y=0 -- which is where r15-qa
        // photographed it cut off by the top of the screen. `dan_chip_name` is
        // that slot's own entry; a skin that does not declare it keeps
        // `song_box_name` exactly as before.
        const SkinInfo* chip = tex.skin_entry("dan_chip_name");
        const SkinInfo& nb = chip ? *chip : tex.skin_config[SC::SONG_BOX_NAME];
        name->draw({
            .x = position + nb.x - name->width / 2.0f,
            .y = nb.y,
            .y2 = std::min(name->height, nb.height) - name->height,
            .fade = fade->attribute
        });
    }
}

void DanBox::draw_closed() { draw_chip(); }

void DanBox::draw_open() {
    if (!yellow_box.has_value()) return;
    // ROUND 17: the chip stays in the strip while this course's board is open.
    draw_chip();
    yellow_box->draw();

    if (!text_loaded) return;
    float f = open_fade->attribute;

    // The per-song offset is a VECTOR: `.x` spreads the dan songs horizontally
    // (the engine's original layout), `.y` stacks them vertically (the arcade's:
    // three 1380x104 rows at pitch 110). A skin that only sets `.x` -- every skin
    // written before this line existed, PyTaikoGreen included -- gets `.y == 0`
    // and is bit-for-bit unchanged.
    float offset_x = tex.skin_config[SC::DAN_YELLOW_BOX_OFFSET].x;
    float offset_y = tex.skin_config[SC::DAN_YELLOW_BOX_OFFSET].y;
    for (int i = 0; i < (int)songs.size(); i++) {
        float x = i * offset_x;
        float y = i * offset_y;
        tex.draw_texture(YELLOW_BOX::GENRE_BANNER,   {.frame=songs[i].genre_index, .x=x, .y=y, .fade=f});
        tex.draw_texture(YELLOW_BOX::DIFFICULTY,     {.frame=songs[i].difficulty,  .x=x, .y=y, .fade=f});
        tex.draw_texture(YELLOW_BOX::DIFFICULTY_X,   {.x=x, .y=y, .fade=f});
        tex.draw_texture(YELLOW_BOX::DIFFICULTY_STAR,{.x=x, .y=y, .fade=f});

        // Level counter
        std::string lvl = std::to_string(songs[i].level);
        float margin = tex.skin_config[SC::DAN_LEVEL_COUNTER_MARGIN].x;
        float total_w = lvl.size() * margin;
        for (int j = 0; j < (int)lvl.size(); j++) {
            tex.draw_texture(YELLOW_BOX::DIFFICULTY_NUM, {.frame=lvl[j]-'0', .x=x-(total_w/2)+(j*margin), .y=y, .fade=f});
        }

        // Song title and subtitle
        if (i < (int)song_texts.size()) {
            auto& [title_text, sub_text] = song_texts[i];
            SkinInfo td = tex.skin_config[SC::DAN_TITLE];
            SkinInfo sd = tex.skin_config[SC::DAN_SUBTITLE];
            if (title_text)
                title_text->draw({.x=td.x+x, .y=td.y+y, .y2=std::min(title_text->height, td.height)-title_text->height, .fade=f});
            if (sub_text)
                sub_text->draw({.x=sd.x+x, .y=sd.y+y-std::min(sub_text->height, sd.height), .y2=std::min(sub_text->height, sd.height)-sub_text->height, .fade=f});
        }
    }

    // Total notes
    tex.draw_texture(YELLOW_BOX::TOTAL_NOTES_BG, {.fade=f});
    tex.draw_texture(YELLOW_BOX::TOTAL_NOTES,    {.fade=f});
    std::string tn = std::to_string(total_notes);
    float tn_margin = tex.skin_config[SC::TOTAL_NOTES_COUNTER_MARGIN].x;
    for (int i = 0; i < (int)tn.size(); i++)
        tex.draw_texture(YELLOW_BOX::TOTAL_NOTES_COUNTER, {.frame=tn[i]-'0', .x=(float)(i*tn_margin), .fade=f});

    // Frame. ROUND 23 (r23-dandoors-recover): when the skin ships `rank_plate` and the
    // course declares a `rank_art` index, that art already carries the brush kanji AND
    // the romaji (39.06 com_dani_mc), so the generated course-name text is dropped --
    // mirroring dan_result.cpp:383 / game_dan.cpp:680's own rank_plate swap. `dan_rank`
    // (dan.json "rank_art") has been parsed and carried on DanBox since ROUND 19 but was
    // only ever forwarded to SessionData::dan_rank for RESULT; DAN_SELECT's own open
    // board always drew the flat colour plate. Art: Graphics/dan_select/yellow_box/
    // rank_plate/0..26.png, copied verbatim from Graphics/dan_result/result_info/
    // rank_plate (same 280x560 canvas, same baked source, already used at that exact
    // size by dan_result's plate -- no rescale, no new extraction).
    if (dan_rank >= 0 && tex.skin_entry("dan_select_rank_plate")) {
        tex.draw_texture(YELLOW_BOX::RANK_PLATE, {.frame=dan_rank, .fade=f});
    } else {
        tex.draw_texture(YELLOW_BOX::FRAME, {.frame=dan_color, .fade=f});
        if (hori_name) {
            SkinInfo hn = tex.skin_config[SC::DAN_HORI_NAME];
            hori_name->draw({
                .x = hn.x - hori_name->width/2.0f,
                .y = hn.y,
                .x2 = std::min(hori_name->width, hn.width) - hori_name->width,
                .fade = f
            });
        }
    }

    draw_exam_box();
}

void DanBox::draw_exam_box() {
    float f = open_fade->attribute;
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_BOTTOM_RIGHT, {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_BOTTOM_LEFT,  {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_TOP_RIGHT,    {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_TOP_LEFT,     {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_BOTTOM,       {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_RIGHT,        {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_LEFT,         {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_TOP,          {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_BOX_CENTER,       {.fade=f});
    tex.draw_texture(YELLOW_BOX::EXAM_HEADER,           {.fade=f});

    float offset_y = tex.skin_config[SC::DAN_EXAM_INFO].y;
    float margin   = tex.skin_config[SC::EXAM_COUNTER_MARGIN].x;

    for (int i = 0; i < (int)exams.size(); i++) {
        const Exam& exam = exams[i];
        float y = i * offset_y;
        tex.draw_texture(YELLOW_BOX::JUDGE_BOX, {.y=y, .fade=f});

        // Exam type icon
        static const std::unordered_map<std::string, TexID> exam_icons = {
            {"gauge",        YELLOW_BOX::EXAM_GAUGE},
            {"combo",        YELLOW_BOX::EXAM_COMBO},
            {"hit",          YELLOW_BOX::EXAM_HIT},
            {"judgebad",     YELLOW_BOX::EXAM_JUDGEBAD},
            {"judgegood",    YELLOW_BOX::EXAM_JUDGEGOOD},
            {"judgeperfect", YELLOW_BOX::EXAM_JUDGEPERFECT},
            {"score",        YELLOW_BOX::EXAM_SCORE},
            {"renda",        YELLOW_BOX::EXAM_ROLL},   // ROUND 47: 連打数 exam type
        };
        auto icon_it = exam_icons.find(exam.type);
        if (icon_it != exam_icons.end())
            tex.draw_texture(icon_it->second, {.y=y, .fade=f});

        float x_offset = 0;
        if (exam.type == "gauge") {
            tex.draw_texture(YELLOW_BOX::EXAM_PERCENT, {.y=y, .fade=f});
            x_offset = tex.skin_config[SC::EXAM_GAUGE_OFFSET].x;
        }

        std::string counter = std::to_string(exam.red);
        for (int j = 0; j < (int)counter.size(); j++) {
            float x = x_offset - (counter.size() - j) * margin;
            tex.draw_texture(YELLOW_BOX::JUDGE_NUM, {.frame=counter[j]-'0', .x=x, .y=y, .fade=f});
        }

        if (exam.range == "more")
            tex.draw_texture(YELLOW_BOX::EXAM_MORE, {.x=-x_offset*1.7f, .y=y, .fade=f});
        else if (exam.range == "less")
            tex.draw_texture(YELLOW_BOX::EXAM_LESS, {.x=-x_offset*1.7f, .y=y, .fade=f});
    }
}
