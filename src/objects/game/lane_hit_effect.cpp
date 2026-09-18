#include "lane_hit_effect.h"
#include "../../libs/texture.h"
#include <spdlog/spdlog.h>

LaneHitEffect::LaneHitEffect(DrumType type, Judgments judgment)
            : type(type), judgment(judgment) {
    fade = dynamic_cast<FadeAnimation*>(tex.get_animation(0, true));
    fade->start();
}

void LaneHitEffect::update(double current_ms) {
    fade->update(current_ms);
}

void LaneHitEffect::draw(float y) {
    if (type == DrumType::DON) {
        tex.draw_texture(LANE::LANE_HIT_EFFECT, {.frame=0, .y=y, .fade=fade->attribute});
    } else if (type == DrumType::KAT) {
        tex.draw_texture(LANE::LANE_HIT_EFFECT, {.frame=1, .y=y, .fade=fade->attribute});
    } else {
        spdlog::warn("LaneHitEffect::draw: unexpected DrumType {}", static_cast<int>(type));
    }
    if (judgment == Judgments::GOOD || judgment == Judgments::OK) {
        tex.draw_texture(LANE::LANE_HIT_EFFECT, {.frame=2, .y=y, .fade=fade->attribute});
    }

}

bool LaneHitEffect::is_finished() const {
    return fade->is_finished;
}
