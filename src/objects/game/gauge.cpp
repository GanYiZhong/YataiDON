#include "gauge.h"
#include "../../libs/texture.h"
#include <cmath>
#include <cstdlib>
#include <spdlog/spdlog.h>

// ROUND 80 (r80-gauge-layering-recheck): the rainbow ("fever") strip's cel
// duration, in ms.
//
// The cabinet's rainbow is an 8-cel cross-fading flip-book, not a scroll:
// `enso_normal/enso/tamashiigage/tamashii_gage.nulm` places the `rainbow`
// instance at depth 55 of the gauge clip (char 32 for the 1P oni plate) and
// that clip's inner sprite, character 31, holds the eight cels as eight
// shapes -- ids 15, 17, 19, 21, 23, 25, 27, 29 at depths 0..7, with a ninth
// placement of id 15 at depth 8 closing the loop. Cel k reaches alpha 1 on
// sprite-31 frame 5k exactly, and the incoming cel k+1 ramps
// 51/102/154/205/256 of 256 over the five frames in between -- measured for
// every k in 0..7 with `lumen_anim_dump --sprite 31 --all --leaves`. So one
// cel lasts FIVE stage frames and the whole loop is 40 frames.
//
// The stage runs at 60 fps (the movie header's own rate), so a cel is
// 5/60 s = 83.3333 ms and the loop is 666.667 ms. This used to be a
// hard-coded 75.0 ms (600 ms loop), i.e. the animation ran 11.1 % fast.
//
// The DAN gauge's own movie (`enso_dani/enso/tamashiigauge/tamashii_gauge`,
// fever clip sprite 136 -> inner sprite 110) is authored identically -- eight
// cels, alpha 1 on frame 5k, the same 51/102/154/205 ramp, 41 frames total --
// so the one constant is correct for both modes.
//
// Common.FPS (120) is the SCRIPT tick rate and has nothing to do with this:
// lumen clip frames are 60 fps. Do not conflate the two.
constexpr double kRainbowCelMs = 1000.0 / 60.0 * 5.0;   // 83.3333...

// ROUND 96 (r96-soulgauge-cellgrid): the cabinet's soul-gauge separator
// overlay (tamashii_gage shape 33) is drawn with colour-transform
// Amul = 77/256. ROUND 80 measured that value and reached it in TWO passes --
// this engine's invented 0.15 plus the child skin's 0.17647 second pass in
// arcade_gauge.lua -- which is 0.30 only where the skin's pass actually
// reached. One pass at the real value is the same colour everywhere.
constexpr float kOverlayAlpha = 77.0f / 256.0f;         // 0.30078125

// ============================================================================
// ROUND 48 (r48-soulgauge-chn-port) ??CHN05 soul-gauge model
//
// Runtime mechanism (ported verbatim from the decoded CHN05 Taiko.exe,
// D:\tlb_test_harness\decompiled\specs\gauge_rank.md 禮1a/禮1b/禮1c and
// research/note_judgement.md 禮7):
//   UpdateTamashii (0x140134140):
//     delta = (float)((float)(ratio * point[kind] * mult) * (1/65536))
//     with ratio = TamashiiRatio[0] = 65536 in every one of the 19,158 corpus
//     fumen (so delta == point[kind]), gain/loss mult = 1.0
//     (EnsoData::Settings words 187/188 defaults, 0x140145053/..5B).
//   UpdateScore (0x14055FA80):
//     soul += delta; clamp to [0, TamashiiMax = 10000].
//   IsNormaClear (0x1400E5B10): cleared iff soul >= TamashiiNorm.
//   IsTamashiiMax (0x1400E5C60): rainbow iff soul >= 10000.
//   Display (EnsoGraphicTamashiiGage::Process 0x140132440):
//     cell = (int)(soul / 10000 * 50)  ??50 cells; this engine keeps its
//     87-unit gauge_length as the equivalent display derivation
//     (soul / 10000 * 87) so draw()/the skin overlay stay unchanged.
//   Rolls/balloons/kusudama never touch the gauge (their handlers never call
//   UpdateGamePlayInfo) ??already true here (ROUND 44 M7).
//
// Per-chart words: the arcade reads TamashiiPoint[????銝] + TamashiiNorm
// from each fumen's course header. TJA carries none of these, so the words
// are regenerated with the AUTHORING generator recovered from the full CHN05
// corpus (6,386 charts dumped, 4,721 unbranched+level-known fitted,
// 99.39% exact (????銝) triple match ??scratchpad/r48_corpus_scan.py /
// r48_solve2.py / r48_final_check.py):
//     raw     = TamashiiNorm / (n_notes * clear_rate)
//     ??      = round(raw)
//     ??      = round(raw * ok_mult)        ok_mult: 0.75 (e/n/h), 0.5 (oni)
//     銝     = -round(raw * loss_mult)
// where n_notes counts discrete judgeable notes only (don/ka/big, matching
// Player::reset_chart's gauge_total_notes), TamashiiNorm is fixed per tier
// (easy 6000, normal 7000, hard 7000, oni 8000 ??corpus-constant across all
// 1520 songs per tier), and clear_rate / loss_mult are per-tier-per-level
// constants solved by interval intersection over the corpus (all solved
// intervals in ENGINE_BINDINGS.md ROUND 48).
// ============================================================================

namespace {

bool r48_disabled() {
    // Verification-only escape hatch, same pattern as YATAIDON_R45_DISABLE:
    // default OFF = CHN05 model live.
    static const bool v = std::getenv("YATAIDON_R48_DISABLE") != nullptr;
    return v;
}

struct ChnGaugeRow { float clear_rate; float loss_mult; };

// CHN05 corpus-solved authoring constants, indexed [level-1], level clamped
// to each tier's table length (star ratings above the cap reuse the last row,
// mirroring the level distribution actually present in the corpus).
const ChnGaugeRow CHN_EASY[5] = {
    {36.0f, 0.5f}, {38.0f, 0.5f}, {38.0f, 0.5f}, {44.0f, 0.5f}, {44.0f, 0.5f}};
const ChnGaugeRow CHN_NORMAL[7] = {
    {45.936f, 0.5f}, {45.936f, 0.5f}, {48.669f, 0.5f}, {49.219f, 0.75f},
    {52.5f, 1.0f}, {52.5f, 1.0f}, {52.5f, 1.0f}};
const ChnGaugeRow CHN_HARD[8] = {
    {54.28f, 0.75f}, {54.25f, 0.75f}, {50.71f, 1.0f}, {48.415f, 7.0f/6.0f},
    {47.26f, 1.25f}, {48.12f, 1.25f}, {48.12f, 1.25f}, {48.12f, 1.25f}};
const ChnGaugeRow CHN_ONI[10] = {
    {56.603f, 1.6f}, {56.603f, 1.6f}, {56.603f, 1.6f}, {56.603f, 1.6f},
    {56.603f, 1.6f}, {56.603f, 1.6f}, {56.603f, 1.6f},
    {56.0f, 2.0f}, {61.0f, 2.0f}, {61.0f, 2.0f}};

// TamashiiNorm per tier (corpus-constant: every e=6000, n=7000, h=7000,
// m/x=8000 across all 1520 songs).
const int CHN_NORMA[4] = {6000, 7000, 7000, 8000};
// ok (?? multiplier per tier: 0.75 easy/normal/hard, 0.5 oni (corpus-exact).
const float CHN_OKM[4] = {0.75f, 0.75f, 0.75f, 0.5f};
// Clear-zone art tier: the three gauge plates' gold zones sit at segments
// 30/35/40 of 50 = raw soul 6000/7000/8000, so hard (norma 7000) uses the
// NORMAL-position art ??CHN05's norm table, not Green's hard-at-79% layout.
const int CHN_ART[4] = {0, 1, 1, 2};

int chn_round(double x) {
    return (int)std::floor(x + 0.5);
}

// UpdateTamashii's exact cast sequence with the corpus-constant ratio 65536
// and mult 1.0: (float)((float)(65536.0 * point) * (1/65536)). For every
// generated point magnitude this is numerically == (double)point; kept as a
// function so the fixed-point shape is explicit.
double chn_delta(int point) {
    return (double)((float)((float)(65536.0 * (double)point) * 0.000015258789f));
}

} // namespace

Gauge::Gauge()
    : gauge_length(0), gauge_max(87.0f), mode(GaugeMode::NORMAL),
      player_num(PlayerNum::P1), total_notes(0), difficulty(0), level(1),
      previous_length(0), is_clear(false), is_rainbow(false),
      tamashii_fire_change(nullptr), gauge_update_anim(nullptr) {}

Gauge::Gauge(GaugeMode mode, PlayerNum player_num, int total_notes, int difficulty, int level)
    : gauge_length(0), mode(mode), player_num(player_num), total_notes(total_notes),
      previous_length(0), is_clear(false), is_rainbow(false),
      tamashii_fire_change(nullptr), gauge_update_anim(nullptr) {

    if (mode == GaugeMode::NORMAL) {
        gauge_max = 87.0f;
        this->difficulty = std::min((int)Difficulty::ONI, difficulty);
        this->level = std::min(10, level);

        clear_start = {52, 60, 69, 69};

        chn_model = !r48_disabled();
        if (chn_model) {
            // r48: per-chart TamashiiPoint words from the corpus-fitted CHN05
            // authoring generator (see the block comment at the top of this
            // file). art/clear tier: hard shares normal's 7000 norma and thus
            // the normal-position clear-zone art.
            int d = this->difficulty;
            if (d < 0) d = 0;
            const ChnGaugeRow* rows;
            int nrows;
            switch (d) {
                case (int)Difficulty::EASY:   rows = CHN_EASY;   nrows = 5;  break;
                case (int)Difficulty::NORMAL: rows = CHN_NORMAL; nrows = 7;  break;
                case (int)Difficulty::HARD:   rows = CHN_HARD;   nrows = 8;  break;
                default:                      rows = CHN_ONI;    nrows = 10; break;
            }
            int lvl = this->level;
            if (lvl < 1) lvl = 1;
            if (lvl > nrows) lvl = nrows;
            const ChnGaugeRow& row = rows[lvl - 1];
            norma     = CHN_NORMA[d];
            art_index = CHN_ART[d];
            soul      = 0.0;
            if (total_notes > 0) {
                double raw = (double)norma / ((double)total_notes * row.clear_rate / 100.0);
                tp_great = chn_round(raw);
                tp_good  = chn_round(raw * CHN_OKM[d]);
                tp_loss  = -chn_round(raw * row.loss_mult);
            } else {
                tp_great = tp_good = tp_loss = 0;
            }
            string_diff = (art_index == 0) ? "_easy"
                        : (art_index == 1) ? "_normal" : "_hard";
            if (std::getenv("YATAIDON_R33_GLSTATE")) {
                spdlog::info("[r48gauge] init diff={} lvl={} notes={} norma={} "
                             "tp=({}, {}, {}) cr={} lossm={}",
                             d, lvl, total_notes, norma,
                             tp_great, tp_good, tp_loss,
                             row.clear_rate, row.loss_mult);
            }
        } else {
            art_index = std::min(2, this->difficulty);
            if (this->difficulty == (int)Difficulty::HARD) {
                string_diff = "_hard";
            } else if (this->difficulty == (int)Difficulty::NORMAL) {
                string_diff = "_normal";
            } else if (this->difficulty == (int)Difficulty::EASY) {
                string_diff = "_easy";
            } else {
                string_diff = "_hard";
            }
        }

        table = {
            {
                {36.0f, 0.75f, -0.5f},
                {38.0f, 0.75f, -0.5f},
                {38.0f, 0.75f, -0.5f},
                {44.0f, 0.75f, -0.5f},
                {44.0f, 0.75f, -0.5f},
            },
            {
                {45.939f, 0.75f, -0.5f},
                {45.939f, 0.75f, -0.5f},
                {48.676f, 0.75f, -0.5f},
                {49.232f, 0.75f, -0.75f},
                {52.5f, 0.75f, -1.0f},
                {52.5f, 0.75f, -1.0f},
                {52.5f, 0.75f, -1.0f},
            },
            {
                {54.325f, 0.75f, -0.75f},
                {54.325f, 0.75f, -0.75f},
                {50.774f, 0.75f, -1.0f},
                {48.410f, 0.75f, -1.17f},
                {47.246f, 0.75f, -1.25f},
                {48.120f, 0.75f, -1.25f},
                {48.120f, 0.75f, -1.25f},
                {48.120f, 0.75f, -1.25f},
            },
            {
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.603f, 0.5f, -1.6f},
                {56.0f, 0.5f, -2.0f},
                {61.428f, 0.5f, -2.0f},
                {61.428f, 0.5f, -2.0f},
            }
        };

        tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
        gauge_update_anim    = (FadeAnimation*)tex.get_animation(10);
    } else {
        // DAN: animations loaded lazily in update() since this object may be
        // constructed before the texture system is ready (class-member initializer)
        gauge_max = 89.0f;
        this->difficulty = 0;
        this->level      = 1;
    }
}

Gauge Gauge::make_result(GaugeMode mode, PlayerNum player_num, float gauge_length, bool is_2p) {
    Gauge g;
    g.is_result    = true;
    g.mode         = mode;
    g.player_num   = player_num;
    g.gauge_length = gauge_length;
    g.is_2p        = is_2p;

    if (mode == GaugeMode::NORMAL) {
        g.gauge_max    = 87.0f;
        g.result_scale = 10.0f / 11.0f;
        int seldiff    = global_data.session_data[(int)player_num].selected_difficulty;
        g.difficulty   = std::min((int)Difficulty::HARD, seldiff);
        g.clear_start  = {52, 60, 69};

        g.chn_model = !r48_disabled();
        if (g.chn_model) {
            // r48: CHN05 result state. gauge_length arrives as the 87-unit
            // display value (soul / 10000 * 87), so recover the raw soul and
            // apply IsNormaClear / IsTamashiiMax against the CHN05 norma
            // (hard = 7000, same as normal ??not Green's clear_start 69).
            int d = seldiff;
            if (d < 0) d = 0;
            if (d > (int)Difficulty::ONI) d = (int)Difficulty::ONI;
            g.norma     = CHN_NORMA[d];
            g.art_index = CHN_ART[d];
            g.string_diff = (g.art_index == 0) ? "_easy"
                          : (g.art_index == 1) ? "_normal" : "_hard";
            g.soul = (double)gauge_length / 87.0 * 10000.0;
        } else {
            g.art_index = g.difficulty;
            if (g.difficulty >= (int)Difficulty::HARD)        g.string_diff = "_hard";
            else if (g.difficulty >= (int)Difficulty::NORMAL) g.string_diff = "_normal";
            else                                              g.string_diff = "_easy";
        }

        // ROUND 57 (r57-dani-leftovers) ??the r50/r53/r54-flagged id collision,
        // fixed per ROUND 54's recorded recipe. Ids 17/20 were inherited from
        // upstream PyTaikoGreen, whose game/animation.json defines 17 as the
        // renda-breathing MOVE and 20 as the kusudama FADE; C-casting them to
        // FadeAnimation / TextureChangeAnimation made the result-gauge
        // "fade-in" consume a looping 0..60 move as alpha (opaque + a 500 ms
        // flicker) and the tamashii fire consume a one-shot 1->0 fade as a
        // frame index (parked on cel 0). The child skin now ships NEW ids
        // 70 (a real fade) and 71 (the real 8-cel fire texture_change, the
        // same cels id 25 carries) in Graphics/game/animation.json;
        // has_animation fail-softs to the historical 17/20 so the parent skin
        // is byte-for-byte unchanged.
        g.gauge_fade_in        = (FadeAnimation*)tex.get_animation(tex.has_animation(70) ? 70 : 17);
        g.tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(tex.has_animation(71) ? 71 : 20);
        g.gauge_fade_in->start();
        g.anims_loaded = true;

        if (gauge_length == g.gauge_max)
            g.state = ResultState::RAINBOW;
        else if (g.chn_model
                 ? (g.soul >= (double)g.norma - 0.5)   // -0.5: float round-trip guard
                 : (gauge_length >= g.clear_start[g.difficulty] - 1))
            g.state = ResultState::CLEAR;
        else
            g.state = ResultState::FAIL;
    } else {
        // DAN mode: animations loaded lazily in update()
        g.gauge_max = 89.0f;
        g.state = (gauge_length == g.gauge_max) ? ResultState::RAINBOW : ResultState::FAIL;
    }
    return g;
}

void Gauge::add_good() {
    if (gauge_update_anim) gauge_update_anim->start();
    previous_length = (int)gauge_length;

    if (mode == GaugeMode::NORMAL) {
        if (chn_model) {
            // CHN05 UpdateTamashii GREAT branch + UpdateScore clamp.
            soul += chn_delta(tp_great);
            if (soul > 10000.0) soul = 10000.0;
            if (soul < 0.0) soul = 0.0;
            gauge_length = (float)(soul / 10000.0 * gauge_max);
            if (std::getenv("YATAIDON_R33_GLSTATE"))
                spdlog::info("[r48gauge] good +{} soul={:.1f}", tp_great, soul);
        } else {
            gauge_length += (1.0f / total_notes) *
                            (100.0f * (clear_start[difficulty] / table[difficulty][level - 1].clear_rate));
        }
    } else {
        gauge_length += (1.0f / (total_notes * (gauge_max / 100.0f))) * 100.0f;
    }
    if (gauge_length > gauge_max) gauge_length = gauge_max;
}

void Gauge::add_ok() {
    if (gauge_update_anim) gauge_update_anim->start();
    previous_length = (int)gauge_length;

    if (mode == GaugeMode::NORMAL) {
        if (chn_model) {
            // CHN05 UpdateTamashii GOOD (?? branch + UpdateScore clamp.
            soul += chn_delta(tp_good);
            if (soul > 10000.0) soul = 10000.0;
            if (soul < 0.0) soul = 0.0;
            gauge_length = (float)(soul / 10000.0 * gauge_max);
            if (std::getenv("YATAIDON_R33_GLSTATE"))
                spdlog::info("[r48gauge] ok +{} soul={:.1f}", tp_good, soul);
        } else {
            gauge_length += ((1.0f * table[difficulty][level - 1].ok_multiplier) / total_notes) *
                            (100.0f * (clear_start[difficulty] / table[difficulty][level - 1].clear_rate));
        }
    } else {
        gauge_length += (0.5f / (total_notes * (gauge_max / 100.0f))) * 100.0f;
    }
    if (gauge_length > gauge_max) gauge_length = gauge_max;
}

void Gauge::add_bad() {
    previous_length = (int)gauge_length;

    if (mode == GaugeMode::NORMAL) {
        if (chn_model) {
            // CHN05 UpdateTamashii 銝/miss branch (tp_loss is negative)
            // + UpdateScore clamp at both ends.
            soul += chn_delta(tp_loss);
            if (soul > 10000.0) soul = 10000.0;
            if (soul < 0.0) soul = 0.0;
            gauge_length = (float)(soul / 10000.0 * gauge_max);
            if (std::getenv("YATAIDON_R33_GLSTATE"))
                spdlog::info("[r48gauge] bad {} soul={:.1f}", tp_loss, soul);
        } else {
            gauge_length += ((1.0f * table[difficulty][level - 1].bad_multiplier) / total_notes) *
                            (100.0f * (clear_start[difficulty] / table[difficulty][level - 1].clear_rate));
            if (gauge_length < 0) gauge_length = 0;
        }
        if (previous_length == gauge_max && gauge_length < gauge_max) {
            if (rainbow_fade_in.has_value()) rainbow_fade_in.reset();
            rainbow_start_ms = -1.0;
            rainbow_frac     = 0.0f;
        }
    } else {
        gauge_length -= (2.0f / (total_notes * (gauge_max / 100.0f))) * 100.0f;
        if (gauge_length < 0) gauge_length = 0;
    }
}

void Gauge::update(double current_ms) {
    if (is_result) {
        if (mode == GaugeMode::DAN && !anims_loaded) {
            tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
            // ROUND 57 ??this lazy path runs on whatever screen owns the
            // gauge at the time, and the parent dan_result/animation.json
            // carries only ids 0/1/25: an unguarded get_animation(63) THROWS
            // there (latent ??DAN_RESULT's gauge draw is currently a
            // screen-local widget, see dan_result.cpp). Same-typed fallback
            // chain: 63 (rainbow fade) -> 10 (gauge fade) -> nullptr.
            if (tex.has_animation(63))      gauge_fade_in = (FadeAnimation*)tex.get_animation(63);
            else if (tex.has_animation(10)) gauge_fade_in = (FadeAnimation*)tex.get_animation(10);
            if (gauge_fade_in) gauge_fade_in->start();
            anims_loaded = true;
        }

        if (state == ResultState::RAINBOW) {
            if (rainbow_start_ms < 0) rainbow_start_ms = current_ms;
            rainbow_frac = (float)fmod((current_ms - rainbow_start_ms) / kRainbowCelMs, 8.0);
        }
        if (tamashii_fire_change) tamashii_fire_change->update(current_ms);
        if (gauge_fade_in) gauge_fade_in->update(current_ms);
        return;
    }

    if (mode == GaugeMode::DAN && !anims_loaded) {
        tamashii_fire_change = (TextureChangeAnimation*)tex.get_animation(25);
        gauge_update_anim    = (FadeAnimation*)tex.get_animation(10);
        anims_loaded = true;
    }

    if (mode == GaugeMode::NORMAL && chn_model) {
        // r48: CHN05 IsTamashiiMax / IsNormaClear ??both against the raw soul.
        is_rainbow = (soul >= 10000.0);
        is_clear   = (soul >= (double)norma);
    } else {
        is_rainbow = (gauge_length == gauge_max);
        is_clear   = (mode == GaugeMode::NORMAL)
                     ? gauge_length > clear_start[std::min(difficulty, (int)Difficulty::HARD)] - 1
                     : is_rainbow;
    }

    if (gauge_length == gauge_max && !rainbow_fade_in.has_value()) {
        rainbow_fade_in = (FadeAnimation*)tex.get_animation(63);
        rainbow_fade_in.value()->start();
        rainbow_start_ms = current_ms;
    }

    if (gauge_update_anim)    gauge_update_anim->update(current_ms);
    if (tamashii_fire_change) tamashii_fire_change->update(current_ms);

    if (rainbow_fade_in.has_value()) {
        rainbow_fade_in.value()->update(current_ms);
        rainbow_frac = (float)fmod((current_ms - rainbow_start_ms) / kRainbowCelMs, 8.0);
    }
}

void Gauge::draw(float y) {
    if (mode == GaugeMode::NORMAL) {
        bool mirrored = y > tex.screen_height / 2.0f;
        Mirror mirror = mirrored ? Mirror::VERTICAL : Mirror::NONE;

        // `.index` picks the texture.json placement entry for the lane half
        // (0 = upper lane, y -72; 1 = lower lane, y -90) and EVERY other plate
        // in this function passes it. The border did not, so in the mirrored
        // lower lane it was placed 18 px too high while the player plate on top
        // of it was placed correctly - and since border_<diff>.png is not a
        // hollow frame but a byte-for-byte copy of 1p_unfilled_<diff>.png (rows
        // 45..78 are the 1P red groove, measured), those 18 px showed up under
        // the 2P plate as a stray red segmented strip: the "second soul gauge".
        // Measured before the fix at screen rows 843..848, x 738..1544, colour
        // (105,0,0); the 2P plate's own opaque span ends at row 842.
        tex.draw_texture(tex.get_enum("gauge/border" + string_diff), {.mirror = mirror, .y = y, .index = mirrored});

        tex.draw_texture(tex.get_enum("gauge/" + (std::to_string((int)player_num) + "p_unfilled" + string_diff)),
                         {.mirror = mirror, .y = y, .index = mirrored});

        // ====================================================================
        // ROUND 96 (r96-soulgauge-cellgrid) ??the ordinary gauge is drawn on
        // the CABINET's 50-cell / 21 px grid, not on this engine's historical
        // 87-cell / 12 px one.
        //
        // Measured from the shipped 39.06 art (scratchpad/r96/measure_overlay.py):
        // `overlay_{easy,normal,hard}.png` each carry 51 separator bars, 7 px
        // wide, at pitch EXACTLY 21.000 px (texture x 12+21k..18+21k, json base
        // x 720 -> screen 732+21k..738+21k, k = 0..50), and
        // `1p_unfilled_*.png`'s groove runs screen 738..1781 with the olive
        // 繧ｯ繝ｪ繧｢ zone starting at screen 1557 = 738 + 21*39. So X0 = 738,
        // pitch = 21, 50 cells, and the clear boundary is a whole cell.
        // ROUND 80 independently measured the same three plates as
        // 29+1+20 / 34+1+15 / 39+1+10 cells = clear cell 30 / 35 / 40.
        //
        // The cabinet itself stretches ONE fill shape (tamashii_gage.nulm frame
        // labels gage_0..gage_50; at gage_25 the fill's sx is 21.875 of a 24 px
        // column = 525 px = 21 x 25), i.e. it is a stretched bar whose length is
        // ALWAYS an integral multiple of 21 px. It never shows a partial cell,
        // and the visible cell separators are the overlay, not the fill art.
        // ROUND 48 already ported the quantisation rule itself:
        // EnsoGraphicTamashiiGage::Process, cell = (int)(soul / 10000 * 50)
        // (floor, 50 cells).
        //
        // WHY THIS CHANGED (user report: 縲檈oul gauge驥假ｼ梧怏骭ｯ 莉悶?譁ｰ蠅樊?謨ｸ
        // 謇?ｩ･縺ｯ荳?譬ｼ荳?譬ｼ 莉也怐襍ｷ譚･譛蓋ap縲?:
        // this branch used to draw the fill at `gauge_length_int * bar_width`
        // (87 cells x 12 px) and its gauge-up flash `<pn>p_bar_fade` ??a 24 px
        // SOLID slab whose texture.json base is 732 ??at `732 + 12*gauge_length_int`.
        // The visible gauge is the child skin's `arcade_gauge.lua` redraw on the
        // 21 px grid, and that file's `erase()` only repaints 24 px past the
        // arcade fill edge, so the engine's own flash survived up to
        //     18 + 12*floor(87p) - 21*floor(50p)  ==  +33 px
        // past the true cell boundary ??a detached, WRONG-HEIGHT (24x40 at json
        // y -31, vs the bar's 12x33 at y -27) orange fragment sitting one cell
        // ahead of the fill with unfilled groove in between. Measured live this
        // round on three frames (soul 945 / 2788 / 5996 -> +30 / +33 / +33 px,
        // predicted +30 / +33 / +33; scratchpad/r96/scan.py, crop
        // scratchpad/r96/crops/live_a_58_edge.png). ROUND 19's claim in
        // arcade_gauge.lua that "the engine's own copy is otherwise harmless
        // (this file's opaque redraw + erase() covers it)" is CORRECTED BY
        // ROUND 96: it was never covered.
        //
        // With the engine on the arcade grid the skin has nothing left to
        // correct, so `arcade_gauge.lua` no longer redraws the fill at all and
        // every seam it existed to hide is gone by construction.
        // ====================================================================
        constexpr int   kSegs   = 50;
        constexpr float kPitch  = 21.0f;
        constexpr float kX0     = 738.0f;   // groove start == 1p_bar/2p_bar json base
        // Clear cell per clear-zone art tier (0 easy / 1 normal+hard / 2 oni),
        // i.e. the index of the rounded TRANSITION cell + 1. ROUND 80 measured
        // the three plates as 29+1+20, 34+1+15, 39+1+10 cells.
        static const int kClearCell[3] = {30, 35, 40};

        const int art  = chn_model ? art_index : std::min(2, difficulty);
        const int cs   = kClearCell[std::max(0, std::min(2, art))];
        // The cabinet's own floor-to-50-cells. gauge_length is soul/10000*87,
        // so gauge_length/gauge_max is exactly the raw soul fraction.
        int cell = (int)((gauge_length / gauge_max) * (float)kSegs);
        if (cell < 0)     cell = 0;
        if (cell > kSegs) cell = kSegs;

        float bar_width      = tex.textures[tex.get_enum("gauge/" + std::to_string((int)player_num) + "p_bar")]->width;
        const int red_cells  = std::min(cell, cs - 1);

        if (red_cells > 0)
            tex.draw_texture(tex.get_enum("gauge/" + (std::to_string((int)player_num) + "p_bar")),
                             {.y = y, .x2 = kPitch * red_cells - bar_width, .index = mirrored});

        if (cell >= cs) {
            // bar_clear_transition is 24 px wide but its glyph is only the
            // first 21 columns (22..23 are fully transparent, measured), i.e.
            // exactly one arcade cell. Its json base is 741, six px right of
            // the fill's 738, so the offset carries the base delta.
            tex.draw_texture(GAUGE::BAR_CLEAR_TRANSITION,
                             {.mirror = mirror, .x = kX0 + kPitch * (cs - 1) - 741.0f,
                              .y = y, .index = mirrored});
            const int gold_cells = cell - cs;
            if (gold_cells > 0) {
                const float gw = kPitch * gold_cells - bar_width;
                tex.draw_texture(GAUGE::BAR_CLEAR_TOP,
                                 {.mirror = mirror, .x = kPitch * cs, .y = y,
                                  .x2 = gw, .index = mirrored});
                tex.draw_texture(GAUGE::BAR_CLEAR_BOTTOM,
                                 {.x = kPitch * cs, .y = y,
                                  .x2 = gw, .index = mirrored});
            }
        }

        if (cell == kSegs && rainbow_fade_in.has_value()) {
            float fade    = rainbow_fade_in.value()->attribute;
            int   frame_a = (int)rainbow_frac % 8;
            int   frame_b = (frame_a + 1) % 8;
            float t       = rainbow_frac - (int)rainbow_frac;
            tex.draw_texture(tex.get_enum("gauge/rainbow" + string_diff),
                             {.frame = frame_a, .mirror = mirror, .y = y, .fade = fade, .index = mirrored});
            tex.draw_texture(tex.get_enum("gauge/rainbow" + string_diff),
                             {.frame = frame_b, .mirror = mirror, .y = y, .fade = fade * t, .index = mirrored});
        }

        // ROUND 96: the gauge-up flash, on the same 21 px grid and FLUSH against
        // the true cell edge instead of straddling an 87-grid one. All three
        // *_fade textures are 24 px wide and share texture.json base x 732, six
        // px LEFT of the fill's 738 -- so the offset that puts their right edge
        // on the cell boundary is (X0 + pitch*cell - 24) - 732.
        //
        // The trigger is the CELL index rising, not the 87-unit display value:
        // "one cell at a time" is the whole point of the report this round
        // fixed. previous_length is the 87-unit value from before the last
        // judgement, so it is converted to cells the same way.
        const int prev_cell = (int)((previous_length / gauge_max) * (float)kSegs);
        if (cell <= kSegs && cell > prev_cell) {
            const float fx = kX0 + kPitch * cell - 24.0f - 732.0f;
            if (cell == cs) {
                tex.draw_texture(GAUGE::BAR_CLEAR_TRANSITION_FADE,
                                 {.mirror = mirror, .x = fx, .y = y,
                                  .fade = gauge_update_anim->attribute, .index = mirrored});
            } else if (cell > cs) {
                tex.draw_texture(GAUGE::BAR_CLEAR_FADE,
                                 {.x = fx, .y = y,
                                  .fade = gauge_update_anim->attribute, .index = mirrored});
            } else {
                tex.draw_texture(tex.get_enum("gauge/" + (std::to_string((int)player_num) + "p_bar_fade")),
                                 {.x = fx, .y = y,
                                  .fade = gauge_update_anim->attribute, .index = mirrored});
            }
        }

        // ROUND 96: the cabinet draws the separator overlay (tamashii_gage
        // shape 33) with colour-transform Amul = 77/256 = 0.30078 -- measured
        // by ROUND 80, which then composited to it in two passes (this 0.15
        // plus the skin's 0.17647). One pass at the real value is the same
        // result with no seam where the skin's pass did not reach.
        tex.draw_texture(tex.get_enum("gauge/overlay" + string_diff),
                         {.mirror = mirror, .y = y, .fade = kOverlayAlpha, .index = mirrored});

        int label_index = chn_model ? art_index : std::min(2, difficulty);
        // r48: CHN05 lights the ?胯??caption exactly at norma-clear
        // (is_clear = soul >= norma), not at the fill's pixel position.
        bool live_label_lit = chn_model ? is_clear : (cell >= cs);
        if (live_label_lit) {
            tex.draw_texture(tex.get_enum("gauge/clear_" + global_data.config->general.language),
                             {.y = y, .index = label_index + (mirrored * 3)});
            if (is_rainbow) {
                tex.draw_texture(GAUGE::TAMASHII_FIRE,
                                 {.frame = (int)tamashii_fire_change->attribute, .scale = 0.75f,
                                  .center = true, .y = y, .index = mirrored});
            }
            tex.draw_texture(GAUGE::TAMASHII, {.y = y, .index = mirrored});
            int fire_frame = (int)tamashii_fire_change->attribute;
            if (is_rainbow && (fire_frame == 0 || fire_frame == 1 || fire_frame == 4 || fire_frame == 5))
                tex.draw_texture(GAUGE::TAMASHII_OVERLAY, {.y = y, .fade = 0.5f, .index = mirrored});
        } else {
            tex.draw_texture(tex.get_enum("gauge/clear_dark_" + global_data.config->general.language),
                             {.y = y, .index = label_index + (mirrored * 3)});
            tex.draw_texture(GAUGE::TAMASHII_DARK, {.y = y, .index = mirrored});
        }
    } else {
        // DAN mode
        if (!anims_loaded) return;

        // ROUND 19 (r19-gauge): this used to stretch the bar by the RAW
        // continuous `gauge_length` (0..gauge_max=89), i.e. pixel-by-pixel
        // growth every frame -- "grows by percentage instead of whole
        // cells" report. The cabinet's own dan gauge (lumen
        // enso_dani/enso/tamashiigauge/tamashii_gauge.nulm -> gauge_lamp_mc)
        // is a single lamp clip on the SAME plate the main gauge uses --
        // MAPPING_hud ROUND 5c already measured this as "89 segs vs arcade
        // 50x21px" when it derived the 726/1794 overrun below -- so it is
        // quantised here to the identical 50-segment / 21px grid the main
        // gauge's Lua overlay uses for NORMAL mode. Entirely inside Gauge
        // (this class): no game_dan.cpp change, no new Lua hook, and DAN's
        // own `1p_unfilled`/`border`/`overlay` art is untouched (only the
        // FILL bar's stretch amount and start x move).
        constexpr int   kDanSegs  = 50;
        constexpr float kDanPitch = 21.0f;
        constexpr float kDanX0    = 738.0f;   // groove start, shared with NORMAL's X0
        constexpr float kDanBaseX = 726.0f;   // GAUGE_DAN::_1P_BAR's own texture.json x
        constexpr float kDanShift = kDanX0 - kDanBaseX;

        int dan_n = (int)((gauge_length / gauge_max) * kDanSegs);
        if (dan_n > kDanSegs) dan_n = kDanSegs;
        float bar_width  = (float)tex.textures[GAUGE_DAN::_1P_BAR]->width;
        float length_px  = dan_n * kDanPitch;

        tex.draw_texture(GAUGE_DAN::BORDER,      {});
        tex.draw_texture(GAUGE_DAN::_1P_UNFILLED, {});
        tex.draw_texture(GAUGE_DAN::_1P_BAR,     {.x = kDanShift, .x2 = length_px - bar_width});

        if (is_rainbow && rainbow_fade_in.has_value()) {
            float fade    = rainbow_fade_in.value()->attribute;
            int   frame_a = (int)rainbow_frac % 8;
            int   frame_b = (frame_a + 1) % 8;
            float t       = rainbow_frac - (int)rainbow_frac;
            tex.draw_texture(GAUGE_DAN::RAINBOW, {.frame = frame_a, .fade = fade});
            tex.draw_texture(GAUGE_DAN::RAINBOW, {.frame = frame_b, .fade = fade * t});
        }

        // ROUND 96: the DAN fill was already on the 50 x 21 px grid (ROUND 19),
        // so it does NOT share the ordinary gauge's off-grid overhang. Its
        // FLASH did: GAUGE_DAN::_1P_BAR_FADE is 32 px wide with texture.json
        // base x 716, and `kDanShift + length_px - bar_width` = 21*dan_n put
        // its right edge at 716 + 21*dan_n + 32 = the cell edge (738 + 21*dan_n)
        // PLUS 10 px. Placed flush instead, the same way NORMAL now is.
        // (Measured from Graphics/game/gauge_dan/texture.json + the shipped PNG
        // sizes, not observed live -- the dan overhang is 10 px, not the 33 px
        // the ordinary gauge showed, and no live dan capture was taken this
        // round. Flagged as such.)
        if ((int)gauge_length <= (int)gauge_max && (int)gauge_length > (int)previous_length) {
            const float dan_fade_w = (float)tex.textures[GAUGE_DAN::_1P_BAR_FADE]->width;
            tex.draw_texture(GAUGE_DAN::_1P_BAR_FADE,
                             {.x = (kDanX0 + length_px - dan_fade_w) - 716.0f,
                              .fade = gauge_update_anim->attribute});
        }

        tex.draw_texture(GAUGE_DAN::OVERLAY, {.fade = 0.15f});

        if (is_rainbow) {
            tex.draw_texture(GAUGE_DAN::TAMASHII_FIRE,
                             {.frame = (int)tamashii_fire_change->attribute, .scale = 0.75f, .center = true});
            tex.draw_texture(GAUGE_DAN::TAMASHII, {});
            int f = (int)tamashii_fire_change->attribute;
            if (f == 0 || f == 1 || f == 4 || f == 5)
                tex.draw_texture(GAUGE_DAN::TAMASHII_OVERLAY, {.fade = 0.5f});
        } else {
            tex.draw_texture(GAUGE_DAN::TAMASHII_DARK, {});
        }
    }
}

void Gauge::draw_result(double external_fade) {
    if (!anims_loaded) return;

    // NORMAL mode uses its own internal fade; DAN mode uses the caller-supplied fade
    double base_fade    = (mode == GaugeMode::NORMAL) ? gauge_fade_in->attribute : external_fade;
    double rainbow_fade = std::min((double)gauge_fade_in->attribute, external_fade);

    std::string player_str = std::to_string(static_cast<int>(player_num)) + "p";
    float scale = result_scale;

    if (mode == GaugeMode::NORMAL) {
        int gauge_length_int = (int)gauge_length;
        int clear_point      = chn_model ? clear_start[art_index] : clear_start[difficulty];
        int label_index      = chn_model ? art_index : (int)difficulty;
        float bar_width      = tex.textures[tex.get_enum("gauge/" + player_str + "_bar")]->width;

        tex.draw_texture(tex.get_enum("gauge/" + (player_str + "_unfilled" + string_diff)),
                         {.scale = scale, .fade = base_fade, .index = is_2p});

        if (state == ResultState::RAINBOW) {
            int frame_a = (int)rainbow_frac % 8;
            int frame_b = (frame_a + 1) % 8;
            float t = rainbow_frac - (int)rainbow_frac;
            tex.draw_texture(tex.get_enum("gauge/rainbow" + string_diff),
                             {.frame = frame_a, .scale = scale, .fade = base_fade, .index = is_2p});
            tex.draw_texture(tex.get_enum("gauge/rainbow" + string_diff),
                             {.frame = frame_b, .scale = scale, .fade = (float)base_fade * t, .index = is_2p});
        } else {
            tex.draw_texture(tex.get_enum("gauge/" + (player_str + "_bar")),
                             {.scale = scale,
                              .x2 = std::min(gauge_length_int * bar_width, (clear_point - 1) * bar_width) - bar_width,
                              .fade = base_fade, .index = is_2p});
            if (gauge_length_int >= clear_point - 1)
                tex.draw_texture(GAUGE::BAR_CLEAR_TRANSITION,
                                 {.scale = scale, .x = (clear_point - 1) * bar_width,
                                  .fade = base_fade, .index = is_2p});
            if (gauge_length_int > clear_point) {
                tex.draw_texture(GAUGE::BAR_CLEAR_TOP,
                                 {.scale = scale, .x = clear_point * bar_width,
                                  .x2 = (gauge_length_int - clear_point) * bar_width,
                                  .fade = base_fade, .index = is_2p});
                tex.draw_texture(GAUGE::BAR_CLEAR_BOTTOM,
                                 {.scale = scale, .x = clear_point * bar_width,
                                  .x2 = (gauge_length_int - clear_point) * bar_width,
                                  .fade = base_fade, .index = is_2p});
            }
        }

        tex.draw_texture(tex.get_enum("gauge/overlay" + string_diff),
                         {.scale = scale, .fade = std::min(0.15, base_fade), .index = is_2p});
        tex.draw_texture(GAUGE::FOOTER, {.scale = scale, .fade = base_fade, .index = is_2p});

        // r48: under the CHN05 model the bright ?胯??caption follows the
        // actual clear STATE (soul >= norma), not the fill's pixel position.
        bool label_lit = chn_model ? (state == ResultState::CLEAR || state == ResultState::RAINBOW)
                                   : (gauge_length_int >= clear_point - 1);
        if (label_lit) {
            tex.draw_texture(tex.get_enum("gauge/clear_" + global_data.config->general.language),
                             {.scale = scale, .fade = base_fade, .index = label_index + (is_2p * 3)});
            if (state == ResultState::RAINBOW) {
                tex.draw_texture(GAUGE::TAMASHII_FIRE,
                                 {.frame = (int)tamashii_fire_change->attribute, .scale = 0.75f * scale,
                                  .center = true, .fade = base_fade, .index = is_2p});
            }
            tex.draw_texture(GAUGE::TAMASHII, {.scale = scale, .fade = base_fade, .index = is_2p});
            int a = (int)tamashii_fire_change->attribute;
            if (state == ResultState::RAINBOW && (a == 0 || a == 1 || a == 4 || a == 5))
                tex.draw_texture(GAUGE::TAMASHII_OVERLAY,
                                 {.scale = scale, .fade = std::min(0.5, base_fade), .index = is_2p});
        } else {
            tex.draw_texture(tex.get_enum("gauge/clear_dark_" + global_data.config->general.language),
                             {.scale = scale, .fade = base_fade, .index = label_index + (is_2p * 3)});
            tex.draw_texture(GAUGE::TAMASHII_DARK, {.scale = scale, .fade = base_fade, .index = is_2p});
        }
    } else { // DAN mode
        float bar_width = (float)tex.textures[GAUGE::_1P_BAR]->width;
        float length_px = gauge_length * bar_width;

        tex.draw_texture(GAUGE::_1P_UNFILLED, {.fade = base_fade});

        if (state != ResultState::RAINBOW)
            tex.draw_texture(GAUGE::_1P_BAR, {.x2 = length_px - bar_width, .fade = base_fade});

        if (state == ResultState::RAINBOW) {
            int frame_a = (int)rainbow_frac % 8;
            int frame_b = (frame_a + 1) % 8;
            float t = rainbow_frac - (int)rainbow_frac;
            tex.draw_texture(GAUGE::RAINBOW, {.frame = frame_a, .fade = (float)rainbow_fade});
            tex.draw_texture(GAUGE::RAINBOW, {.frame = frame_b, .fade = (float)rainbow_fade * t});
        }

        tex.draw_texture(GAUGE::OVERLAY, {.fade = std::min(base_fade, 0.15)});
        tex.draw_texture(GAUGE::FOOTER,  {.fade = base_fade});

        if (state == ResultState::RAINBOW) {
            tex.draw_texture(GAUGE::TAMASHII_FIRE,
                             {.frame = (int)tamashii_fire_change->attribute, .scale = 0.75f,
                              .center = true, .fade = base_fade});
            tex.draw_texture(GAUGE::TAMASHII, {.fade = base_fade});
            int f = (int)tamashii_fire_change->attribute;
            if (f == 0 || f == 1 || f == 4 || f == 5)
                tex.draw_texture(GAUGE::TAMASHII_OVERLAY, {.fade = std::min(base_fade, 0.5)});
        } else {
            tex.draw_texture(GAUGE::TAMASHII_DARK, {.fade = base_fade});
        }
    }
}
