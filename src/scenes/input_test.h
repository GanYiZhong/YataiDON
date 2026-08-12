#pragma once

#include "../libs/screen.h"
#include "../objects/game/drum_hit_effect.h"

class InputTestDrumEffect : public DrumHitEffect {
private:
    float x_offset;
    float y_offset;

public:
    InputTestDrumEffect(DrumType type, Side side, float x_offset, float y_offset)
        : DrumHitEffect(type, side), x_offset(x_offset), y_offset(y_offset) {}

    void draw(float) override {
        if (type == DrumType::DON) {
            // The skin ships a single full-face don flash, so clip it to the
            // half that was actually hit - otherwise left and right don look
            // identical on the input test.
            auto& drum   = *tex.textures[PRACTICE::LARGE_DRUM];
            float left   = x_offset + (float)drum.x[0];
            float centre = left + drum.width / 2.0f;
            float right  = left + (float)drum.width;
            int sx = virtual_to_screen_x(side == Side::LEFT ? left   : centre);
            int ex = virtual_to_screen_x(side == Side::LEFT ? centre : right);
            ray::BeginScissorMode(sx, 0, ex - sx, ray::GetScreenHeight());
            tex.draw_texture(PRACTICE::LARGE_DRUM_DON, {.x = x_offset, .y = y_offset, .fade = fade->attribute});
            ray::EndScissorMode();
        } else if (side == Side::LEFT) {
            tex.draw_texture(PRACTICE::LARGE_DRUM_KAT_L, {.x = x_offset, .y = y_offset, .fade = fade->attribute});
        } else {
            tex.draw_texture(PRACTICE::LARGE_DRUM_KAT_R, {.x = x_offset, .y = y_offset, .fade = fade->attribute});
        }
    }
};

class InputTestScreen : public Screen {
private:
    std::vector<std::unique_ptr<DrumHitEffect>> hit_effects;
    float drum_x_offset = 0;
    float drum_y_offset = 0;

public:
    InputTestScreen() : Screen("input_test") {}

    void on_screen_start() override;
    std::optional<Screens> update() override;
    void draw() override;
};
