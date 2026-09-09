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
                 float v_advance = 1.0f);

    ~OutlinedText();

    bool upload_pending();

    bool is_ready() const { return texture.has_value(); }

    void finish();

    void draw(const DrawTextureParams& = {});
};

extern FontManager font_manager;
