// Небольшая математика для симуляции: векторы, кватернионы, утилиты.
// Не зависит от raylib, чтобы симуляцию можно было тестировать без окна.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cl {

// raylib объявляет PI макросом с тем же значением — не конфликтуем с ним
#ifndef PI
constexpr float PI = 3.14159265358979f;
#endif
constexpr float TAU = 6.28318530717959f;
constexpr float DEG = PI / 180.0f;
constexpr float G = 9.81f;

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float clamp01(float v) { return clampf(v, 0.0f, 1.0f); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline float signf(float v) { return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f); }
inline float sq(float v) { return v * v; }
inline float smoothstep(float e0, float e1, float x) {
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
inline float wrapAngle(float a) {
    while (a > PI) a -= TAU;
    while (a < -PI) a += TAU;
    return a;
}
// Экспоненциальное сглаживание, не зависящее от шага времени
inline float damp(float cur, float target, float rate, float dt) {
    return mixf(target, cur, std::exp(-rate * dt));
}
inline float moveTowards(float cur, float target, float maxDelta) {
    if (std::fabs(target - cur) <= maxDelta) return target;
    return cur + signf(target - cur) * maxDelta;
}

struct V3 {
    float x = 0, y = 0, z = 0;
    constexpr V3() = default;
    constexpr V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator/(float s) const { return {x / s, y / s, z / s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(V3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(V3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline V3 operator*(float s, V3 v) { return v * s; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float len2(V3 v) { return dot(v, v); }
inline float len(V3 v) { return std::sqrt(dot(v, v)); }
inline V3 norm(V3 v) {
    float l = len(v);
    return l > 1e-8f ? v / l : V3{0, 0, 0};
}
inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline V3 mulv(V3 a, V3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dist2xz(V3 a, V3 b) { return sq(a.x - b.x) + sq(a.z - b.z); }
inline float distxz(V3 a, V3 b) { return std::sqrt(dist2xz(a, b)); }
inline bool finite(V3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
// Проекция на плоскость с нормалью n
inline V3 projPlane(V3 v, V3 n) { return v - n * dot(v, n); }

// Кватернион (x, y, z, w). Базис машины: +Z — вперёд, +Y — вверх, +X — влево.
struct Q {
    float x = 0, y = 0, z = 0, w = 1;
};
inline Q qmul(Q a, Q b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
inline Q qconj(Q q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Q qnorm(Q q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return Q{};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}
inline Q qaxis(V3 axis, float ang) {
    V3 a = norm(axis);
    float s = std::sin(ang * 0.5f);
    return {a.x * s, a.y * s, a.z * s, std::cos(ang * 0.5f)};
}
inline Q qyaw(float yaw) { return qaxis({0, 1, 0}, yaw); }
inline V3 qrot(Q q, V3 v) {
    // v' = v + 2w(u×v) + 2u×(u×v)
    V3 u{q.x, q.y, q.z};
    V3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}
// Интегрирование ориентации по угловой скорости (мировой)
inline Q qintegrate(Q q, V3 w, float dt) {
    Q wq{w.x, w.y, w.z, 0};
    Q d = qmul(wq, q);
    q.x += 0.5f * dt * d.x;
    q.y += 0.5f * dt * d.y;
    q.z += 0.5f * dt * d.z;
    q.w += 0.5f * dt * d.w;
    return qnorm(q);
}
inline Q qslerp(Q a, Q b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        return qnorm({mixf(a.x, b.x, t), mixf(a.y, b.y, t), mixf(a.z, b.z, t), mixf(a.w, b.w, t)});
    }
    float th = std::acos(clampf(d, -1, 1)), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}
// Кватернион из базиса (вперёд, вверх)
inline Q qlook(V3 fwd, V3 up) {
    V3 f = norm(fwd);
    V3 l = norm(cross(up, f));
    V3 u = cross(f, l);
    // матрица столбцами: X=l, Y=u, Z=f
    float m00 = l.x, m01 = u.x, m02 = f.x;
    float m10 = l.y, m11 = u.y, m12 = f.y;
    float m20 = l.z, m21 = u.z, m22 = f.z;
    float tr = m00 + m11 + m22;
    Q q;
    if (tr > 0) {
        float s = std::sqrt(tr + 1.0f) * 2;
        q.w = 0.25f * s; q.x = (m21 - m12) / s; q.y = (m02 - m20) / s; q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q.w = (m21 - m12) / s; q.x = 0.25f * s; q.y = (m01 + m10) / s; q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q.w = (m02 - m20) / s; q.x = (m01 + m10) / s; q.y = 0.25f * s; q.z = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q.w = (m10 - m01) / s; q.x = (m02 + m20) / s; q.y = (m12 + m21) / s; q.z = 0.25f * s;
    }
    return qnorm(q);
}

// Детерминированный генератор случайных чисел (PCG32)
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL, inc = 0xda3e39cb94b95bdbULL;
    explicit Rng(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed) { state = 0; inc = (seed << 1u) | 1u; next(); state += 0x853c49e6748fea9bULL + seed; next(); }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xs >> rot) | (xs << ((-rot) & 31));
    }
    float f01() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * f01(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); }
    bool chance(float p) { return f01() < p; }
};

inline uint32_t hash2i(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline float hash2f(int x, int y) { return (hash2i(x, y) & 0xffffff) / 16777216.0f; }

}  // namespace cl
