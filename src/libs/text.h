#pragma once

#include "texture.h"
#include <future>

class FontManager {
private:
    fs::path font_path;

    // One glyph atlas per requested pixel size. raylib's ImageTextEx/DrawTextEx
    // compose at font.baseSize and rescale to the requested size; keeping
    // baseSize == the size we draw at removes that resample entirely.
    struct SizedFont {
        ray::Font font{};
        std::unordered_set<int> codepoints;  // only what was asked for AT THIS SIZE
        uint64_t last_used = 0;
        bool loaded = false;
    };

    std::unordered_map<int, SizedFont> fonts;
    uint64_t use_clock = 0;

    // A 1x1 texture handed to worker-thread font copies so their validity does
    // not depend on the lifetime of the atlas they were copied from.
    ray::Texture sentinel_texture{};

    mutable std::mutex font_mutex;

    // Memory bound: at most this many atlases resident, LRU-evicted. Each entry
    // rasterises only the codepoints requested at its own size, so the total is
    // sum(codepoints_at_size * size^2) rather than all-codepoints * max_size^2.
    static constexpr size_t MAX_SIZED_FONTS = 32;

    SizedFont& acquire(const std::string& text, int font_size);  // font_mutex held
    void evict_lru(int keep_size);                               // font_mutex held

public:
    FontManager();
    void init(const fs::path& font_path);
    ray::Font get_font(const std::string& text, int font_size);
    // Returns a deep copy of the font for `font_size`, safe to use on another
    // thread. Caller must UnloadFont() the returned font when done (after
    // clearing .texture, which is shared).
    ray::Font copy_font(const std::string& text, int font_size);
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
                 // In OUTLINE-RADIUS units BEFORE the screen_scale multiply below.
                 // Float since ROUND 16: a Lumen `border` is already in 1080p stage
                 // px, so an arcade skin passes `border / screen_scale`, which is
                 // rarely an integer (record borders include 1.5/3.5/4.5/6.5/7.5).
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
