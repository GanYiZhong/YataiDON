#pragma once

// ROUND 70 (r70-danresult-text-handoff) -- the dan condition THRESHOLD caption.
//
// The cabinet draws it as ONE dynamic text field, never as a digit sheet plus
// suffix sprites. The authored records (absolute paths only --
// `lumen_font_dir_for` rejects a path whose FIRST component is `lumen` and then
// silently renders zero glyphs):
//
//   lumen_shape_dump <abs>\dani_enso_detail.nulm  ... -m -r 239 -O 901 631 -v
//       id=186  tx_border  M=(1,0,0,1, -682, -27.5)  size=36 align=1
//                          box=(-2,-2, 278,62)   atlas bucket _64  border 4.00
//   lumen_shape_dump <abs>\dani_result_detail.nulm ... -m -r 234 -O 1100 425 -v
//       id=119  tx_border  M=(1,0,0,1, -600, -22.5)  size=36 align=1
//                          box=(-2,-2, 268,62)   atlas bucket _64  border 4.00
//       id=226  tx_border  M=(1,0,0,1, +439/-26/-491, 24.5) size=20 align=1
//                          box=(-2,-2, 238,52)   atlas bucket _30  border 3.50
//   (`--check-text` prints each record's border width; the border COLOUR is a
//    renderer-side default the harness does not extract, so it is not read from
//    the clip -- see MAPPING.md ROUND 70 for how it was settled.)
//
// `align=1` is RIGHT, not LEFT. Rendering the same field with 1/3/5/7 digits
// pins the RIGHT edge and moves the left one, on all three records
// (scratchpad/r70/m_align2.py). ROUND 67's hand-off note annotated it "LEFT";
// that annotation is wrong and is corrected here.
//
// The string is a wordlist format row, not a literal:
//   requirement_or_higher_pre    jp "%s 以上"     en "%s or more"
//   requirement_less_than_pre    jp "%s 未満"     en "Less than %s"
//   requirement_soul_gauge_pre   jp "%s ％以上"   en "%s%% or higher"
// i.e. the number, ONE ascii space, then the suffix -- and nothing else.
//
// CORRECTED BY ROUND 94 -- this block previously read
//     "requirement_soul_gauge  jp \"%s ％\"  en \"%s %%\""
// and concluded "A soul gauge condition takes `requirement_soul_gauge` (no 以上)".
// That is wrong on two counts and it is the 「"100%"文字要改為"100 以上"」 defect.
//   (a) It was read out of CHN05's `data/wordlist.msg`. Shipped text must come
//       from 39.06. In 39.06's OWN `datatable/wordlist.bin`, the bare key
//       `requirement_soul_gauge` is the EMPTY STRING in every language column;
//       so are `requirement_or_higher_<n>_<i>` and `requirement_less_than_<n>_<i>`.
//       The rows that carry the FORMAT are the `_pre` siblings, and the native
//       code fills the bare rows in from them at runtime -- no .lua does it.
//   (b) `requirement_soul_gauge_pre` is jp "%s ％以上" / en "%s%% or higher" /
//       tw "%s％以上" / kr "%s％이상". The ％ stays AND 以上 is there.
// Independently confirmed by the movie's own baked default text:
//   lumen_shape_dump <abs>\tamashii_gauge.nulm ... -m -r 145 -f 10 -v -L jp
//       id=144  tx_border  M=(1,0,0,1, -113, 10)  size=25 align=2
//                          box=(-2,-2, 228,52)  border 3.50  rgba 255,255,255,255
//                          text="100%以上"          <-- the DAN_RESULT record
//   (`-f 10` is sprite 145's `dani_result` label. Frame 0 is `dani_enso` and is a
//    DIFFERENT record, id=141, size 36 align=1, whose baked text is also
//    "100%以上". Both screens say 以上.)
//
// 以上 vs 未満 ON A GAUGE ROW: the cabinet has NO 未満 gauge row. There is exactly
// one gauge format row and 以上 is baked into it; neither call site branches on
// range (DaniResultTotalBase.lua:77 and dani_select_theme_disp_data.lua:141 both
// SetText the gauge key unconditionally -- the or_higher/less_than pair is used
// only for the score/count rows). Our data agrees: across every Songs/**/dan.json
// all 25 `gauge` exams are range "more"; `less` occurs only on judgebad/judgegood.
// So 以上 is a FIXED suffix here. `dan_exam_text_gauge_less` below is an ENGINE
// INVENTION with no cabinet counterpart -- an unreachable defensive branch so a
// hypothetical `less` gauge cannot silently print 以上; nothing measured backs its
// wording.
//
// This engine composed the same caption from `exam_border_counter` at a fixed
// 26 px pitch, plus separate ％ and 以上/未満 sprites, plus invented 4 px and
// 10 px gaps -- and LEFT-anchored the whole thing on DAN_RESULT. That is the
// reported 「條件的繪製很醜(xxx以上，未滿)這些字體都不對」 and the GAME_DAN half of
// 「xxx以上的位置偏了」: one defect, one root cause, both screens.
//
// Opt-in: a skin turns this on by declaring `dan_exam_text_more`; a skin that
// never does (PyTaikoGreen) keeps the sprite composition bit-for-bit.

#include "../../libs/text.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

// Builds the caption exactly as the wordlist row does. `type`/`range` are the
// dan.json exam fields; the format strings live in skin_config so a skin owns
// its own localisation, with the cabinet's Japanese as the built-in fallback.
inline std::string exam_threshold_text(const TextureWrapper& tex,
                                       const std::string& type,
                                       const std::string& range,
                                       int value,
                                       const std::string& lang) {
    const bool gauge = (type == "gauge");
    const bool less  = (range == "less");
    const char* key = gauge ? (less ? "dan_exam_text_gauge_less" : "dan_exam_text_gauge")
                    : less  ? "dan_exam_text_less"
                            : "dan_exam_text_more";
    // Narrow literals: the source is UTF-8 and so is the execution charset.
    // ROUND 94 -- the gauge row is "%s ％以上" (requirement_soul_gauge_pre), NOT
    // "%s ％". The `less` gauge row has no cabinet counterpart; see the header.
    const char* jp  = gauge ? (less ? "%s \xEF\xBC\x85\xE6\x9C\xAA\xE6\xBA\x80"   // "%s ％未満" (invented)
                                    : "%s \xEF\xBC\x85\xE4\xBB\xA5\xE4\xB8\x8A")  // "%s ％以上"
                    : less  ? "%s \xE6\x9C\xAA\xE6\xBA\x80"                       // "%s 未満"
                            : "%s \xE4\xBB\xA5\xE4\xB8\x8A";                      // "%s 以上"
    std::string fmt = tex.skin_text(key, lang, tex.skin_text(key, "ja", jp));
    const std::string num = std::to_string(value);
    // Only "%s" is substituted; "%%" is an escaped percent (the en rows use it).
    std::string out;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == 's') { out += num; i++; }
        else if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] == '%') { out += '%'; i++; }
        else out += fmt[i];
    }
    return out;
}

// One OutlinedText per (string, size). A threshold changes only when the exam,
// the value or the language does, and OutlinedText rasterises on a worker
// thread, so it must never be rebuilt per frame. Cleared with the scene, before
// the FontManager goes (ROUND 69's `between.stop()` lesson).
class ExamCaptionCache {
public:
    // `outline` is in OutlinedText's outline-RADIUS units BEFORE its own
    // screen_scale multiply, i.e. an arcade skin passes `record border /
    // screen_scale` (text.h:77, and skin_config's `outline` is deliberately
    // never scaled on load -- texture.cpp:76).
    OutlinedText* get(const std::string& s, int size, float outline,
                      ray::Color fill = ray::WHITE, ray::Color ol = ray::BLACK) {
        if (s.empty() || size <= 0) return nullptr;
        const std::string k = s + "\x1f" + std::to_string(size) + "\x1f"
                            + std::to_string((int)(outline * 100));
        auto it = items.find(k);
        if (it == items.end())
            it = items.emplace(k, std::make_unique<OutlinedText>(
                     s, size, fill, ol, false, outline)).first;
        return it->second.get();
    }

    void clear() { items.clear(); }

    // OutlinedText draws its glyphs inset by `pad` on both axes inside its own
    // image, and `width`/`height` include that padding twice (text.cpp:294-298).
    // Callers anchoring to a GLYPH edge rather than an image edge need it.
    static float pad_for(float outline, float screen_scale) {
        return (float)((int)(outline * screen_scale) + 2);
    }

private:
    std::map<std::string, std::unique_ptr<OutlinedText>> items;
};
