#pragma once

#include "../../../libs/text.h"
#include <string>
#include <unordered_set>
#include <vector>

inline bool language_is_cjk(const std::string& lang) {
    static const std::unordered_set<std::string> cjk = {
        "ja", "zh", "ko", "zh_tw", "zh-tw", "zh_cn", "zh-cn",
    };
    return cjk.count(lang) > 0;
}

inline std::string word_wrap(const std::string& text, int font_size, float spacing, float max_width) {
    ray::Font font = font_manager.get_font(text, font_size);

    std::vector<std::string> words;
    size_t start = 0;
    while (start < text.size()) {
        size_t sp = text.find(' ', start);
        if (sp == std::string::npos) { words.push_back(text.substr(start)); break; }
        words.push_back(text.substr(start, sp - start));
        start = sp + 1;
    }

    std::string wrapped, current;
    for (const auto& w : words) {
        std::string candidate = current.empty() ? w : current + " " + w;
        float width = ray::MeasureTextEx(font, candidate.c_str(), (float)font_size, spacing).x;
        if (width > max_width && !current.empty()) {
            wrapped += current + "\n";
            current = w;
        } else {
            current = candidate;
        }
    }
    if (!current.empty()) wrapped += current;
    return wrapped;
}
