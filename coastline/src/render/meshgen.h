// Построение процедурных мешей для raylib
#pragma once
#include <cstdint>
#include <vector>

#include "../core/mathx.h"
#include "raylib.h"

namespace cl {

struct Col8 {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};
// цвет в формате 0xAABBGGRR (как в мире)
inline Col8 col8(uint32_t c, uint8_t a = 255) { return {(uint8_t)(c & 255), (uint8_t)((c >> 8) & 255), (uint8_t)((c >> 16) & 255), a}; }
inline Col8 rgbf(float r, float g, float b, uint8_t a = 255) {
    return {(uint8_t)(clamp01(r) * 255), (uint8_t)(clamp01(g) * 255), (uint8_t)(clamp01(b) * 255), a};
}
inline Col8 shade(Col8 c, float k) { return rgbf(c.r / 255.0f * k, c.g / 255.0f * k, c.b / 255.0f * k, c.a); }

// Преобразование для вставки примитивов: позиция, поворот по Y, масштаб
struct Xf {
    V3 o{0, 0, 0};
    float yaw = 0;
    float s = 1;
    V3 apply(V3 p) const {
        float c = std::cos(yaw), sn = std::sin(yaw);
        p = p * s;
        return {o.x + p.x * c + p.z * sn, o.y + p.y, o.z - p.x * sn + p.z * c};
    }
    V3 dir(V3 n) const {
        float c = std::cos(yaw), sn = std::sin(yaw);
        return {n.x * c + n.z * sn, n.y, -n.x * sn + n.z * c};
    }
};

class MeshBuilder {
public:
    std::vector<float> pos, nrm, uv;
    std::vector<uint8_t> col;
    std::vector<uint16_t> idx;
    int count() const { return (int)(pos.size() / 3); }
    bool empty() const { return idx.empty(); }
    int vert(V3 p, V3 n, float u, float v, Col8 c);
    void tri(int a, int b, int c) { idx.push_back(a); idx.push_back(b); idx.push_back(c); }
    // плоский четырёхугольник a-b-c-d (против часовой стрелки при взгляде снаружи)
    void quad(V3 a, V3 b, V3 c, V3 d, Col8 col, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
    void triangle(V3 a, V3 b, V3 c, Col8 col);
    void box(const Xf& x, V3 mn, V3 mx, Col8 col, bool bottom = false, float uvm = 0);
    void cylinder(const Xf& x, V3 base, float r0, float r1, float h, int seg, Col8 col, bool capTop = true, bool smooth = true, float u = 0);
    void cylinderAxis(V3 a, V3 b, float r0, float r1, int seg, Col8 col, bool caps = true);
    void icosphere(const Xf& x, V3 c, V3 rad, int subdiv, Col8 col, uint32_t jitterSeed = 0, float jitter = 0);
    void append(const MeshBuilder& o, const Xf& x);
    Mesh upload() const;
    void clear() { pos.clear(); nrm.clear(); uv.clear(); col.clear(); idx.clear(); }
};

// Набор мешей: автоматически начинает новый, когда кончаются 16-битные индексы
class MeshSet {
public:
    std::vector<MeshBuilder> parts;
    MeshBuilder& get(int needVerts);
    std::vector<Mesh> upload() const;
};

BoundingBox meshBounds(const Mesh& m);

}  // namespace cl
