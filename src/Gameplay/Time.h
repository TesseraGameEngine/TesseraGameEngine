#pragma once

#include <cstdint>

namespace Gameplay {

class Time {
public:
    static inline float deltaTime{0.0f};
    static inline float fixedDeltaTime{1.0f / 60.0f};
    static inline float time{0.0f};
    static inline float timeScale{1.0f};
    static inline uint64_t frameCount{0};

    static void Reset() {
        deltaTime = 0.0f;
        time = 0.0f;
        timeScale = 1.0f;
        frameCount = 0;
    }
};

} // namespace Gameplay

