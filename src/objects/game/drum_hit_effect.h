#pragma once

#include "../enums.h"
#include "../../libs/animation.h"

struct TextureObject;

class DrumHitEffect {
protected:
    DrumType type;
    Side side;
    FadeAnimation* fade;
    TextureObject* t_effect = nullptr;

public:
    DrumHitEffect(DrumType type, Side side);
    virtual ~DrumHitEffect() = default;

    void update(double current_ms);

    virtual void draw(float y);

    bool is_finished() const;

};
