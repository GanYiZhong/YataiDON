#include "fps_counter.h"
#include "../../libs/texture.h"
#include "../../libs/text.h"

FPSCounter::FPSCounter() {
    lastTime = get_current_ms();
    for (int i = 0; i < SAMPLE_SIZE; i++) {
        frameTimes[i] = 16.67f;
    }
}

void FPSCounter::update() {
    double currentTime = get_current_ms();
    float deltaTime = currentTime - lastTime;
    lastTime = currentTime;

    frameTimes[currentFrame % SAMPLE_SIZE] = deltaTime;
    currentFrame++;
}

float FPSCounter::get_fps() {
    float sum = 0;
    for (int i = 0; i < SAMPLE_SIZE; i++) {
        sum += frameTimes[i];
    }
    float avgFrameTime = sum / SAMPLE_SIZE;
    return 1000.0f / avgFrameTime;
}

void FPSCounter::draw() {}
