#pragma once

#include "../enums.h"
#include "../../libs/animation.h"

struct TextureObject;

class LaneHitEffect {
private:
    DrumType type;
    Judgments judgment;
    FadeAnimation* fade;
    TextureObject* t_effect = nullptr;

public:
    LaneHitEffect(DrumType type, Judgments judgment);

    void update(double current_ms);

    void draw(float y);

    bool is_finished() const;
};
