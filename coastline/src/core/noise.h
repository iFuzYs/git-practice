// Градиентный шум и фрактальные суммы для рельефа и текстур
#pragma once
#include <cstdint>

namespace cl {

class Noise {
public:
    explicit Noise(uint32_t seed = 7);
    float perlin(float x, float y) const;             // [-1, 1]
    float fbm(float x, float y, int oct, float lac = 2.0f, float gain = 0.5f) const;
    float ridged(float x, float y, int oct) const;     // [0, 1], острые хребты
private:
    uint8_t p_[512];
};

}  // namespace cl
