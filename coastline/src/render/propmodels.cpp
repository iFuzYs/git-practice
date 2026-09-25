#include "propmodels.h"

#include <vector>

namespace cl {

namespace {

Col8 C(int r, int g, int b, uint8_t a = A_LIT) { return {(uint8_t)r, (uint8_t)g, (uint8_t)b, a}; }
Col8 withA(Col8 c, uint8_t a) { c.a = a; return c; }
Col8 mixc(Col8 a, Col8 b, float t) {
    return {(uint8_t)mixf(a.r, b.r, t), (uint8_t)mixf(a.g, b.g, t), (uint8_t)mixf(a.b, b.b, t), a.a};
}

// Затенение диапазона вершин: низ темнее (имитация AO), по нормали — чуть светлее верх
void aoRange(MeshBuilder& mb, int start, float y0, float y1, float k0, float k1) {
    for (int i = start; i < mb.count(); i++) {
        float y = mb.pos[i * 3 + 1];
        float t = clamp01((y - y0) / std::max(y1 - y0, 0.01f));
        float ny = mb.nrm[i * 3 + 1];
        float k = mixf(k0, k1, t) * (0.9f + 0.14f * ny);
        for (int c = 0; c < 3; c++) mb.col[i * 4 + c] = (uint8_t)clampf(mb.col[i * 4 + c] * k, 0, 255);
    }
}

// Конус с неровным краем: гладкие нормали по кольцу, отдельная вершина острия на грань
void cone(MeshBuilder& mb, V3 base, float r, float h, int seg, Col8 cBase, Col8 cTip, uint32_t seed, float jit, bool under, float droop = 0) {
    std::vector<V3> ring(seg + 1);
    std::vector<V3> rn(seg + 1);
    float a0 = hash2f((int)seed, 7) * TAU;
    for (int i = 0; i <= seg; i++) {
        int ii = i % seg;
        float a = a0 + ii * TAU / seg;
        float rr = r * (1.0f + (hash2f((int)seed * 3 + ii, 11) - 0.5f) * 2.0f * jit);
        ring[i] = base + V3{std::cos(a) * rr, -droop * rr, std::sin(a) * rr};
        rn[i] = norm(V3{std::cos(a) * h, r * 0.9f, std::sin(a) * h});
    }
    V3 apex = base + V3{0, h, 0};
    for (int i = 0; i < seg; i++) {
        int a = mb.vert(ring[i], rn[i], 0, 0, cBase);
        V3 nt = norm(rn[i] + rn[i + 1] + V3{0, 0.6f, 0});
        int b = mb.vert(apex, nt, 0, 1, cTip);
        int c = mb.vert(ring[i + 1], rn[i + 1], 0, 0, cBase);
        mb.tri(a, b, c);
    }
    if (under) {
        V3 cc = base + V3{0, -droop * r * 0.4f, 0};
        Col8 cu = shade(cBase, 0.7f);
        int ci = mb.vert(cc, V3{0, -1, 0}, 0, 0, cu);
        int st = mb.count();
        for (int i = 0; i <= seg; i++) mb.vert(ring[i], V3{0, -1, 0}, 0, 0, cu);
        for (int i = 0; i < seg; i++) mb.tri(ci, st + i, st + i + 1);
    }
}

// Простая «капля» для дальних планов: верх, кольцо, низ с гладкими нормалями
void blob(MeshBuilder& mb, V3 c, V3 rad, int seg, Col8 top, Col8 bot) {
    int t = mb.vert(c + V3{0, rad.y, 0}, V3{0, 1, 0}, 0, 0, top);
    int b = mb.vert(c - V3{0, rad.y, 0}, V3{0, -1, 0}, 0, 0, bot);
    int st = mb.count();
    for (int i = 0; i < seg; i++) {
        float a = i * TAU / seg;
        V3 d{std::cos(a), 0, std::sin(a)};
        mb.vert(c + V3{d.x * rad.x, rad.y * 0.1f, d.z * rad.z}, norm(d + V3{0, 0.25f, 0}), 0, 0, mixc(top, bot, 0.4f));
    }
    for (int i = 0; i < seg; i++) {
        int i0 = st + i, i1 = st + (i + 1) % seg;
        mb.tri(t, i1, i0);
        mb.tri(b, i0, i1);
    }
}

// Двусторонний четырёхугольник (листья, флаги)
void quad2(MeshBuilder& mb, V3 a, V3 b, V3 c, V3 d, Col8 col, Col8 colBack) {
    mb.quad(a, b, c, d, col);
    mb.quad(d, c, b, a, colBack);
}

// Стена от A к B (в плоскости XZ, локально), наружная нормаль (-dz, 0, dx)
void wall(MeshBuilder& mb, const Xf& x, float ax, float az, float bx, float bz, float y0, float y1, Col8 c, float tileW, float tileH, float u0 = 0) {
    V3 a0 = x.apply({ax, y0, az}), b0 = x.apply({bx, y0, bz}), b1 = x.apply({bx, y1, bz}), a1 = x.apply({ax, y1, az});
    float L = std::sqrt(sq(bx - ax) + sq(bz - az));
    if (tileW <= 0) {
        mb.quad(a0, b0, b1, a1, c);
        return;
    }
    // окна по высоте считаем от нуля (уровня земли), текстура перевёрнута по v
    mb.quad(a0, b0, b1, a1, c, u0, -y0 / tileH, u0 + L / tileW, -y1 / tileH);
}

// Коробка со стенами-фасадами (без дна), крыша отдельным цветом
void facadeBox(MeshBuilder& mb, const Xf& x, float cx, float cz, float hx, float hz, float y0, float y1, Col8 c, float tw, float th, Col8 roof, bool top = true) {
    float X0 = cx - hx, X1 = cx + hx, Z0 = cz - hz, Z1 = cz + hz;
    wall(mb, x, X1, Z0, X0, Z0, y0, y1, c, tw, th);
    wall(mb, x, X0, Z0, X0, Z1, y0, y1, c, tw, th);
    wall(mb, x, X0, Z1, X1, Z1, y0, y1, c, tw, th);
    wall(mb, x, X1, Z1, X1, Z0, y0, y1, c, tw, th);
    if (top) mb.quad(x.apply({X0, y1, Z1}), x.apply({X1, y1, Z1}), x.apply({X1, y1, Z0}), x.apply({X0, y1, Z0}), roof);
}

void lbox(MeshBuilder& mb, const Xf& x, V3 mn, V3 mx, Col8 c, bool bottom = false) { mb.box(x, mn, mx, c, bottom); }

// ------------------------------------------------------------------ деревья
void pine(MeshBuilder& mb, int var, int lod) {
    Col8 trunk = C(92, 66, 48);
    Col8 g0 = var == 0 ? C(34, 72, 40, A_FOLIAGE) : C(30, 66, 58, A_FOLIAGE);
    Col8 g1 = var == 0 ? C(58, 104, 52, A_FOLIAGE) : C(52, 98, 80, A_FOLIAGE);
    if (lod > 0) {
        cone(mb, {0, 1.0f, 0}, 2.5f, 8.2f, 6, shade(g0, 0.85f), g1, 3 + var, 0.0f, false);
        return;
    }
    Xf id;
    mb.cylinder(id, {0, -0.3f, 0}, 0.3f, 0.08f, 8.8f, 6, trunk, false, true);
    int layers = var == 0 ? 5 : 6;
    for (int k = 0; k < layers; k++) {
        float t = (float)k / (layers - 1);
        float y = 1.5f + t * (var == 0 ? 5.6f : 6.2f);
        float r = mixf(var == 0 ? 2.5f : 2.1f, 0.75f, t);
        float h = mixf(2.5f, 2.0f, t);
        cone(mb, {0, y, 0}, r, h, 8, shade(g0, 0.8f + t * 0.2f), mixc(g0, g1, 0.5f + t * 0.5f), (uint32_t)(k * 17 + var * 5 + 1), 0.16f, true, 0.18f);
    }
    cone(mb, {0, 7.7f + var * 0.6f, 0}, 0.55f, 1.6f, 6, g0, g1, 99 + var, 0.1f, true, 0.1f);
}

void deciduous(MeshBuilder& mb, bool birch, int var, int lod) {
    Col8 trunk = birch ? C(226, 222, 212) : C(94, 72, 56);
    Col8 leaf = birch ? (var == 0 ? C(118, 164, 64, A_FOLIAGE) : C(176, 170, 62, A_FOLIAGE)) : (var == 0 ? C(62, 110, 44, A_FOLIAGE) : C(84, 120, 46, A_FOLIAGE));
    if (lod > 0) {
        if (birch) blob(mb, {0, 5.6f, 0}, {1.9f, 2.8f, 1.9f}, 6, leaf, shade(leaf, 0.6f));
        else blob(mb, {0, 5.0f, 0}, {3.4f, 2.6f, 3.4f}, 7, leaf, shade(leaf, 0.6f));
        return;
    }
    Xf id;
    if (birch) {
        mb.cylinder(id, {0, -0.3f, 0}, 0.2f, 0.07f, 8.6f, 6, trunk, false, true);
        for (int i = 0; i < 4; i++) {
            float y = 1.0f + i * 1.5f + hash2f(i, 3) * 0.6f;
            float r = mixf(0.2f, 0.07f, (y + 0.3f) / 8.6f) + 0.012f;
            mb.cylinder(id, {0, y, 0}, r, r, 0.14f, 6, C(40, 38, 36), false, true);
        }
        int st = mb.count();
        mb.icosphere(id, {0.2f, 5.2f, 0}, {1.6f, 2.2f, 1.6f}, 1, leaf, 11 + var, 0.18f);
        mb.icosphere(id, {-0.5f, 6.8f, 0.3f}, {1.3f, 1.8f, 1.3f}, 1, leaf, 23 + var, 0.2f);
        mb.icosphere(id, {0.4f, 7.6f, -0.3f}, {0.9f, 1.2f, 0.9f}, 1, leaf, 31 + var, 0.2f);
        aoRange(mb, st, 3.2f, 8.8f, 0.68f, 1.12f);
    } else {
        mb.cylinder(id, {0, -0.3f, 0}, 0.46f, 0.3f, 4.4f, 7, trunk, false, true);
        mb.cylinderAxis({0, 3.4f, 0}, {1.5f, 5.2f, 0.4f}, 0.2f, 0.1f, 5, trunk, false);
        mb.cylinderAxis({0, 3.6f, 0}, {-1.2f, 5.5f, -0.8f}, 0.2f, 0.1f, 5, trunk, false);
        int st = mb.count();
        mb.icosphere(id, {0, 5.4f, 0}, {3.1f, 2.4f, 3.1f}, 1, leaf, 41 + var, 0.16f);
        mb.icosphere(id, {1.6f, 5.0f, 0.8f}, {2.0f, 1.7f, 2.0f}, 1, leaf, 53 + var, 0.2f);
        mb.icosphere(id, {-1.4f, 5.6f, -1.1f}, {2.1f, 1.8f, 2.1f}, 1, leaf, 67 + var, 0.2f);
        mb.icosphere(id, {0.3f, 7.0f, -0.2f}, {1.8f, 1.3f, 1.8f}, 1, leaf, 71 + var, 0.2f);
        aoRange(mb, st, 3.0f, 8.2f, 0.62f, 1.14f);
    }
}

void palm(MeshBuilder& mb, int var, int lod) {
    Col8 trunk = C(150, 120, 86), trunk2 = C(118, 92, 66);
    Col8 leaf = var == 0 ? C(66, 128, 46, A_FOLIAGE) : C(88, 136, 50, A_FOLIAGE);
    // изогнутый ствол
    auto trunkAt = [&](float t) {
        float lean = var == 0 ? 1.6f : -1.2f;
        return V3{lean * t * t, t * 8.6f, 0.3f * t * t};
    };
    int segs = lod > 0 ? 2 : 7;
    for (int i = 0; i < segs; i++) {
        float t0 = (float)i / segs, t1 = (float)(i + 1) / segs;
        mb.cylinderAxis(trunkAt(t0), trunkAt(t1), mixf(0.3f, 0.2f, t0), mixf(0.3f, 0.2f, t1) + (lod > 0 ? 0 : 0.03f), lod > 0 ? 4 : 7,
                        (i % 2) ? trunk : trunk2, false);
    }
    V3 top = trunkAt(1.0f);
    int fronds = lod > 0 ? 4 : 9;
    for (int f = 0; f < fronds; f++) {
        float a = f * TAU / fronds + hash2f(f, var) * 0.4f;
        V3 d{std::cos(a), 0, std::sin(a)};
        V3 side{-d.z, 0, d.x};
        float L = 4.3f + hash2f(f, 5 + var) * 0.8f;
        int n = lod > 0 ? 2 : 6;
        V3 prevC = top, prevL = top, prevR = top;
        for (int k = 1; k <= n; k++) {
            float t = (float)k / n;
            V3 c = top + d * (t * L) + V3{0, 0.9f * t - 2.6f * t * t, 0};
            float w = 0.62f * std::pow(std::sin(PI * std::min(t, 0.96f)), 0.7f);
            V3 l = c + side * w - V3{0, 0.18f * w, 0};
            V3 r = c - side * w - V3{0, 0.18f * w, 0};
            Col8 cc = shade(leaf, 0.8f + t * 0.3f);
            // две половинки листа «домиком»
            quad2(mb, prevC, prevL, l, c, cc, shade(cc, 0.8f));
            quad2(mb, prevR, prevC, c, r, cc, shade(cc, 0.8f));
            prevC = c; prevL = l; prevR = r;
        }
    }
    if (lod == 0) {
        Xf id;
        for (int i = 0; i < 3; i++) {
            float a = i * TAU / 3;
            mb.icosphere(id, top + V3{std::cos(a) * 0.3f, -0.35f, std::sin(a) * 0.3f}, {0.2f, 0.22f, 0.2f}, 0, C(96, 70, 40), 0, 0);
        }
    }
}

void bush(MeshBuilder& mb, int var, int lod) {
    Col8 leaf = var == 0 ? C(54, 96, 40, A_FOLIAGE) : C(82, 112, 44, A_FOLIAGE);
    if (lod > 0) {
        blob(mb, {0, 0.7f, 0}, {1.1f, 0.75f, 1.1f}, 6, leaf, shade(leaf, 0.6f));
        return;
    }
    Xf id;
    int st = mb.count();
    mb.icosphere(id, {0, 0.6f, 0}, {1.0f, 0.75f, 1.0f}, 1, leaf, 5 + var, 0.2f);
    mb.icosphere(id, {0.6f, 0.5f, 0.4f}, {0.7f, 0.6f, 0.7f}, 1, leaf, 9 + var, 0.2f);
    mb.icosphere(id, {-0.5f, 0.45f, -0.4f}, {0.7f, 0.55f, 0.7f}, 1, leaf, 13 + var, 0.2f);
    aoRange(mb, st, 0.0f, 1.4f, 0.6f, 1.1f);
}

void rock(MeshBuilder& mb, int var, int lod) {
    Col8 c = var == 0 ? C(128, 124, 116) : var == 1 ? C(112, 106, 98) : C(140, 132, 118);
    Xf id;
    int st = mb.count();
    if (lod > 0) {
        blob(mb, {0, 0.3f, 0}, {1.45f, 1.1f, 1.3f}, 6, c, shade(c, 0.7f));
        return;
    }
    mb.icosphere(id, {0, 0.3f, 0}, {1.45f, 1.05f, 1.3f}, 1, c, 101 + var * 7, 0.24f);
    if (var == 2) mb.icosphere(id, {1.1f, 0.0f, 0.6f}, {0.7f, 0.55f, 0.65f}, 1, c, 131, 0.25f);
    // мох на верхних гранях
    for (int i = st; i < mb.count(); i++) {
        float ny = mb.nrm[i * 3 + 1];
        if (ny > 0.72f && hash2f(i / 3, 17) > 0.35f) {
            mb.col[i * 4] = 88; mb.col[i * 4 + 1] = 110; mb.col[i * 4 + 2] = 60;
        }
    }
    aoRange(mb, st, -0.6f, 1.4f, 0.65f, 1.08f);
}

// ------------------------------------------------------------------ дорожные объекты
void fence(MeshBuilder& mb) {
    Col8 w = C(152, 118, 82), w2 = C(132, 100, 70);
    Xf id;
    for (float x : {-1.55f, 0.0f, 1.55f}) lbox(mb, id, {x - 0.07f, -0.3f, -0.07f}, {x + 0.07f, 1.25f, 0.07f}, w2);
    lbox(mb, id, {-1.65f, 0.48f, -0.04f}, {1.65f, 0.62f, 0.04f}, w, true);
    lbox(mb, id, {-1.65f, 0.95f, -0.04f}, {1.65f, 1.09f, 0.04f}, w, true);
}

void board(MeshBuilder& mb) {
    Xf id;
    Col8 post = C(60, 60, 66);
    lbox(mb, id, {-1.45f, -0.3f, -0.08f}, {-1.25f, 1.3f, 0.08f}, post);
    lbox(mb, id, {1.25f, -0.3f, -0.08f}, {1.45f, 1.3f, 0.08f}, post);
    lbox(mb, id, {-1.75f, 1.15f, -0.14f}, {1.75f, 3.15f, 0.14f}, C(30, 30, 34), true);
    // светящиеся панели с градиентом с обеих сторон
    Col8 top = C(255, 150, 40, A_EMIT), bot = C(236, 40, 128, A_EMIT);
    for (int side = 0; side < 2; side++) {
        float z = side ? -0.15f : 0.15f;
        V3 n{0, 0, side ? -1.0f : 1.0f};
        float x0 = side ? 1.6f : -1.6f, x1 = side ? -1.6f : 1.6f;
        int a = mb.vert({x0, 1.3f, z}, n, 0, 0, bot), b = mb.vert({x1, 1.3f, z}, n, 0, 0, bot);
        int c = mb.vert({x1, 3.0f, z}, n, 0, 0, top), d = mb.vert({x0, 3.0f, z}, n, 0, 0, top);
        mb.tri(a, b, c);
        mb.tri(a, c, d);
        // белая «звезда» опыта
        float zc = side ? -0.16f : 0.16f;
        V3 cc{0, 2.15f, zc};
        Col8 wc = C(255, 250, 240, A_EMIT);
        for (int k = 0; k < 5; k++) {
            float a0 = PI * 0.5f + k * TAU / 5, a1 = a0 + TAU / 10, a2 = a0 - TAU / 10;
            V3 p0 = cc + V3{std::cos(a0) * 0.62f, std::sin(a0) * 0.62f, 0};
            V3 p1 = cc + V3{std::cos(a1) * 0.25f, std::sin(a1) * 0.25f, 0};
            V3 p2 = cc + V3{std::cos(a2) * 0.25f, std::sin(a2) * 0.25f, 0};
            if (side) { p0.x = -p0.x; p1.x = -p1.x; p2.x = -p2.x; }
            int i0 = mb.vert(cc, n, 0, 0, wc), i1 = mb.vert(p2, n, 0, 0, wc), i2 = mb.vert(p0, n, 0, 0, wc), i3 = mb.vert(p1, n, 0, 0, wc);
            mb.tri(i0, i1, i2);
            mb.tri(i0, i2, i3);
        }
    }
}

void trafficCone(MeshBuilder& mb) {
    Xf id;
    lbox(mb, id, {-0.25f, 0, -0.25f}, {0.25f, 0.04f, 0.25f}, C(40, 40, 40));
    mb.cylinder(id, {0, 0.04f, 0}, 0.2f, 0.035f, 0.7f, 10, C(240, 92, 24), true, true);
    mb.cylinder(id, {0, 0.34f, 0}, 0.122f, 0.103f, 0.1f, 10, C(245, 245, 245), false, true);
    mb.cylinder(id, {0, 0.5f, 0}, 0.086f, 0.067f, 0.1f, 10, C(245, 245, 245), false, true);
}

void hay(MeshBuilder& mb) {
    Xf id;
    mb.cylinder(id, {0, -0.1f, 0}, 0.8f, 0.8f, 1.45f, 12, C(214, 178, 92), true, true);
    mb.cylinder(id, {0, 0.45f, 0}, 0.812f, 0.812f, 0.08f, 12, C(236, 236, 230), false, true);
    mb.cylinder(id, {0, 0.95f, 0}, 0.812f, 0.812f, 0.08f, 12, C(236, 236, 230), false, true);
}

void lamp(MeshBuilder& mb) {
    Xf id;
    Col8 m = C(74, 78, 84);
    mb.cylinder(id, {0, -0.3f, 0}, 0.13f, 0.08f, 7.8f, 8, m, true, true);
    lbox(mb, id, {-0.05f, 7.3f, 0}, {0.05f, 7.42f, 1.7f}, m, true);
    lbox(mb, id, {-0.26f, 7.12f, 1.4f}, {0.26f, 7.36f, 2.15f}, m, true);
    lbox(mb, id, {-0.2f, 7.08f, 1.48f}, {0.2f, 7.13f, 2.06f}, C(255, 226, 170, A_NIGHT), true);
}

void turbineTower(MeshBuilder& mb) {
    Xf id;
    Col8 w = C(236, 238, 240);
    mb.cylinder(id, {0, -1, 0}, 2.1f, 1.15f, 73.0f, 12, w, false, true);
    lbox(mb, id, {-1.4f, 71.2f, -2.6f}, {1.4f, 74.2f, 5.0f}, w, true);
    lbox(mb, id, {-0.25f, 74.2f, -2.2f}, {0.25f, 74.5f, -1.7f}, C(255, 40, 30, A_NIGHT));
}

void pole(MeshBuilder& mb) {
    Xf id;
    Col8 w = C(108, 84, 60);
    mb.cylinder(id, {0, -0.3f, 0}, 0.17f, 0.12f, 9.3f, 7, w, true, true);
    lbox(mb, id, {-1.2f, 8.2f, -0.07f}, {1.2f, 8.36f, 0.07f}, w, true);
    for (float x : {-1.05f, 0.0f, 1.05f}) mb.cylinder(id, {x, 8.36f, 0}, 0.05f, 0.04f, 0.2f, 5, C(90, 110, 90), true, true);
}

void barrier(MeshBuilder& mb) {
    // бетонный блок «нью-джерси» вдоль локальной оси z
    Col8 c = C(206, 204, 196);
    float hz = 2.02f;
    float px[6] = {0.3f, 0.26f, 0.12f, -0.12f, -0.26f, -0.3f};
    float py[6] = {-0.2f, 0.22f, 0.95f, 0.95f, 0.22f, -0.2f};
    for (int i = 0; i < 5; i++) {
        V3 a{px[i], py[i], -hz}, b{px[i + 1], py[i + 1], -hz};
        mb.quad(V3{a.x, a.y, hz}, V3{a.x, a.y, -hz}, V3{b.x, b.y, -hz}, V3{b.x, b.y, hz}, i == 2 ? shade(c, 1.05f) : c);
    }
    for (int e = 0; e < 2; e++) {
        float z = e ? hz : -hz;
        V3 n{0, 0, e ? 1.0f : -1.0f};
        int st = mb.count();
        for (int i = 0; i < 6; i++) mb.vert({px[i], py[i], z}, n, 0, 0, shade(c, 0.9f));
        for (int i = 1; i < 5; i++) {
            if (e) mb.tri(st, st + i + 1, st + i);
            else mb.tri(st, st + i, st + i + 1);
        }
    }
    // красно-белые полосы
    for (int k = 0; k < 4; k++) {
        float z0 = -hz + 0.2f + k * 1.0f, z1 = z0 + 0.5f;
        for (float sd : {1.0f, -1.0f}) {
            float x = sd * 0.23f;
            V3 a{x + sd * 0.012f, 0.35f, z0}, b{x + sd * 0.012f, 0.35f, z1}, c2{x + sd * 0.012f, 0.6f, z1}, d{x + sd * 0.012f, 0.6f, z0};
            if (sd > 0) mb.quad(a, b, c2, d, C(210, 40, 36, A_NIGHT));
            else mb.quad(b, a, d, c2, C(210, 40, 36, A_NIGHT));
        }
    }
}

void flag(MeshBuilder& mb, int var) {
    Xf id;
    Col8 cols[5] = {C(255, 96, 40, A_FOLIAGE), C(255, 206, 44, A_FOLIAGE), C(44, 190, 255, A_FOLIAGE), C(250, 250, 250, A_FOLIAGE), C(150, 86, 255, A_FOLIAGE)};
    mb.cylinder(id, {0, -0.3f, 0}, 0.07f, 0.05f, 9.3f, 6, C(220, 222, 226), true, true);
    // «парус»: изогнутое полотно
    Col8 c = cols[var % 5];
    int n = 6;
    for (int k = 0; k < n; k++) {
        float t0 = (float)k / n, t1 = (float)(k + 1) / n;
        auto P = [&](float t, float y) {
            float w = 1.25f * std::sin(t * PI * 0.5f + 0.2f);
            return V3{0.08f + t * 1.2f, y - t * t * 0.9f, std::sin(t * 2.4f) * 0.18f + w * 0.0f};
        };
        V3 a = P(t0, 4.4f), b = P(t1, 4.4f + t1 * 0.6f), c2 = P(t1, 8.9f), d = P(t0, 8.9f);
        b.y = 4.4f + t1 * 1.1f;
        a.y = 4.4f + t0 * 1.1f;
        Col8 cc = (k == 1) ? C(255, 255, 255, A_FOLIAGE) : c;
        quad2(mb, a, b, c2, d, cc, shade(cc, 0.85f));
    }
}

}  // namespace

int propVariants(PropKind k) {
    switch (k) {
        case PK_PINE: case PK_OAK: case PK_BIRCH: case PK_PALM: case PK_BUSH: return 2;
        case PK_ROCK: return 3;
        case PK_FLAG: return 5;
        default: return 1;
    }
}

void propGeometry(MeshBuilder& mb, PropKind k, int var, int lod) {
    switch (k) {
        case PK_PINE: pine(mb, var, lod); break;
        case PK_OAK: deciduous(mb, false, var, lod); break;
        case PK_BIRCH: deciduous(mb, true, var, lod); break;
        case PK_PALM: palm(mb, var, lod); break;
        case PK_BUSH: bush(mb, var, lod); break;
        case PK_ROCK: rock(mb, var, lod); break;
        case PK_FENCE: fence(mb); break;
        case PK_BOARD: board(mb); break;
        case PK_CONE: trafficCone(mb); break;
        case PK_HAY: hay(mb); break;
        case PK_LAMP: lamp(mb); break;
        case PK_TURBINE: turbineTower(mb); break;
        case PK_POLE: pole(mb); break;
        case PK_BARRIER: barrier(mb); break;
        case PK_FLAG: flag(mb, var); break;
        default: break;
    }
}

void rotorGeometry(MeshBuilder& mb) {
    Xf id;
    Col8 w = C(240, 242, 244);
    mb.icosphere(id, {0, 0, 0.2f}, {1.1f, 1.1f, 1.6f}, 1, w, 0, 0);
    for (int b = 0; b < 3; b++) {
        float a = b * TAU / 3;
        V3 d{std::cos(a), std::sin(a), 0}, s{-std::sin(a), std::cos(a), 0};
        V3 r0 = d * 0.9f, r1 = d * 32.0f;
        float w0 = 1.2f, w1 = 0.25f;
        V3 zt{0, 0, 0.18f};
        // лопасть — тонкая клиновидная пластина
        V3 a0 = r0 + s * w0, a1 = r0 - s * (w0 * 0.3f), b0 = r1 + s * w1, b1 = r1 - s * (w1 * 0.3f);
        mb.quad(a0 + zt, b0 + zt, b1 + zt, a1 + zt, w);
        mb.quad(a1 - zt, b1 - zt, b0 - zt, a0 - zt, shade(w, 0.92f));
        mb.quad(a0 - zt, b0 - zt, b0 + zt, a0 + zt, w);
        mb.quad(a1 + zt, b1 + zt, b1 - zt, a1 - zt, w);
    }
}

// ------------------------------------------------------------------ здания
namespace {

void gableRoof(MeshBuilder& mb, const Xf& x, float hx, float hz, float y, float rh, float over, Col8 roof, Col8 wallC) {
    bool alongX = hx >= hz;
    // локальная система: ось конька — «a», поперёк — «b»
    float ha = alongX ? hx : hz, hb = alongX ? hz : hx;
    auto P = [&](float a, float yy, float b) { return alongX ? x.apply({a, yy, b}) : x.apply({b, yy, a}); };
    float A = ha + over, B = hb + over;
    float drop = over * rh / hb;
    V3 r0 = P(-A, y + rh, 0), r1 = P(A, y + rh, 0);
    V3 e0 = P(-A, y - drop, B), e1 = P(A, y - drop, B), f0 = P(-A, y - drop, -B), f1 = P(A, y - drop, -B);
    Col8 under = shade(roof, 0.55f);
    if (alongX) {
        mb.quad(e0, e1, r1, r0, roof);
        mb.quad(f1, f0, r0, r1, shade(roof, 0.95f));
        mb.quad(r0, r1, e1, e0, under);
        mb.quad(r1, r0, f0, f1, under);
    } else {
        mb.quad(e1, e0, r0, r1, roof);
        mb.quad(f0, f1, r1, r0, shade(roof, 0.95f));
        mb.quad(r1, r0, e0, e1, under);
        mb.quad(r0, r1, f1, f0, under);
    }
    // фронтоны
    for (int e = 0; e < 2; e++) {
        float a = e ? ha : -ha;
        V3 p0 = P(a, y, -hb), p1 = P(a, y, hb), p2 = P(a, y + rh, 0);
        bool flip = (e == 1) != alongX;
        if (flip) mb.triangle(p0, p2, p1, wallC);
        else mb.triangle(p0, p1, p2, wallC);
    }
}

// Вертикальный градиент на экране/баннере
void gradQuad(MeshBuilder& mb, V3 a, V3 b, V3 c, V3 d, Col8 bot, Col8 top) {
    V3 n = norm(cross(b - a, c - a));
    int i0 = mb.vert(a, n, 0, 0, bot), i1 = mb.vert(b, n, 0, 0, bot), i2 = mb.vert(c, n, 0, 0, top), i3 = mb.vert(d, n, 0, 0, top);
    mb.tri(i0, i1, i2);
    mb.tri(i0, i2, i3);
}

void people(MeshBuilder& mb, const Xf& x, V3 a, V3 b, int n, uint32_t seed) {
    Col8 shirts[8] = {C(230, 60, 50), C(40, 120, 220), C(250, 250, 250), C(250, 200, 40), C(30, 30, 36), C(60, 190, 110), C(250, 120, 170), C(250, 140, 40)};
    Col8 skins[4] = {C(236, 196, 160), C(200, 150, 110), C(150, 100, 70), C(100, 70, 50)};
    for (int i = 0; i < n; i++) {
        float t = (i + 0.2f + hash2f(i, (int)seed) * 0.6f) / n;
        V3 p = a + (b - a) * t;
        Col8 sh = shirts[(int)(hash2f(i, (int)seed + 5) * 8) % 8];
        Col8 sk = skins[(int)(hash2f(i, (int)seed + 9) * 4) % 4];
        float hgt = 0.75f + hash2f(i, (int)seed + 13) * 0.25f;
        lbox(mb, x, p + V3{-0.2f, 0, -0.14f}, p + V3{0.2f, hgt, 0.14f}, sh);
        lbox(mb, x, p + V3{-0.11f, hgt, -0.11f}, p + V3{0.11f, hgt + 0.24f, 0.11f}, sk);
    }
}

}  // namespace

void buildingGeometry(MeshBuilder& mb, const Building& b) {
    Xf x{b.c, b.yaw, 1};
    Col8 wc = col8(b.color);
    float hx = b.hx, hz = b.hz, h = b.h;
    const float base = -2.5f;  // фундамент уходит в склон
    switch (b.style) {
        case BS_HOUSE: {
            Col8 roofs[4] = {C(176, 82, 58), C(92, 62, 46), C(70, 92, 112), C(150, 52, 46)};
            facadeBox(mb, x, 0, 0, hx, hz, base, h, withA(wc, A_FACADE_HOUSE), 4.0f, 3.5f, wc, false);
            float rh = std::min(std::min(hx, hz) * 0.75f, 3.4f);
            gableRoof(mb, x, hx, hz, h, rh, 0.45f, roofs[b.var % 4], wc);
            // дверь и труба
            lbox(mb, x, {-0.55f, 0, hz}, {0.55f, 2.15f, hz + 0.08f}, C(96, 64, 44));
            lbox(mb, x, {hx * 0.4f, h, -0.5f}, {hx * 0.4f + 0.8f, h + rh + 0.6f, 0.3f}, C(150, 90, 70));
            break;
        }
        case BS_SHOP: {
            facadeBox(mb, x, 0, 0, hx, hz, base, h, withA(wc, A_FACADE_SHOP), 4.0f, 3.5f, C(96, 96, 100));
            // парапет
            float p = 0.25f;
            lbox(mb, x, {-hx, h, -hz}, {hx, h + 0.7f, -hz + p}, wc);
            lbox(mb, x, {-hx, h, hz - p}, {hx, h + 0.7f, hz}, wc);
            lbox(mb, x, {-hx, h, -hz + p}, {-hx + p, h + 0.7f, hz - p}, wc);
            lbox(mb, x, {hx - p, h, -hz + p}, {hx, h + 0.7f, hz - p}, wc);
            lbox(mb, x, {-1.5f, h, -1.0f}, {0.8f, h + 1.3f, 0.6f}, C(170, 172, 176));
            // полосатые маркизы на длинных сторонах
            Col8 aw[4] = {C(210, 60, 50), C(40, 120, 190), C(60, 150, 90), C(230, 150, 40)};
            Col8 ac = aw[b.var % 4], white = C(245, 245, 240);
            bool alongX = hx >= hz;
            float L = alongX ? hx : hz, D = alongX ? hz : hx;
            for (int s = 0; s < 2; s++) {
                float sd = s ? -1.0f : 1.0f;
                int n = std::max(2, (int)(L * 2 / 1.2f));
                for (int k = 0; k < n; k++) {
                    float a0 = -L + 2 * L * k / n, a1 = -L + 2 * L * (k + 1) / n;
                    auto P = [&](float a, float y, float d) { return alongX ? x.apply({a, y, d * sd}) : x.apply({d * sd, y, a}); };
                    V3 q0 = P(a0, 3.3f, D), q1 = P(a1, 3.3f, D), q2 = P(a1, 2.6f, D + 1.6f), q3 = P(a0, 2.6f, D + 1.6f);
                    Col8 cc = (k % 2) ? white : ac;
                    bool flip = (sd > 0) == alongX;
                    if (flip) { mb.quad(q1, q0, q3, q2, cc); mb.quad(q0, q1, q2, q3, shade(cc, 0.6f)); }
                    else { mb.quad(q0, q1, q2, q3, cc); mb.quad(q1, q0, q3, q2, shade(cc, 0.6f)); }
                }
            }
            // светящаяся вывеска
            Col8 sign[4] = {C(255, 90, 120, A_NIGHT), C(80, 220, 255, A_NIGHT), C(255, 220, 80, A_NIGHT), C(150, 255, 120, A_NIGHT)};
            if (alongX) lbox(mb, x, {-hx * 0.5f, 3.6f, hz}, {hx * 0.5f, 4.4f, hz + 0.15f}, sign[(b.var + 1) % 4]);
            else lbox(mb, x, {hx, 3.6f, -hz * 0.5f}, {hx + 0.15f, 4.4f, hz * 0.5f}, sign[(b.var + 1) % 4]);
            break;
        }
        case BS_TOWER: {
            bool glass = b.var >= 2;
            uint8_t st = glass ? A_FACADE_GLASS : A_FACADE_OFFICE;
            float tw = glass ? 4.0f : 3.5f, th = glass ? 4.0f : 3.5f;
            Col8 roofc = C(110, 112, 118);
            if (h > 30) {
                float h1 = std::floor(h * 0.68f / th) * th;
                facadeBox(mb, x, 0, 0, hx, hz, base, h1, withA(wc, st), tw, th, roofc);
                facadeBox(mb, x, 0, 0, hx * 0.74f, hz * 0.74f, h1, h, withA(wc, st), tw, th, roofc);
            } else {
                facadeBox(mb, x, 0, 0, hx, hz, base, h, withA(wc, st), tw, th, roofc);
            }
            float sx = h > 30 ? hx * 0.74f : hx, sz = h > 30 ? hz * 0.74f : hz;
            lbox(mb, x, {-sx, h, -sz}, {sx, h + 0.6f, -sz + 0.3f}, wc);
            lbox(mb, x, {-sx, h, sz - 0.3f}, {sx, h + 0.6f, sz}, wc);
            lbox(mb, x, {-sx * 0.4f, h, -sz * 0.4f}, {sx * 0.4f, h + 2.6f, sz * 0.4f}, C(150, 152, 158));
            mb.cylinder(x, {0, h + 2.6f, 0}, 0.12f, 0.05f, 6.0f, 5, C(200, 200, 205), false, true);
            lbox(mb, x, {-0.2f, h + 8.6f, -0.2f}, {0.2f, h + 8.9f, 0.2f}, C(255, 40, 30, A_NIGHT));
            break;
        }
        case BS_BARN: {
            float hw = h * 0.55f;
            Col8 roof = C(72, 72, 78), trim = C(240, 238, 230);
            wall(mb, x, hx, -hz, -hx, -hz, base, hw, wc, 0, 0);
            wall(mb, x, -hx, -hz, -hx, hz, base, hw, wc, 0, 0);
            wall(mb, x, -hx, hz, hx, hz, base, hw, wc, 0, 0);
            wall(mb, x, hx, hz, hx, -hz, base, hw, wc, 0, 0);
            float y1 = hw + (h - hw) * 0.68f;
            float px[5] = {-(hx + 0.35f), -hx * 0.58f, 0, hx * 0.58f, hx + 0.35f};
            float py[5] = {hw - 0.2f, y1, h, y1, hw - 0.2f};
            for (int i = 0; i < 4; i++) {
                V3 a0 = x.apply({px[i], py[i], -hz - 0.35f}), a1 = x.apply({px[i], py[i], hz + 0.35f});
                V3 b0 = x.apply({px[i + 1], py[i + 1], -hz - 0.35f}), b1 = x.apply({px[i + 1], py[i + 1], hz + 0.35f});
                mb.quad(a1, a0, b0, b1, roof);
                mb.quad(a0, a1, b1, b0, shade(roof, 0.5f));
            }
            for (int e = 0; e < 2; e++) {
                float z = e ? hz : -hz;
                V3 n{0, 0, e ? 1.0f : -1.0f};
                (void)n;
                V3 cc = x.apply({0, hw, z});
                V3 pts[5] = {x.apply({-hx, hw, z}), x.apply({-hx * 0.58f, y1 - 0.05f, z}), x.apply({0, h - 0.1f, z}), x.apply({hx * 0.58f, y1 - 0.05f, z}), x.apply({hx, hw, z})};
                for (int i = 0; i < 4; i++) {
                    if (e) mb.triangle(cc, pts[i + 1], pts[i], wc);
                    else mb.triangle(cc, pts[i], pts[i + 1], wc);
                }
                // ворота с белой рамкой и крестом
                float zz = e ? hz + 0.06f : -hz - 0.06f;
                float z0 = std::min(z, zz), z1 = std::max(z, zz);
                lbox(mb, x, {-2.6f, 0, z0}, {2.6f, 0.25f, z1}, trim);
                lbox(mb, x, {-2.6f, 4.2f, z0}, {2.6f, 4.45f, z1}, trim);
                lbox(mb, x, {-2.6f, 0, z0}, {-2.35f, 4.45f, z1}, trim);
                lbox(mb, x, {2.35f, 0, z0}, {2.6f, 4.45f, z1}, trim);
                lbox(mb, x, {-0.12f, 0, z0}, {0.12f, 4.45f, z1}, trim);
            }
            for (float sx : {-1.0f, 1.0f})
                for (float sz : {-1.0f, 1.0f}) lbox(mb, x, {sx * hx - 0.15f, 0, sz * hz - 0.15f}, {sx * hx + 0.15f, hw, sz * hz + 0.15f}, trim);
            break;
        }
        case BS_HANGAR: {
            int n = 12;
            Col8 roof = wc, end = shade(wc, 0.92f);
            for (int i = 0; i < n; i++) {
                float a0 = PI * i / n, a1 = PI * (i + 1) / n;
                V3 p0{std::cos(a0) * (hx + 0.3f), std::sin(a0) * h, 0}, p1{std::cos(a1) * (hx + 0.3f), std::sin(a1) * h, 0};
                V3 a = x.apply({p0.x, p0.y, -hz - 0.3f}), bb = x.apply({p1.x, p1.y, -hz - 0.3f});
                V3 c = x.apply({p1.x, p1.y, hz + 0.3f}), d = x.apply({p0.x, p0.y, hz + 0.3f});
                Col8 cc = (i % 2) ? roof : shade(roof, 0.94f);
                mb.quad(d, c, bb, a, cc);
                mb.quad(a, bb, c, d, shade(roof, 0.4f));
            }
            for (int e = 0; e < 2; e++) {
                float z = e ? hz : -hz;
                V3 cc = x.apply({0, base, z});
                for (int i = 0; i < n; i++) {
                    float a0 = PI * i / n, a1 = PI * (i + 1) / n;
                    V3 p0 = x.apply({std::cos(a0) * hx, std::max(std::sin(a0) * h, base), z});
                    V3 p1 = x.apply({std::cos(a1) * hx, std::max(std::sin(a1) * h, base), z});
                    if (i == 0) p0 = x.apply({hx, base, z});
                    if (i == n - 1) p1 = x.apply({-hx, base, z});
                    if (e) mb.triangle(cc, p0, p1, end);
                    else mb.triangle(cc, p1, p0, end);
                }
            }
            // ворота
            float zz = hz + 0.05f;
            lbox(mb, x, {-hx * 0.7f, 0, hz}, {hx * 0.7f, h * 0.72f, zz}, C(84, 88, 94));
            for (int k = 1; k < 6; k++) {
                float xx = -hx * 0.7f + k * hx * 1.4f / 6;
                lbox(mb, x, {xx - 0.08f, 0, zz}, {xx + 0.08f, h * 0.72f, zz + 0.04f}, C(60, 62, 66));
            }
            lbox(mb, x, {-hx * 0.7f, h * 0.72f, hz}, {hx * 0.7f, h * 0.72f + 0.8f, zz + 0.02f}, C(255, 236, 190, A_NIGHT));
            break;
        }
        case BS_TENT: {
            Col8 white = C(248, 248, 244);
            float eave = 3.1f, ex = hx + 0.2f, ez = hz + 0.2f;
            for (float sx : {-1.0f, 1.0f})
                for (float sz : {-1.0f, 1.0f}) mb.cylinder(x, {sx * (hx - 0.3f), -0.2f, sz * (hz - 0.3f)}, 0.1f, 0.1f, eave + 0.3f, 6, C(200, 200, 205), false, true);
            V3 apex = x.apply({0, h, 0});
            V3 cs[4] = {x.apply({ex, eave, -ez}), x.apply({-ex, eave, -ez}), x.apply({-ex, eave, ez}), x.apply({ex, eave, ez})};
            for (int f = 0; f < 4; f++) {
                V3 a = cs[f], bb = cs[(f + 1) % 4];
                for (int k = 0; k < 4; k++) {
                    V3 p0 = a + (bb - a) * (k / 4.0f), p1 = a + (bb - a) * ((k + 1) / 4.0f);
                    Col8 cc = (k % 2) ? white : wc;
                    mb.triangle(p1, p0, apex, cc);
                    mb.triangle(p0, p1, apex, shade(cc, 0.62f));
                }
                // бахрома
                V3 d0 = a - V3{0, 0.55f, 0}, d1 = bb - V3{0, 0.55f, 0};
                mb.quad(bb, a, d0, d1, wc);
                mb.quad(a, bb, d1, d0, shade(wc, 0.6f));
                // гирлянда: светится ночью
                V3 g0 = a - V3{0, 0.62f, 0}, g1 = bb - V3{0, 0.62f, 0};
                mb.quad(g1, g0, g0 - V3{0, 0.08f, 0}, g1 - V3{0, 0.08f, 0}, C(255, 214, 120, A_NIGHT));
            }
            lbox(mb, x, {-2.4f, 0, -1.0f}, {2.4f, 1.05f, 1.0f}, C(150, 110, 76));
            lbox(mb, x, {-2.5f, 1.05f, -1.1f}, {2.5f, 1.12f, 1.1f}, C(230, 230, 226));
            people(mb, x, {-2.0f, 0, 1.8f}, {2.0f, 0, 1.8f}, 3, b.var * 7u + 3);
            break;
        }
        case BS_STAGE: {
            Col8 dark = C(38, 38, 46), truss = C(84, 84, 92);
            lbox(mb, x, {-hx, -1.5f, -hz}, {hx, 1.4f, hz}, dark);
            lbox(mb, x, {-hx, 1.4f, -hz}, {hx, h - 1.0f, -hz + 0.6f}, C(24, 24, 30));
            // светодиодный экран
            V3 s0 = x.apply({hx * 0.72f, 3.0f, -hz + 0.62f}), s1 = x.apply({-hx * 0.72f, 3.0f, -hz + 0.62f});
            V3 s2 = x.apply({-hx * 0.72f, h - 3.0f, -hz + 0.62f}), s3 = x.apply({hx * 0.72f, h - 3.0f, -hz + 0.62f});
            gradQuad(mb, s1, s0, s3, s2, C(255, 70, 150, A_EMIT), C(255, 170, 50, A_EMIT));
            lbox(mb, x, {-hx - 1, h - 1.0f, -hz}, {hx + 1, h, hz + 1}, truss, true);
            for (float sx : {-1.0f, 1.0f})
                for (float sz : {-1.0f, 1.0f}) lbox(mb, x, {sx * hx - 0.45f, 1.4f, sz * (hz - 0.2f) - 0.45f + (sz > 0 ? 0.2f : 0)}, {sx * hx + 0.45f, h - 1.0f, sz * (hz - 0.2f) + 0.45f + (sz > 0 ? 0.2f : 0)}, truss);
            for (float sx : {-1.0f, 1.0f}) {
                lbox(mb, x, {sx * (hx - 1.6f) - 1.2f, 1.4f, hz - 3.2f}, {sx * (hx - 1.6f) + 1.2f, 6.6f, hz - 1.0f}, C(20, 20, 22));
                lbox(mb, x, {sx * (hx - 1.6f) - 0.8f, 2.0f, hz - 0.99f}, {sx * (hx - 1.6f) + 0.8f, 6.0f, hz - 0.97f}, C(60, 60, 66));
            }
            // прожекторы под фермой
            Col8 lc[3] = {C(120, 220, 255, A_EMIT), C(255, 120, 220, A_EMIT), C(255, 240, 200, A_EMIT)};
            for (int k = 0; k < 9; k++) {
                float xx = -hx + 2 + k * (2 * hx - 4) / 8;
                lbox(mb, x, {xx - 0.3f, h - 1.5f, hz - 0.2f}, {xx + 0.3f, h - 1.0f, hz + 0.4f}, lc[k % 3]);
            }
            // публика перед сценой
            for (int row = 0; row < 5; row++) people(mb, x, {-hx + 3, -0.05f, hz + 3.0f + row * 1.6f}, {hx - 3, -0.05f, hz + 3.0f + row * 1.6f}, 22 - row, 100u + row);
            break;
        }
        case BS_SCREEN: {
            Col8 dark = C(24, 24, 28);
            lbox(mb, x, {-hx * 0.8f - 0.3f, -1.0f, -0.3f}, {-hx * 0.8f + 0.3f, 4.2f, 0.3f}, C(80, 80, 88));
            lbox(mb, x, {hx * 0.8f - 0.3f, -1.0f, -0.3f}, {hx * 0.8f + 0.3f, 4.2f, 0.3f}, C(80, 80, 88));
            lbox(mb, x, {-hx, 4.0f, -hz}, {hx, h, hz}, dark, true);
            V3 a = x.apply({-hx + 0.3f, 4.3f, hz + 0.02f}), bb = x.apply({hx - 0.3f, 4.3f, hz + 0.02f});
            V3 c = x.apply({hx - 0.3f, h - 0.3f, hz + 0.02f}), d = x.apply({-hx + 0.3f, h - 0.3f, hz + 0.02f});
            gradQuad(mb, a, bb, c, d, C(40, 160, 255, A_EMIT), C(230, 60, 200, A_EMIT));
            break;
        }
        case BS_GRANDSTAND: {
            int tiers = 6;
            float depth = 2 * hz / tiers;
            Col8 acc = C(255, 110, 40);
            for (int k = 0; k < tiers; k++) {
                float z1 = hz - k * depth, z0 = z1 - depth;
                float top = 0.8f + k * 1.25f;
                lbox(mb, x, {-hx, base, z0}, {hx, top, z1}, (k % 2) ? wc : shade(wc, 0.85f));
                lbox(mb, x, {-hx, top, z0 + depth * 0.55f}, {hx, top + 0.45f, z0 + depth * 0.75f}, (k % 2) ? acc : C(40, 120, 220));
                people(mb, x, {-hx + 1.5f, top, z0 + depth * 0.4f}, {hx - 1.5f, top, z0 + depth * 0.4f}, 26, 30u + k);
            }
            float topB = 0.8f + (tiers - 1) * 1.25f;
            lbox(mb, x, {-hx, base, -hz - 0.4f}, {hx, topB + 3.0f, -hz}, wc);
            // навес
            V3 r0 = x.apply({-hx - 1, h + 2.5f, -hz - 0.4f}), r1 = x.apply({hx + 1, h + 2.5f, -hz - 0.4f});
            V3 r2 = x.apply({hx + 1, h + 1.2f, hz * 0.7f}), r3 = x.apply({-hx - 1, h + 1.2f, hz * 0.7f});
            mb.quad(r3, r2, r1, r0, C(240, 240, 244));
            mb.quad(r0, r1, r2, r3, C(120, 120, 128));
            for (int k = 0; k <= 4; k++) {
                float xx = -hx + k * hx / 2;
                lbox(mb, x, {xx - 0.25f, topB + 3.0f, -hz - 0.4f}, {xx + 0.25f, h + 2.4f, -hz}, C(200, 200, 206));
            }
            for (float sx : {-1.0f, 1.0f}) {
                V3 p0 = x.apply({sx * hx, 0, hz}), p1 = x.apply({sx * hx, 0, -hz}), p2 = x.apply({sx * hx, topB + 3.0f, -hz});
                if (sx > 0) mb.triangle(p0, p1, p2, wc);
                else mb.triangle(p1, p0, p2, wc);
            }
            break;
        }
        case BS_LIGHTHOUSE: {
            int bands = 6;
            for (int k = 0; k < bands; k++) {
                float y0 = k * h / bands - (k == 0 ? 2.0f : 0), y1 = (k + 1) * h / bands;
                float r0 = mixf(hx, hx * 0.65f, std::max(y0, 0.0f) / h), r1 = mixf(hx, hx * 0.65f, y1 / h);
                mb.cylinder(x, {0, y0, 0}, r0, r1, y1 - y0, 16, (k % 2) ? C(200, 44, 40) : C(248, 248, 248), false, true);
            }
            mb.cylinder(x, {0, h, 0}, hx * 0.9f, hx * 0.9f, 0.45f, 16, C(60, 60, 66), true, true);
            mb.cylinder(x, {0, h + 0.45f, 0}, hx * 0.5f, hx * 0.5f, 3.0f, 12, C(255, 236, 170, A_NIGHT), false, true);
            mb.cylinder(x, {0, h + 3.45f, 0}, hx * 0.62f, 0.05f, 2.2f, 12, C(180, 40, 36), false, true);
            lbox(mb, x, {-0.7f, 0, hx - 0.2f}, {0.7f, 2.2f, hx + 0.1f}, C(80, 60, 40));
            break;
        }
        case BS_OBSERVATORY: {
            mb.cylinder(x, {0, base, 0}, hx, hx, 7.0f - base, 20, wc, false, true);
            int rings = 6;
            for (int k = 0; k < rings; k++) {
                float a0 = PI * 0.5f * k / rings, a1 = PI * 0.5f * (k + 1) / rings;
                float r0 = std::cos(a0) * hx * 0.96f, r1 = std::cos(a1) * hx * 0.96f;
                float y0 = 7.0f + std::sin(a0) * hx * 0.8f, y1 = 7.0f + std::sin(a1) * hx * 0.8f;
                mb.cylinder(x, {0, y0, 0}, r0, std::max(r1, 0.01f), y1 - y0, 20, C(206, 210, 216), k == rings - 1, true);
            }
            lbox(mb, x, {-0.8f, 7.2f, hx * 0.2f}, {0.8f, 7.0f + hx * 0.78f, hx * 0.93f}, C(40, 44, 52));
            lbox(mb, x, {-1.0f, 0, hx - 0.3f}, {1.0f, 2.4f, hx + 0.05f}, C(90, 70, 50));
            break;
        }
        case BS_TOWERMAST: {
            if (hx >= 3.0f) {
                // диспетчерская вышка
                mb.cylinder(x, {0, base, 0}, 2.2f, 1.9f, h - 5.0f - base, 10, wc, false, true);
                int n = 8;
                float r = hx * 0.95f;
                for (int i = 0; i < n; i++) {
                    float a0 = i * TAU / n, a1 = (i + 1) * TAU / n;
                    float ax = std::cos(a0) * r, az = std::sin(a0) * r, bx = std::cos(a1) * r, bz = std::sin(a1) * r;
                    wall(mb, x, bx, bz, ax, az, h - 5.0f, h - 1.5f, C(170, 200, 220, A_FACADE_GLASS), 4.0f, 3.5f);
                }
                mb.cylinder(x, {0, h - 5.4f, 0}, hx * 1.05f, hx * 1.05f, 0.4f, 8, C(220, 220, 224), true, false);
                mb.cylinder(x, {0, h - 1.5f, 0}, hx * 1.1f, hx * 1.1f, 0.5f, 8, C(220, 220, 224), true, false);
                mb.cylinder(x, {0, h - 1.0f, 0}, 0.1f, 0.05f, 4.0f, 5, C(200, 200, 205), false, true);
                lbox(mb, x, {-0.2f, h + 3.0f, -0.2f}, {0.2f, h + 3.3f, 0.2f}, C(255, 40, 30, A_NIGHT));
            } else {
                // радиомачта с полосами
                int bands = 7;
                for (int k = 0; k < bands; k++) {
                    float y0 = k * h / bands - (k == 0 ? 1.0f : 0), y1 = (k + 1) * h / bands;
                    float r0 = mixf(hx * 0.55f, hx * 0.18f, std::max(y0, 0.0f) / h), r1 = mixf(hx * 0.55f, hx * 0.18f, y1 / h);
                    mb.cylinder(x, {0, y0, 0}, r0, r1, y1 - y0, 4, (k % 2) ? C(250, 250, 250) : wc, false, false);
                }
                lbox(mb, x, {-hx * 0.8f, 0, -hx * 0.8f}, {hx * 0.8f, 1.2f, hx * 0.8f}, C(150, 150, 150));
                lbox(mb, x, {-0.25f, h, -0.25f}, {0.25f, h + 0.5f, 0.25f}, C(255, 40, 30, A_NIGHT));
            }
            break;
        }
        case BS_ARCH: {
            float pw = 0.8f;
            Col8 cc = wc;
            for (float sx : {-1.0f, 1.0f}) {
                lbox(mb, x, {sx * hx - pw, -1.5f, -1.0f}, {sx * hx + pw, h, 1.0f}, cc);
                // светящиеся полосы на внутренней стороне
                float xi = sx * hx - sx * pw - sx * 0.02f;
                V3 a = x.apply({xi, 0.5f, -0.6f}), bb = x.apply({xi, 0.5f, 0.6f}), c = x.apply({xi, h - 2.8f, 0.6f}), d = x.apply({xi, h - 2.8f, -0.6f});
                if (sx > 0) mb.quad(bb, a, d, c, C(90, 230, 255, A_EMIT));
                else mb.quad(a, bb, c, d, C(90, 230, 255, A_EMIT));
            }
            lbox(mb, x, {-hx - pw, h - 2.6f, -1.0f}, {hx + pw, h, 1.0f}, cc, true);
            for (int s = 0; s < 2; s++) {
                float z = s ? -1.02f : 1.02f;
                V3 a = x.apply({-hx + 0.4f, h - 2.3f, z}), bb = x.apply({hx - 0.4f, h - 2.3f, z});
                V3 c = x.apply({hx - 0.4f, h - 0.3f, z}), d = x.apply({-hx + 0.4f, h - 0.3f, z});
                if (s) gradQuad(mb, bb, a, d, c, C(236, 40, 128, A_EMIT), C(255, 160, 40, A_EMIT));
                else gradQuad(mb, a, bb, c, d, C(236, 40, 128, A_EMIT), C(255, 160, 40, A_EMIT));
            }
            break;
        }
    }
}

void rampGeometry(MeshBuilder& mb, const Ramp& r) {
    Xf x{r.c, r.yaw, 1};
    float hx = r.hx, hz = r.hz;
    float b = -1.6f;
    Col8 top = C(150, 142, 126), side = C(60, 60, 64), stripeY = C(250, 200, 30), stripeK = C(30, 30, 30);
    int n = 10;
    for (int k = 0; k < n; k++) {
        float t0 = (float)k / n, t1 = (float)(k + 1) / n;
        float z0 = -hz + 2 * hz * t0, z1 = -hz + 2 * hz * t1;
        float y0 = mixf(r.h0, r.h1, t0) + 0.01f, y1 = mixf(r.h0, r.h1, t1) + 0.01f;
        Col8 pc = (k % 2) ? top : shade(top, 0.9f);
        mb.quad(x.apply({-hx + 0.3f, y0, z0}), x.apply({-hx + 0.3f, y1, z1}), x.apply({hx - 0.3f, y1, z1}), x.apply({hx - 0.3f, y0, z0}), pc);
        for (float sx : {-1.0f, 1.0f}) {
            float xa = sx > 0 ? hx - 0.3f : -hx, xb = sx > 0 ? hx : -hx + 0.3f;
            mb.quad(x.apply({xa, y0, z0}), x.apply({xa, y1, z1}), x.apply({xb, y1, z1}), x.apply({xb, y0, z0}), stripeY);
            // борта: жёлто-чёрные полосы
            float xs = sx * hx;
            V3 p0 = x.apply({xs, b, z0}), p1 = x.apply({xs, b, z1}), p2 = x.apply({xs, y1, z1}), p3 = x.apply({xs, y0, z0});
            Col8 sc = (k % 2) ? stripeY : stripeK;
            if (sx > 0) mb.quad(p1, p0, p3, p2, shade(sc, 0.9f));
            else mb.quad(p0, p1, p2, p3, shade(sc, 0.9f));
        }
    }
    // задняя стенка
    V3 a = x.apply({hx, b, hz}), bb = x.apply({-hx, b, hz}), c = x.apply({-hx, r.h1 + 0.01f, hz}), d = x.apply({hx, r.h1 + 0.01f, hz});
    mb.quad(bb, a, d, c, side);
}

}  // namespace cl
