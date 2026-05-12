#pragma once

#include <stdint.h>

struct ClawdAnimationAsset {
    const char* name;
    uint16_t width;
    uint16_t height;
    uint16_t frame_count;
    uint16_t frame_delay_ms;
    const uint16_t* frames;
};

struct ClawdPosterAsset {
    const char* name;
    uint16_t width;
    uint16_t height;
    const uint16_t* pixels;
};

extern const ClawdAnimationAsset kClawdSplashAnimation;
extern const ClawdPosterAsset kClawdGalleryPosters[];
extern const uint8_t kClawdGalleryPosterCount;
