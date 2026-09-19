#pragma once

#include "../../libs/ray.h"

struct TextureObject;

class JudgeCounter {
private:
    int good;
    int ok;
    int bad;
    int drumrolls;
    ray::Color orange;
    ray::Color white;
    TextureObject* t_counter = nullptr;
    TextureObject* t_bg = nullptr;
    TextureObject* t_total_percent = nullptr;
    TextureObject* t_judgments = nullptr;
    TextureObject* t_drumrolls = nullptr;
    TextureObject* t_percent = nullptr;

    void draw_counter(float counter, float x, float y, float margin, ray::Color color);

public:
    JudgeCounter();

    void update(int good, int ok, int bad, int drumrolls);
    void draw();
};
