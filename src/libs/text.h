#pragma once

#include "texture.h"
#include <future>

class FontManager {
private:
    fs::path font_path;

    struct SizedFont {
        ray::Font font{};                    // texture + recs + glyph structs; glyph images alias `cache`
        std::unordered_set<int> codepoints;  // only what was asked for AT THIS SIZE
        std::vector<ray::GlyphInfo> cache;   // rasterized once per codepoint (images owned here)
        bool atlas_dirty = false;
        uint64_t last_used = 0;
        bool loaded = false;
    };

    std::vector<unsigned char> font_data;    // the .ttf bytes, read once

    void rasterize_new(SizedFont& entry, int font_size, const std::vector<int>& cps);   // font_mutex held
    void rebuild_atlas(SizedFont& entry, int font_size);                                // font_mutex held
    void release_font(SizedFont& entry);                                                // font_mutex held
    void release_cache(SizedFont& entry);                                               // font_mutex held
    bool register_codepoints(SizedFont& entry, int font_size, const std::string& text); // font_mutex held

    std::unordered_map<int, SizedFont> fonts;
    uint64_t use_clock = 0;

    ray::Texture sentinel_texture{};

    mutable std::mutex font_mutex;

    static constexpr size_t MAX_SIZED_FONTS = 32;

    SizedFont& acquire(const std::string& text, int font_size);  // font_mutex held
    void evict_lru(int keep_size);                               // font_mutex held

public:
    FontManager();
    void init(const fs::path& font_path);
    void unload();
    ray::Font get_font(const std::string& text, int font_size);
    ray::Font copy_font(const std::string& text, int font_size);
    // Rasterize the glyphs `text` needs at `font_size` now, without rebuilding the
    // atlas. Call it for a whole batch (every song title of a genre, every lyric
    // line of a chart) before the OutlinedTexts are created, so the atlas is
    // rebuilt once for the batch instead of once per new string.
    void register_text(const std::string& text, int font_size);
};

class OutlinedText {
private:
    std::string text;
    float font_size;
    float outline_thickness;
    float v_advance = 1.0f;

    ray::Font worker_font;

    std::optional<ray::Image> pending_image;
    mutable std::mutex pending_mutex;

    std::optional<ray::Texture> texture;

    std::future<void> build_future;

    struct BuildData { ray::Image img; };

    BuildData build_horizontal_text(ray::Color color, ray::Color outline_color, float spacing);
    BuildData build_vertical_text  (ray::Color color, ray::Color outline_color, float spacing);

public:
    float width  = 0.0f;
    float height = 0.0f;
    float x_offset = 0.0f;
    float y_offset = 0.0f;

    OutlinedText(std::string text, int font_size,
                 ray::Color color, ray::Color outline_color,
                 bool is_vertical,
                 float outline_thickness = 5.0f,
                 float spacing = 2.0f,
                 float v_advance = 1.0f,
                 FontManager* fonts = nullptr);      // nullptr = the skin's main font

    ~OutlinedText();

    bool upload_pending();

    bool is_ready() const { return texture.has_value(); }
    const ray::Texture& texture_ref() const { return *texture; }
    // Recolour the rendered text top-to-bottom from `top` to `bottom` over its ink rows
    // (meant for a fill-only text: outline 0, white). Forces the build to finish.
    void tint_vertical_gradient(ray::Color top, ray::Color bottom);
    void tint_vertical_stops(const std::vector<std::pair<float, ray::Color>>& stops);   // multi-stop vertical gradient (gloss bands)
    void tint_stroke_stops(const std::vector<std::pair<float, ray::Color>>& stops);     // the same gradient, but per stroke: t runs from each stroke's own top edge to its bottom edge
    void post_emboss(float radius, float strength, float lx, float ly);                 // shade the fill by its edge slope: lit towards (lx,ly), dark on the far side
    // Resample the rendered text to `sx` of its width (bicubic) so it can be drawn 1:1.
    void post_squeeze(float sx);
    // Soften the rendered text: box-blur the alpha by `radius` px (canvas grows to fit).
    void post_blur(float radius);
    // Steepen the alpha ramp at the edges (k > 1 = crisper, like the cabinet's baked art).
    void post_sharpen(float k);
    // Thicken (>0) or thin (<0) the strokes by shifting the alpha threshold: `px` in pixels, |px| <= ~0.8.
    void post_weight(float px);

    void finish();

    void draw(const DrawTextureParams& = {});
};

extern FontManager font_manager;
// The UI/label face (Graphics/font_label.ttf and its per-language siblings): the cabinet
// draws its 30/32pt captions from a rounded gothic and only headings from the main face.
// Falls back to the main font file when the skin has none.
extern FontManager label_font_manager;
