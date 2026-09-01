#include "box_dan.h"
#include "../../../libs/song_parser.h"
#include "../../../libs/scores.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>

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
    // ROUND 64 (r64-danselect-fidelity) -- the ribbon chip's kanji is PURE BLACK
    // with NO outline on the cabinet, not white-on-black-outline.
    //
    // Evidence: the twenty chips are baked art, one character each
    // (`dani_select.nulm` board_1..board_20 = chars 191/194/.../258, sprite 282
    // depths 0-19). `lumen_shape_dump -m -r 191 -O 960 540` renders a 79x172
    // wooden tile whose 五級 brush strokes are solid #000 with no stroke of any
    // other colour around them -- the tile's own dark border is the only outline
    // on it. There is no DefineEditText anywhere in the ribbon: dani_select.nulm
    // defines exactly 12 text records (txt_song_name / txt_song_ruby_name /
    // txt_clear_label / txt_theme_name / txt_need_score / txt_name_gauge /
    // txt_num_border_gauge / txt_*_bestscore), all of them on the DETAIL board,
    // none on a chip -- so there is no fill/outline pair to copy, the art itself
    // is the spec. Ours generated the chip label as text and inherited the song
    // wheel's white fill + 5.0 black outline, which is what the report called
    // "文字繪製方式錯誤，應該是純黑色".
    //
    // Opt-in so PyTaikoGreen (whose chips are flat colour plates that need the
    // light-on-dark treatment) is bit-for-bit unchanged:
    //   "dan_chip_name_black": { "x": 1 }
    // The outline colour still has to be black rather than "none": OutlinedText
    // always draws the outline pass, so a 0 thickness is the only way to switch
    // it off -- and this skin's ol() proportional-outline convention (ROUNDs
    // 16/27/28) reduces to exactly that here.
    const bool chip_black = tex.skin_flag("dan_chip_name_black");
    name = std::make_unique<OutlinedText>(text_name, (int)base_font,
                                          chip_black ? ray::BLACK : ray::WHITE,
                                          ray::BLACK, true,
                                          chip_black ? 0.0f : name_outline);
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
    // ROUND 95 -- prefer the strings the DAN_SELECT scan worker already read.
    // This loop used to run a full `SongParser` over EVERY chart of the course on
    // the main thread, the first frame the box came on screen. That is a third
    // parse of each chart (the scan already does two) and it lands on the intro
    // movie's reveal: the r95 timeline measured ~2 s of completely frozen render
    // loop there, in the pre-r95 build as well. `song_titles` carries the same
    // two strings off the scan's own parse. An empty/short vector (any other
    // construction path, e.g. PyTaikoGreen) falls back to the parse below, so
    // nothing else changes.
    const bool have_titles = song_titles.size() == songs.size();
    for (auto& entry : songs) {
        std::string title_str, sub_str;
        if (have_titles) {
            title_str = song_titles[song_idx].first;
            sub_str   = song_titles[song_idx].second;
        } else {
            SongParser sp(entry.song_path);
            title_str = sp.metadata.title.count(lang) ? sp.metadata.title.at(lang) : sp.metadata.title.at("en");
            sub_str   = sp.metadata.subtitle.count(lang) ? sp.metadata.subtitle.at(lang) : "";
        }
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
        // ROUND 64 (r64-danselect-fidelity) -- the 1st/2nd/3rd position badge.
        //
        // `dani_select_theme_disp_data.lua:SetDaniSongLabel` does
        //   GotoAndStop("normal/set_song_N/label", "song_no_N")
        // and `label` is char 65 in dani_select.nulm (sprite 104 depth 1),
        // whose three labels are song_no_1@0 / song_no_2@5 / song_no_3@10 --
        // an 80x80 round chip reading 1st (green) / 2nd (blue) / 3rd (pink).
        // Art extracted verbatim by scratchpad/r64/extract_art.py into
        // Graphics/dan_select/yellow_box/song_label/{0,1,2}.png.
        //
        // Geometry: inside sprite 104 the badge sits at row-local (-632, 0) and
        // the difficulty icon at (-534, -16); the board's three rows are at
        // board-local y -348 / -238 / -128 (pitch 110). The board->screen map
        // for this skin is +(1102, 618) -- fixed by the same difficulty icon,
        // whose 80x56 art is placed at (528, 242), i.e. centre (568, 270) =
        // (-534, -348) + (1102, 618) -- so the badge's own centre is
        // (470, 270 + 110i) and its 80x80 top-left (430, 230 + 110i).
        // Skipped entirely when the skin ships no such art (PyTaikoGreen).
        tex.draw_texture(YELLOW_BOX::GENRE_BANNER,   {.frame=songs[i].genre_index, .x=x, .y=y, .fade=f});
        // Above the row plate, as on the cabinet: inside sprite 104 the plate is
        // depth 0 and `label` depth 1.
        if (tex.has_texture("yellow_box/song_label"))
            tex.draw_texture(tex.get_enum("yellow_box/song_label"),
                             {.frame=std::min(i, 2), .x=x, .y=y, .fade=f});
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

    // ROUND 77 (r77-danselect-text-followup) -- the 総ノーツ数 READOUT is not a
    // cabinet field, and like ROUND 73's FREE PLAY caption it had no gate of any
    // kind: it was drawn on every open dan board unconditionally.
    //
    // Evidence, from the cabinet's own movie rather than from a screenshot:
    // `dani_select.nulm` defines exactly TWELVE DefineEditText records and not
    // one of them is a note count --
    //   lumen_shape_dump dani_select.nulm --check-text -v
    //     id=23  size=26 "100%"          id=25  size=16 "best score"
    //     id=27  size=36 "100% 以上"      id=29  size=22 "スコア達成数"
    //     id=45  size=26 "1234567"       id=47  size=16 "best score"
    //     id=49  size=28 "1234567 以上"   id=52  size=22 "スコア達成数"
    //     id=55  size=30 "クリア条件"      id=77  size=20 / id=79 size=40 (song
    //                                     name + ruby)   id=284 size=42 "お題名"
    // i.e. the board carries song name/ruby, the course title, the 合格条件 tab,
    // the per-condition caption/threshold/best-score pairs -- and nothing else.
    // The caption art itself is documented in this skin's MAPPING.md as
    // "generated 総ノーツ数, 26 px", i.e. it was invented by the engine's own
    // ROUND 6-era layout and never extracted from the cabinet.
    //
    // The only other place this engine prints a note count is GAME_DAN's
    // dan_info panel (残りノーツ数, `DanGameScreen::draw_dan_info`), and that
    // counter is ALREADY skinned out for this skin (r27-danremaining-notes).
    // So on the cabinet the answer to "which screens show a note count" is
    // NONE, and this is an opt-out rather than a DAN_SELECT special case:
    // `dan_select_total_notes: {"x": 0}` suppresses it. A skin that never
    // declares the key -- PyTaikoGreen -- keeps the readout, unchanged.
    //
    // TOTAL_NOTES_BG stays UNCONDITIONAL: this skin re-purposes that slot for
    // the board's gold pillars (MAPPING.md: "normal__shape13 + normal__shape15
    // composited on a 1544x872 sheet ... the only draw that happens before
    // `frame`"), so it is not part of the readout at all.
    tex.draw_texture(YELLOW_BOX::TOTAL_NOTES_BG, {.fade=f});
    const SkinInfo* tn_slot = tex.skin_entry("dan_select_total_notes");
    if (!tn_slot || tn_slot->x >= 1.0f) {
        tex.draw_texture(YELLOW_BOX::TOTAL_NOTES,    {.fade=f});
        std::string tn = std::to_string(total_notes);
        float tn_margin = tex.skin_config[SC::TOTAL_NOTES_COUNTER_MARGIN].x;
        for (int i = 0; i < (int)tn.size(); i++)
            tex.draw_texture(YELLOW_BOX::TOTAL_NOTES_COUNTER, {.frame=tn[i]-'0', .x=(float)(i*tn_margin), .fade=f});
    }

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

// ─── ROUND 64 (r64-danselect-fidelity) — the cabinet's 合格条件 grid ─────────
//
// The report: 「下面的合格條件繪製方式錯誤」. Ours drew every exam as one
// full-width bar stacked down the board. The cabinet's block is a GRID, and
// `dani_select_theme_disp_data.lua` names every piece of it:
//
//   normal/txt_clear_label                     -- the 合格条件 tab
//   normal/set_gauge                           -- ONE narrow box, on the LEFT,
//                                                 always the 魂ゲージ condition
//   normal/set_score_1 .. set_score_3          -- a RIGHT-hand column of
//                                                 separate boxes, one per
//                                                 remaining condition; a
//                                                 condition of NormaType.kNone
//                                                 puts its box on label "none"
//                                                 (SetDaniIsAvailableQuota) and
//                                                 it is not drawn at all
//   set_score_N/set_theme_1..3/theme_label     -- the per-position segment
//                                                 marker, char 44, labels
//                                                 song_total@0 / song_no_1@5 /
//                                                 song_no_2@10 / song_no_3@15
//
// and `SetDaniThemeQuota` is the data switch this engine already has:
//   theme_is_continuous == true  -> set_score_N label "song_no_1" (ONE segment)
//                                   + theme_label "song_total" (yellow tomoe)
//   theme_is_continuous == false -> label "song_no_3" (THREE segments) with
//                                   theme_label song_no_1/2/3 on each
// `theme_is_continuous` is `Exam::gothrough` (parsed since ROUND 19, per-song
// exams supported since ROUND 47), so the data side needed nothing new.
//
// GEOMETRY, measured off `lumen_shape_dump -m -r 185 -O 960 540 -f 25 -c -v`
// (sprite 185 is the detail board's `normal`), in board-local units, then
// mapped to this skin with the +(1102, 618) offset fixed by the difficulty
// icon (see draw_open above) -- the board is 1:1 with the arcade's, so nothing
// is scaled:
//
//   set_gauge      placed (-505,  84), art 321x171  -> screen (436, 615)
//   set_score_1..3 placed ( 168,  63 + 132n), art 998x129
//                                              -> screen (769, 615 + 132n)
//   set_theme_k    box-local x 174 + 327k  (pitch 327)
//   theme_label    box-local (14 + 327k, 48), 160x32
//   txt_need_score box 268 wide, centred on box-local 185 + 327k
//
// ART: the wide right-hand plate is ALREADY in the skin -- yellow_box/
// judge_box.png, whose opaque content is exactly 998x129 at PNG (3,2), i.e.
// the arcade `set_score` plate; ROUND 6-era code was merely stretching it to
// 1330 px and stacking two of them. Only two files are new, both extracted
// verbatim from dani_select.nutexb by scratchpad/r64/extract_art.py:
// yellow_box/exam_gauge_box.png (the 321x171 left plate, tomoe strip baked in
// because the gauge condition is always a whole-run total) and
// yellow_box/exam_seg_label/{0..3}.png (the 160x32 segment strips). The old
// 9-slice exam_box_* frame is NOT drawn in grid mode -- but NOT because the
// cabinet has no outer frame.
//
// ROUND 73 CORRECTION. ROUND 64 wrote here that "the cabinet has no outer frame
// around the block, only the tab and the boxes on the board's own cream". That
// is wrong, and it is why the 合格条件 block reads as a bare tab floating on
// cream. Column/row scans of the cabinet's own render of `dani_select.nulm`
// sprite 185 (`lumen_shape_dump -m -r 185 -O 960 540 -f <rank>`) find a solid
// **#503C2C** rule on three sides of the block:
//   * top    : screen y 603..604 (png y 525..526), running the full width --
//              sampled at screen (920,604) and (600,603).
//   * left   : screen x 420..421 (png x 278..279), from the tab down to y 1020.
//   * right  : screen x 1782..1783 (png x 1640..1641), same extent.
// joined by ~25 px rounded corners. The 9-slice `exam_box_*` set cannot draw it
// (wrong colour and wrong corner radius), so the frame ships as ONE cut taken
// straight out of the cabinet render, `yellow_box/exam_frame.png` (1369x421 at
// screen (418,601) -- scratchpad/r73/extract_art.py `cut_frame`).
//
// The rule colour is rank-INVARIANT: sprite 185's 126 frames are the 21 dan
// ranks (labels 5kyuu f0 .. 14dan f90, sousaku f95, gaiden f100), and probing
// 1kyuu/1dan/5dan/9dan/10dan/gaiden gives #503C2C for the frame, #97705C for
// the condition-box border and #966D5D for the label-pill border in EVERY one.
// So none of this chrome may be themed per rank.
//
// Every draw is absolute-positioned through this helper so the numbers above
// can be read straight out of the measurement, rather than being spread over
// texture.json offsets that would have to be back-solved.
static void draw_abs(const std::string& name, float X, float Y, float fade,
                     int frame = 0) {
    if (!tex.has_texture(name)) return;
    uint32_t id = (uint32_t)tex.get_enum(name);
    auto it = tex.textures.find(id);
    if (it == tex.textures.end() || it->second->x.empty()) return;
    tex.draw_texture(id, {.frame = frame,
                          .x = X - it->second->x[0],
                          .y = Y - it->second->y[0],
                          .fade = fade});
}

void DanBox::draw_exam_grid() {
    const float f = open_fade->attribute;

    // ROUND 73 -- the block's outer rule, UNDER the tab (the cut deliberately
    // leaves the tab's own footprint empty, so the tab paints over the join).
    // Fail-soft: a skin that ships no exam_frame.png simply keeps the old bare
    // look rather than losing the block.
    if (tex.has_texture("yellow_box/exam_frame"))
        tex.draw_texture(tex.get_enum("yellow_box/exam_frame"), {.fade=f});

    // The 合格条件 tab keeps its own texture.json position, now (420, 568) --
    // the cabinet's tab is 410x37 at png (278,490)..(687,526), i.e. screen
    // (420,568)..(829,604) under the same +(142,78) board->screen mapping.
    // ROUND 73: it was (420,570) for a 410x**56** file whose top 19 rows were
    // an opaque #FAF3E1 chip of the board cream the cut was taken over (the
    // same mis-crop ROUND 72 found on DAN_RESULT), with the tab itself clipped
    // 18 px on the right. The re-cut file is 410x**37** and starts at the tab's
    // real top row, so the y moves up by the 2 px the old chip was mis-seated.
    //
    // CORRECTED BY ROUND 99 -- the (420,568) POSITION above is right and is
    // left alone; the ART was not. User report 「把這個合成[格]條件移到正確的
    // 位置」: the 合格条件 caption was clipped by the tab's own top edge and sat
    // 18 px left of centre. ROUND 73's re-cut took the tab SILHOUETTE from the
    // cabinet render at offset (18,19) -- correct, re-measured this round -- but
    // reused that SAME offset for the glyph RGB. The pre-r73 410x56 file's
    // glyphs were themselves mis-seated: ink at x 148..260 / y 12..38, i.e.
    // ABOVE the file's own tab top row 19, floating on the cream chip. Shifted
    // by (18,19) that ink became x 130..242 / y -7..19, so the top 7 rows fell
    // off the canvas and the centre moved from 204 to 186. The cabinet's own
    // ink (dani_select.nulm sprite 185, board185_f25.png png (278,490)..(687,526))
    // is tab-local x 137..271 / y 8..34, centre 204.0. The glyph offset is
    // therefore (0,+4), not (18,19); re-cut by scratchpad/r99/recut_header.py,
    // verified back off disk at centre 204.0 / rows 8..34. This is an ASSET
    // fix only -- no engine or skin_config change was needed.
    tex.draw_texture(YELLOW_BOX::EXAM_HEADER, {.fade=f});

    auto num = [&](const SkinInfo* s, float dx, float dy) {
        return std::pair<float,float>{s ? s->x : dx, s ? s->y : dy};
    };
    const SkinInfo* gb = tex.skin_entry("dan_exam_gauge_box");
    const SkinInfo* sb = tex.skin_entry("dan_exam_score_box");
    auto [gx, gy] = num(gb, 436.0f, 615.0f);
    auto [sx, sy] = num(sb, 769.0f, 615.0f);
    const float pitch  = (sb && sb->height > 0) ? sb->height : 132.0f;
    const SkinInfo* sg = tex.skin_entry("dan_exam_seg");
    const float seg_x  = sg ? sg->x     : 14.0f;
    const float seg_y  = sg ? sg->y     : 48.0f;
    const float seg_dx = (sg && sg->width > 0) ? sg->width : 327.0f;
    const SkinInfo* vp = tex.skin_entry("dan_exam_value");
    const float val_x  = vp ? vp->x : 185.0f;
    const float val_y  = vp ? vp->y : 40.0f;
    const SkinInfo* pp = tex.skin_entry("dan_exam_label_pill");
    const float pill_x = pp ? pp->x : 26.0f;
    const float pill_y = pp ? pp->y : 2.0f;

    static const std::unordered_map<std::string, std::string> exam_icons = {
        {"gauge",        "yellow_box/exam_gauge"},
        {"combo",        "yellow_box/exam_combo"},
        {"hit",          "yellow_box/exam_hit"},
        {"judgebad",     "yellow_box/exam_judgebad"},
        {"judgegood",    "yellow_box/exam_judgegood"},
        {"judgeperfect", "yellow_box/exam_judgeperfect"},
        {"score",        "yellow_box/exam_score"},
        {"renda",        "yellow_box/exam_roll"},
    };

    const float margin = tex.skin_config[SC::EXAM_COUNTER_MARGIN].x;

    // ROUND 89 -- the threshold as the cabinet actually draws it: ONE dynamic
    // text field, RIGHT-aligned, not a digit sheet plus suffix sprites.
    //
    // `dani_select.nulm` sprite 185 (`-m -r 185 -c -O 960 540 -v`):
    //   id=27 txt_num_border_gauge  (-604, 34.5)  size 36 align=1 border 4.00 white
    //                               box 240x64 -> right edge -366   [gauge plate]
    //   id=49 txt_need_score        (-282+327k, 40.5+132r) size 28 align=1
    //                               border 3.00 white, box 270x54
    //                               -> right edge -14+327k          [score segs]
    // align=1 is RIGHT (exam_caption.h documents the same finding for the two
    // sibling records on GAME_DAN / DAN_RESULT).
    //
    // The sprite path below composed the same caption from `yellow_box/judge_num`
    // at a FIXED 44 px pitch (`exam_counter_margin`) plus separate % / 以上 / 未満
    // sprites nudged +8 and +6 px in y -- a monospace cell around a proportional
    // face, and a suffix off the digits' baseline. That is the reported
    // 「xxx以上 未滿的xxx數字都長得不太對勁」. Measured on this skin's own frame
    // (scratchpad/r89/ours_panel.png): "92 % 以上" ran 44 px per digit against
    // the cabinet's proportional advance.
    //
    // This is the SAME defect and the SAME root cause ROUND 70 fixed on GAME_DAN
    // and DAN_RESULT; exam_caption.h's header says "one defect, one root cause,
    // both screens" -- it was THREE screens, and DAN_SELECT was missed because it
    // lives here rather than in scenes/. The string builder is shared, so the
    // caption text itself already matches the other two by construction.
    //
    // Opt-in, exactly like the other two: `dan_select_exam_border_text` (score
    // segments) and `dan_select_gauge_border_text` (the gauge plate) carry the
    // field's RIGHT edge in x and its box top in y, both RELATIVE TO THE PLATE,
    // plus font_size and outline (= the record's border / screen_scale).
    // A skin that declares neither keeps the sprite composition bit-for-bit.
    const SkinInfo* seg_bt   = tex.skin_entry("dan_select_exam_border_text");
    const SkinInfo* gauge_bt = tex.skin_entry("dan_select_gauge_border_text");

    auto draw_value_text = [&](const Exam& exam, const SkinInfo* bt,
                               float right_x, float top_y) -> bool {
        if (!bt) return false;
        const float ol = bt->outline >= 0 ? bt->outline : (3.0f / tex.screen_scale);
        OutlinedText* cap = exam_captions.get(
            exam_threshold_text(tex, exam.type, exam.range, exam.red,
                                global_data.config->general.language),
            bt->font_size > 0 ? bt->font_size : 28, ol);
        if (!cap) return false;
        const float pad = ExamCaptionCache::pad_for(ol, tex.screen_scale);
        cap->draw({.x = right_x - cap->width + pad, .y = top_y - pad, .fade = f});
        return true;
    };

    // One condition's threshold, drawn as the cabinet's single centred
    // "<N>[%] 以上/未満" run rather than as a right-aligned column.
    auto draw_value = [&](const Exam& exam, float centre_x, float top_y) {
        const std::string digits = std::to_string(exam.red);
        float pct_w = 0.0f, suf_w = 0.0f;
        if (exam.type == "gauge" && tex.has_texture("yellow_box/exam_percent"))
            pct_w = (float)tex.textures.at((uint32_t)tex.get_enum("yellow_box/exam_percent"))->width;
        const char* suffix = exam.range == "less" ? "yellow_box/exam_less"
                           : exam.range == "more" ? "yellow_box/exam_more" : nullptr;
        if (suffix && tex.has_texture(suffix))
            suf_w = (float)tex.textures.at((uint32_t)tex.get_enum(suffix))->width;
        const float total = digits.size() * margin + pct_w + suf_w;
        float cx = centre_x - total * 0.5f;
        for (char c : digits) {
            draw_abs("yellow_box/judge_num", cx, top_y, f, c - '0');
            cx += margin;
        }
        if (pct_w > 0.0f) { draw_abs("yellow_box/exam_percent", cx, top_y + 8, f); cx += pct_w; }
        if (suf_w > 0.0f) { draw_abs(suffix, cx, top_y + 6, f); }
    };

    int right_row = 0;
    bool gauge_used = false;
    for (const Exam& exam : exams) {
        const bool left = (exam.type == "gauge" && !gauge_used);
        if (left) gauge_used = true;
        if (!left && right_row >= 3) continue;      // the cabinet has three slots

        const float BX = left ? gx : sx;
        const float BY = left ? gy : (sy + pitch * right_row);

        draw_abs(left ? "yellow_box/exam_gauge_box" : "yellow_box/judge_box",
                 BX - (left ? 0.0f : 3.0f), BY - (left ? 0.0f : 2.0f), f);

        auto ic = exam_icons.find(exam.type);
        if (ic != exam_icons.end())
            draw_abs(ic->second, BX + pill_x, BY + pill_y, f);

        if (left) {
            // The gauge plate bakes its own song_total strip, so only the
            // threshold is drawn over it.
            if (!draw_value_text(exam, gauge_bt,
                                 BX + (gauge_bt ? gauge_bt->x : 0.0f),
                                 BY + (gauge_bt ? gauge_bt->y : 0.0f)))
                draw_value(exam, BX + 181.0f, BY + 45.0f);
        } else {
            // gothrough (a whole-run total) -> one segment carrying the yellow
            // tomoe; per-song -> three, one per position, colour-coded to the
            // 1st/2nd/3rd badges on the song rows above.
            const int segs = exam.gothrough ? 1 : (int)std::min<size_t>(songs.size(), 3);
            for (int k = 0; k < segs; k++) {
                const int frame = exam.gothrough ? 0 : (k + 1);
                draw_abs("yellow_box/exam_seg_label",
                         BX + seg_x + seg_dx * k, BY + seg_y, f, frame);
                if (!draw_value_text(exam, seg_bt,
                                     BX + (seg_bt ? seg_bt->x : 0.0f) + seg_dx * k,
                                     BY + (seg_bt ? seg_bt->y : 0.0f)))
                    draw_value(exam, BX + val_x + seg_dx * k, BY + val_y);
            }
        }
        if (!left) right_row++;
    }
}

void DanBox::draw_exam_box() {
    if (tex.skin_flag("dan_exam_grid")) { draw_exam_grid(); return; }

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
