#include "noise.h"

#include <cmath>

#include "mathx.h"

namespace cl {

Noise::Noise(uint32_t seed) {
    uint8_t perm[256];
    for (int i = 0; i < 256; i++) perm[i] = (uint8_t)i;
    Rng rng(seed);
    for (int i = 255; i > 0; i--) {
        int j = (int)(rng.next() % (uint32_t)(i + 1));
        uint8_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (int i = 0; i < 512; i++) p_[i] = perm[i & 255];
}

static inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline float grad(int h, float x, float y) {
    switch (h & 7) {
        case 0: return x + y;
        case 1: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x;
        case 5: return -x;
        case 6: return y;
        default: return -y;
    }
}

float Noise::perlin(float x, float y) const {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    int X = xi & 255, Y = yi & 255;
    float u = fade(xf), v = fade(yf);
    int aa = p_[p_[X] + Y], ab = p_[p_[X] + Y + 1], ba = p_[p_[X + 1] + Y], bb = p_[p_[X + 1] + Y + 1];
    float x1 = mixf(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
    float x2 = mixf(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);
    return mixf(x1, x2, v) * 0.7071f;
}

float Noise::fbm(float x, float y, int oct, float lac, float gain) const {
    float s = 0, a = 1, n = 0;
    for (int i = 0; i < oct; i++) {
        s += perlin(x, y) * a;
        n += a;
        a *= gain;
        x = x * lac + 17.3f;
        y = y * lac + 9.1f;
    }
    return s / n;
}

float Noise::ridged(float x, float y, int oct) const {
    float s = 0, a = 1, n = 0, w = 1;
    for (int i = 0; i < oct; i++) {
        float r = 1.0f - std::fabs(perlin(x, y));
        r *= r;
        r *= w;
        w = clamp01(r * 1.8f);
        s += r * a;
        n += a;
        a *= 0.5f;
        x = x * 2.03f + 11.7f;
        y = y * 2.03f + 3.9f;
    }
    return s / n;
}

}  // namespace cl
