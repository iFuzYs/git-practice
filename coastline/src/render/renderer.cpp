#include "renderer.h"

#include <algorithm>
#include <cstring>

#include "../core/noise.h"
#include "raymath.h"
#include "rlgl.h"

#if defined(PLATFORM_WEB)
#include <GLES3/gl3.h>
#endif

namespace cl {

Matrix matFrom(V3 p, Q q, float s) {
    V3 bx = qrot(q, {1, 0, 0}) * s, by = qrot(q, {0, 1, 0}) * s, bz = qrot(q, {0, 0, 1}) * s;
    Matrix m{};
    m.m0 = bx.x; m.m1 = bx.y; m.m2 = bx.z; m.m3 = 0;
    m.m4 = by.x; m.m5 = by.y; m.m6 = by.z; m.m7 = 0;
    m.m8 = bz.x; m.m9 = bz.y; m.m10 = bz.z; m.m11 = 0;
    m.m12 = p.x; m.m13 = p.y; m.m14 = p.z; m.m15 = 1;
    return m;
}

static Matrix matYaw(V3 p, float yaw, float s) {
    float c = std::cos(yaw) * s, sn = std::sin(yaw) * s;
    Matrix m{};
    m.m0 = c; m.m1 = 0; m.m2 = -sn; m.m3 = 0;
    m.m4 = 0; m.m5 = s; m.m6 = 0; m.m7 = 0;
    m.m8 = sn; m.m9 = 0; m.m10 = c; m.m11 = 0;
    m.m12 = p.x; m.m13 = p.y; m.m14 = p.z; m.m15 = 1;
    return m;
}

CarDraw carDrawFrom(const Vehicle& v, float lights) {
    CarDraw d;
    d.spec = v.spec;
    d.pos = v.pos;
    d.rot = v.rot;
    d.color = v.color;
    for (int i = 0; i < 4; i++) {
        const WheelState& wh = v.w[i];
        d.wheelC[i] = wh.local + V3{0, -(wh.rest - wh.comp), 0};
        d.steer[i] = wh.steer;
        d.spin[i] = wh.spin;
    }
    d.lights = lights;
    d.brake = v.brakeLights;
    d.reverse = v.gear == -1 ? 1.0f : 0.0f;
    d.dirt = clamp01(v.damage);
    return d;
}

CarDraw carDrawStatic(const CarSpec* s, V3 ground, float yaw, uint32_t color) {
    CarDraw d;
    d.spec = s;
    d.pos = ground + V3{0, s->cgH, 0};
    d.rot = qyaw(yaw);
    d.color = color;
    float zF = s->wheelbase * (1.0f - s->frontWeight), zR = -s->wheelbase * s->frontWeight;
    for (int i = 0; i < 4; i++) {
        bool front = i < 2, left = (i % 2) == 0;
        d.wheelC[i] = {left ? s->track * 0.5f : -s->track * 0.5f, -(s->cgH - s->wheelR), front ? zF : zR};
    }
    return d;
}

// ------------------------------------------------------------------ служебное
Renderer::SMesh Renderer::uploadMB(const MeshBuilder& mb) {
    SMesh s;
    s.mn = {1e30f, 1e30f, 1e30f};
    s.mx = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < mb.count(); i++) {
        V3 p{mb.pos[i * 3], mb.pos[i * 3 + 1], mb.pos[i * 3 + 2]};
        s.mn = {std::min(s.mn.x, p.x), std::min(s.mn.y, p.y), std::min(s.mn.z, p.z)};
        s.mx = {std::max(s.mx.x, p.x), std::max(s.mx.y, p.y), std::max(s.mx.z, p.z)};
    }
    s.m = mb.upload();
    // копии в ОЗУ больше не нужны
    RL_FREE(s.m.vertices); s.m.vertices = nullptr;
    RL_FREE(s.m.normals); s.m.normals = nullptr;
    RL_FREE(s.m.texcoords); s.m.texcoords = nullptr;
    RL_FREE(s.m.colors); s.m.colors = nullptr;
    // raylib рисует индексированный меш, только если indices != NULL (данные берутся из VBO),
    // поэтому вместо большого массива оставляем крошечную заглушку
    RL_FREE(s.m.indices);
    s.m.indices = (unsigned short*)RL_MALLOC(sizeof(unsigned short));
    return s;
}

int Renderer::cellOf(float x, float z) const {
    int i = clampf((x - World::ORIGIN) / PCS, 0, PC - 1), j = clampf((z - World::ORIGIN) / PCS, 0, PC - 1);
    return j * PC + i;
}

void Renderer::setFrustum(const Matrix& m) {
    float r0[4] = {m.m0, m.m4, m.m8, m.m12};
    float r1[4] = {m.m1, m.m5, m.m9, m.m13};
    float r2[4] = {m.m2, m.m6, m.m10, m.m14};
    float r3[4] = {m.m3, m.m7, m.m11, m.m15};
    for (int k = 0; k < 4; k++) {
        planes_[0][k] = r3[k] + r0[k];
        planes_[1][k] = r3[k] - r0[k];
        planes_[2][k] = r3[k] + r1[k];
        planes_[3][k] = r3[k] - r1[k];
        planes_[4][k] = r3[k] + r2[k];
        planes_[5][k] = r3[k] - r2[k];
    }
}

bool Renderer::visible(V3 mn, V3 mx) const {
    for (int i = 0; i < 6; i++) {
        const float* p = planes_[i];
        float x = p[0] >= 0 ? mx.x : mn.x, y = p[1] >= 0 ? mx.y : mn.y, z = p[2] >= 0 ? mx.z : mn.z;
        if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0) return false;
    }
    return true;
}

static Material makeMat(Shader s) {
    Material m = LoadMaterialDefault();
    m.shader = s;
    return m;
}

// ------------------------------------------------------------------ построение
void Renderer::buildTerrain() {
    const World& w = *w_;
    const int CH = 64, NCH = (World::N - 1) / CH;
    for (int lod = 0; lod < 2; lod++) {
        int step = lod ? 2 : 1;
        int n = CH / step + 1;
        for (int cj = 0; cj < NCH; cj++)
            for (int ci = 0; ci < NCH; ci++) {
                MeshBuilder mb;
                auto vtx = [&](int gi, int gj, float drop) {
                    int k = w.idx(gi, gj);
                    V3 p{w.vx(gi), w.height[k] - drop, w.vx(gj)};
                    Col8 c{w.tint[k * 4], w.tint[k * 4 + 1], w.tint[k * 4 + 2], w.tint[k * 4 + 3]};
                    return mb.vert(p, w.normal[k], 0, 0, c);
                };
                for (int j = 0; j < n; j++)
                    for (int i = 0; i < n; i++) vtx(ci * CH + i * step, cj * CH + j * step, 0);
                for (int j = 0; j < n - 1; j++)
                    for (int i = 0; i < n - 1; i++) {
                        int v00 = j * n + i, v10 = v00 + 1, v01 = v00 + n, v11 = v01 + 1;
                        mb.tri(v00, v01, v10);
                        mb.tri(v11, v10, v01);
                    }
                // «юбки» по краям участка закрывают щели между уровнями детализации
                for (int e = 0; e < 4; e++) {
                    bool boundary = (e == 0 && cj == 0) || (e == 1 && cj == NCH - 1) || (e == 2 && ci == 0) || (e == 3 && ci == NCH - 1);
                    float drop = boundary ? 80.0f : (lod ? 6.0f : 3.0f);
                    int prevTop = -1, prevBot = -1;
                    for (int t = 0; t < n; t++) {
                        int i = e == 2 ? 0 : e == 3 ? n - 1 : t;
                        int j = e == 0 ? 0 : e == 1 ? n - 1 : t;
                        int top = j * n + i;
                        int bot = vtx(ci * CH + i * step, cj * CH + j * step, drop);
                        if (prevTop >= 0) {
                            mb.tri(prevTop, top, bot);
                            mb.tri(prevTop, bot, prevBot);
                            mb.tri(prevTop, bot, top);
                            mb.tri(prevTop, prevBot, bot);
                        }
                        prevTop = top;
                        prevBot = bot;
                    }
                }
                terr_[lod].push_back(uploadMB(mb));
            }
    }
}

void Renderer::buildOuter() {
    const World& w = *w_;
    Noise nz(w.seed + 5);
    const float E = 1536, R = 6400, st = 128;
    int n = (int)(2 * R / st) + 1;
    std::vector<float> h(n * n);
    auto X = [&](int i) { return -R + i * st; };
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            float x = X(i), z = X(j);
            float cx = clampf(x, -E + 6, E - 6), cz = clampf(z, -E + 6, E - 6);
            float he = w.terrainH(cx, cz);
            float d = std::max(std::fabs(x), std::fabs(z)) - E;
            float v;
            if (d <= 0) v = he - 8.0f;
            else if (he > 3.0f) {
                float r = nz.ridged(x * 0.0009f + 3.0f, z * 0.0009f - 1.0f, 4);
                float far = 90.0f + 330.0f * r;
                v = mixf(he, far, smoothstep(0.0f, 1400.0f, d));
            } else v = std::min(he, -6.0f) - d * 0.006f;
            h[j * n + i] = v;
        }
    MeshSet ms;
    std::vector<int> vi(n * n, -1);
    // сетка делится на части по 64 тыс. вершин; для простоты строим одну часть на полосу
    int bands = 4;
    for (int b = 0; b < bands; b++) {
        MeshBuilder mb;
        std::fill(vi.begin(), vi.end(), -1);
        int j0 = b * (n - 1) / bands, j1 = (b + 1) * (n - 1) / bands;
        auto V = [&](int i, int j) {
            int k = j * n + i;
            if (vi[k] >= 0) return vi[k];
            float hl = h[j * n + std::max(i - 1, 0)], hr = h[j * n + std::min(i + 1, n - 1)];
            float hd = h[std::max(j - 1, 0) * n + i], hu = h[std::min(j + 1, n - 1) * n + i];
            V3 nn = norm(V3{hl - hr, 2 * st, hd - hu});
            float hh = h[k];
            Col8 c{0, (uint8_t)(hh > -2.5f && hh < 3.0f ? 255 : 0), 0, 0};
            vi[k] = mb.vert({X(i), hh, X(j)}, nn, 0, 0, c);
            return vi[k];
        };
        for (int j = j0; j < j1; j++)
            for (int i = 0; i < n - 1; i++) {
                float x0 = X(i), x1 = X(i + 1), z0 = X(j), z1 = X(j + 1);
                bool inside = x0 > -E && x1 < E && z0 > -E && z1 < E;
                if (inside) continue;
                int v00 = V(i, j), v10 = V(i + 1, j), v01 = V(i, j + 1), v11 = V(i + 1, j + 1);
                mb.tri(v00, v01, v10);
                mb.tri(v11, v10, v01);
            }
        if (!mb.empty()) outer_.push_back(uploadMB(mb));
    }
}

static float roadLift(const Road& r) {
    switch (r.type) {
        case ROAD_HIGHWAY: return 0.08f;
        case ROAD_MAIN: return 0.068f;
        case ROAD_STREET: return 0.045f + 0.007f * (r.id % 3);
        case ROAD_DIRT: return 0.035f;
        default: return 0.03f;
    }
}

void Renderer::buildRoads() {
    const World& w = *w_;
    for (const Road& r : w.roads) {
        int n = (int)r.pts.size();
        if (n < 2) continue;
        int segs = r.loop ? n : n - 1;
        float period = r.type == ROAD_RUNWAY ? 60.0f : 12.0f;
        float hw = r.halfW(), lift = roadLift(r);
        Col8 white{255, 255, 255, 255};
        for (int start = 0; start < segs; start += 48) {
            int end = std::min(start + 48, segs);
            MeshBuilder mb;
            for (int i = start; i <= end; i++) {
                const RoadPt& p = r.pts[i % n];
                float s = i == n ? r.length : p.s;
                V3 up = norm(cross(p.t, p.l));
                if (up.y < 0) up = up * -1.0f;
                V3 L = p.p + p.l * hw + V3{0, lift, 0}, R = p.p - p.l * hw + V3{0, lift, 0};
                mb.vert(L, up, 0, s / period, white);
                mb.vert(R, up, 1, s / period, white);
            }
            for (int k = 0; k < end - start; k++) {
                int L0 = k * 2, R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
                mb.tri(L0, R0, L1);
                mb.tri(L1, R0, R1);
            }
            roads_[r.type].push_back(uploadMB(mb));
        }
    }
}

void Renderer::buildStatics() {
    const World& w = *w_;
    const int SC = 12;
    const float SCS = 256.0f;
    std::vector<MeshSet> cells(SC * SC);
    auto cellFor = [&](float x, float z) {
        int i = clampf((x - World::ORIGIN) / SCS, 0, SC - 1), j = clampf((z - World::ORIGIN) / SCS, 0, SC - 1);
        return j * SC + i;
    };
    MeshBuilder tmp;
    for (const Building& b : w.buildings) {
        tmp.clear();
        buildingGeometry(tmp, b);
        cells[cellFor(b.c.x, b.c.z)].get(tmp.count()).append(tmp, Xf{});
    }
    for (const Ramp& r : w.ramps) {
        tmp.clear();
        rampGeometry(tmp, r);
        cells[cellFor(r.c.x, r.c.z)].get(tmp.count()).append(tmp, Xf{});
    }
    // мосты: плита под полотном и опоры
    Col8 conc{176, 172, 164, 255}, concD{130, 128, 122, 255};
    for (const Road& r : w.roads) {
        int n = (int)r.pts.size();
        int segs = r.loop ? n : n - 1;
        float hw = r.halfW() + 0.55f, lift = roadLift(r);
        for (int i = 0; i < segs; i++) {
            const RoadPt& a = r.pts[i];
            const RoadPt& b = r.pts[(i + 1) % n];
            if (!a.bridge && !b.bridge) continue;
            tmp.clear();
            V3 aL = a.p + a.l * hw, aR = a.p - a.l * hw, bL = b.p + b.l * hw, bR = b.p - b.l * hw;
            float ta = lift - 0.03f, bt = -1.15f;
            V3 up{0, 1, 0};
            // боковые грани
            tmp.quad(aL + up * bt, bL + up * bt, bL + up * ta, aL + up * ta, conc);
            tmp.quad(bR + up * bt, aR + up * bt, aR + up * ta, bR + up * ta, conc);
            // низ
            tmp.quad(aL + up * bt, aR + up * bt, bR + up * bt, bL + up * bt, concD);
            // опоры каждые ~30 м
            float s0 = a.s, s1 = i + 1 == n ? r.length : b.s;
            if (a.bridge && (int)(s0 / 30.0f) != (int)(s1 / 30.0f)) {
                float g = w.terrainH(a.p.x, a.p.z);
                float bottom = std::min(g, 0.0f) - 3.0f;
                if (a.p.y - g > 2.2f) {
                    int cnt = r.type == ROAD_HIGHWAY ? 2 : 1;
                    for (int k = 0; k < cnt; k++) {
                        float off = cnt == 1 ? 0.0f : (k ? -1.0f : 1.0f) * hw * 0.5f;
                        V3 c = a.p + a.l * off;
                        Xf x{V3{c.x, bottom, c.z}, std::atan2(a.t.x, a.t.z), 1};
                        tmp.cylinder(x, {0, 0, 0}, 0.95f, 0.8f, a.p.y - 1.1f - bottom, 10, conc, false, true);
                    }
                    Xf cap{V3{a.p.x, a.p.y, a.p.z}, std::atan2(a.t.x, a.t.z), 1};
                    tmp.box(cap, {-hw * 0.8f, -1.8f, -0.9f}, {hw * 0.8f, -1.1f, 0.9f}, concD, true);
                }
            }
            cells[cellFor(a.p.x, a.p.z)].get(tmp.count()).append(tmp, Xf{});
        }
    }
    for (auto& ms : cells)
        for (auto& part : ms.parts)
            if (!part.empty()) statics_.push_back(uploadMB(part));
}

void Renderer::buildProps() {
    const World& w = *w_;
    MeshBuilder farProto[PK_COUNT][PROP_VARS];
    for (int k = 0; k < PK_COUNT; k++)
        for (int v = 0; v < propVariants((PropKind)k) && v < PROP_VARS; v++) {
            MeshBuilder mb;
            propGeometry(mb, (PropKind)k, v, 0);
            if (!mb.empty()) {
                prop_[k][v] = uploadMB(mb).m;
                propOk_[k][v] = true;
            }
            if (k == PK_PINE || k == PK_OAK || k == PK_BIRCH || k == PK_PALM || k == PK_ROCK) propGeometry(farProto[k][v], (PropKind)k, v, 1);
        }
    {
        MeshBuilder mb;
        rotorGeometry(mb);
        rotor_ = uploadMB(mb).m;
    }
    cellProps_.assign(PC * PC, {});
    cellMin_.assign(PC * PC, V3{1e30f, 1e30f, 1e30f});
    cellMax_.assign(PC * PC, V3{-1e30f, -1e30f, -1e30f});
    for (int i = 0; i < (int)w.props.size(); i++) {
        const Prop& p = w.props[i];
        if (p.kind == PK_TURBINE) {
            turbines_.push_back(i);
            continue;
        }
        if (p.kind == PK_LAMP) lamps_.push_back(i);
        if (p.kind == PK_POLE) poles_.push_back(i);
        int c = cellOf(p.pos.x, p.pos.z);
        cellProps_[c].push_back(i);
        float top = p.pos.y + 12.0f * p.scale;
        cellMin_[c] = {std::min(cellMin_[c].x, p.pos.x - 6), std::min(cellMin_[c].y, p.pos.y - 2), std::min(cellMin_[c].z, p.pos.z - 6)};
        cellMax_[c] = {std::max(cellMax_[c].x, p.pos.x + 6), std::max(cellMax_[c].y, top), std::max(cellMax_[c].z, p.pos.z + 6)};
    }
    far_.assign(PC * PC, {});
    for (int c = 0; c < PC * PC; c++) {
        MeshSet ms;
        for (int idx : cellProps_[c]) {
            const Prop& p = w.props[idx];
            const MeshBuilder& proto = farProto[p.kind][std::min((int)p.var, PROP_VARS - 1)];
            if (proto.empty()) continue;
            ms.get(proto.count()).append(proto, Xf{p.pos, p.yaw, p.scale});
        }
        for (auto& part : ms.parts)
            if (!part.empty()) far_[c].push_back(uploadMB(part));
    }
    // источники свечения
    for (int idx : lamps_) {
        const Prop& p = w.props[idx];
        Glow g{Xf{p.pos, p.yaw, p.scale}.apply({0, 7.0f, 1.77f}), {255, 196, 120, 255}, 2.6f, 0, idx, 420};
        glows_.push_back(g);
    }
    for (int idx : turbines_) {
        const Prop& p = w.props[idx];
        glows_.push_back({Xf{p.pos, p.yaw, p.scale}.apply({0, 74.6f, -1.95f}), {255, 40, 30, 255}, 5.0f, 2, -1, 2600});
    }
    for (const Building& b : w.buildings) {
        Xf x{b.c, b.yaw, 1};
        if (b.style == BS_TOWER) glows_.push_back({x.apply({0, b.h + 8.75f, 0}), {255, 40, 30, 255}, 3.0f, 2, -1, 1800});
        if (b.style == BS_TOWERMAST) glows_.push_back({x.apply({0, b.h + (b.hx >= 3 ? 3.15f : 0.25f), 0}), {255, 40, 30, 255}, 4.0f, 2, -1, 2600});
        if (b.style == BS_LIGHTHOUSE) glows_.push_back({x.apply({0, b.h + 1.9f, 0}), {255, 236, 180, 255}, 9.0f, 0, -1, 3000});
        if (b.style == BS_STAGE)
            for (int k = 0; k < 9; k += 2) {
                float xx = -b.hx + 2 + k * (2 * b.hx - 4) / 8;
                Color c = k % 3 == 0 ? Color{120, 220, 255, 255} : Color{255, 120, 220, 255};
                glows_.push_back({x.apply({xx, b.h - 1.3f, b.hz + 0.5f}), c, 3.5f, 0, -1, 900});
            }
        if (b.style == BS_ARCH)
            for (float sx : {-1.0f, 1.0f}) glows_.push_back({x.apply({sx * b.hx, b.h - 1.3f, 0}), {255, 140, 60, 255}, 5.0f, 0, -1, 900});
    }
}

void Renderer::buildShadow() {
    freeShadow();
    shadowSize_ = quality >= 1 ? 2048 : 1024;
    shadowHalf_ = quality >= 2 ? 120.0f : quality == 1 ? 90.0f : 60.0f;
    RenderTexture2D rt{};
    rt.id = rlLoadFramebuffer();
    if (rt.id == 0) {
        shadows = false;
        return;
    }
    rt.texture.width = rt.texture.height = shadowSize_;
    rlEnableFramebuffer(rt.id);
#if defined(PLATFORM_WEB)
    // WebGL2 принимает только «размерный» формат глубины — raylib 5.5 передаёт безразмерный
    {
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        rt.depth.id = id;
    }
#else
    rt.depth.id = rlLoadTextureDepth(shadowSize_, shadowSize_, false);
#endif
    rt.depth.width = rt.depth.height = shadowSize_;
    rt.depth.format = 19;
    rt.depth.mipmaps = 1;
    rlFramebufferAttach(rt.id, rt.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
    bool ok = rlFramebufferComplete(rt.id);
    rlDisableFramebuffer();
    if (!ok || rt.depth.id == 0) {
        TraceLog(LOG_WARNING, "COASTLINE: карта теней недоступна, тени отключены");
        if (rt.depth.id) rlUnloadTexture(rt.depth.id);
        rlUnloadFramebuffer(rt.id);
        shadows = false;
        return;
    }
    shadowRT_ = rt;
}

void Renderer::freeShadow() {
    if (shadowRT_.id) {
        rlUnloadTexture(shadowRT_.depth.id);
        rlUnloadFramebuffer(shadowRT_.id);
    }
    shadowRT_ = RenderTexture2D{};
}

static Mesh planeMesh() {
    MeshBuilder mb;
    Col8 white{245, 245, 248, 255}, orange{255, 110, 40, 255}, dark{40, 44, 52, 255};
    mb.cylinderAxis({0, 0, -4.2f}, {0, 0, 3.0f}, 0.45f, 0.75f, 10, white, true);
    mb.cylinderAxis({0, 0, 3.0f}, {0, 0, 4.1f}, 0.75f, 0.2f, 10, orange, true);
    Xf id;
    mb.box(id, {-5.8f, -0.1f, 0.2f}, {5.8f, 0.08f, 1.7f}, white, true);
    mb.box(id, {-5.9f, -0.12f, 0.2f}, {-4.9f, 0.1f, 1.7f}, orange, true);
    mb.box(id, {4.9f, -0.12f, 0.2f}, {5.9f, 0.1f, 1.7f}, orange, true);
    mb.box(id, {-2.0f, 0.1f, -4.3f}, {2.0f, 0.2f, -3.4f}, white, true);
    mb.box(id, {-0.07f, 0.1f, -4.4f}, {0.07f, 1.7f, -3.4f}, orange, true);
    mb.box(id, {-0.5f, 0.35f, 1.2f}, {0.5f, 0.85f, 2.6f}, Col8{70, 110, 150, 255}, false);
    mb.box(id, {-0.12f, -1.1f, 1.8f}, {0.12f, -0.3f, 2.0f}, dark, true);
    mb.box(id, {-1.3f, -1.2f, 1.6f}, {1.3f, -1.1f, 2.1f}, dark, true);
    mb.cylinderAxis({0, 0, 4.1f}, {0, 0, 4.25f}, 0.15f, 0.1f, 6, dark, true);
    mb.box(id, {-0.08f, -1.4f, 4.18f}, {0.08f, 1.4f, 4.22f}, dark, true);
    Mesh m = mb.upload();
    return m;
}

static Mesh balloonMesh() {
    MeshBuilder mb;
    Xf id;
    Col8 cols[4] = {{255, 110, 40, 255}, {255, 210, 50, 255}, {60, 180, 255, 255}, {240, 70, 140, 255}};
    int seg = 12;
    for (int k = 0; k < 8; k++) {
        float a0 = PI * k / 8, a1 = PI * (k + 1) / 8;
        float r0 = std::sin(a0) * 8.0f * (k < 4 ? 1.0f : 0.94f), r1 = std::sin(a1) * 8.0f * (k + 1 < 4 ? 1.0f : 0.94f);
        float y0 = 22 - std::cos(a0) * 9.5f, y1 = 22 - std::cos(a1) * 9.5f;
        (void)seg;
        for (int s = 0; s < 12; s++) {
            float b0 = s * TAU / 12, b1 = (s + 1) * TAU / 12;
            V3 p00{std::cos(b0) * r0, y0, std::sin(b0) * r0}, p01{std::cos(b1) * r0, y0, std::sin(b1) * r0};
            V3 p10{std::cos(b0) * r1, y1, std::sin(b0) * r1}, p11{std::cos(b1) * r1, y1, std::sin(b1) * r1};
            mb.quad(p00, p10, p11, p01, cols[s % 4]);
        }
    }
    // корзина и стропы
    mb.box(id, {-0.8f, 0, -0.8f}, {0.8f, 1.1f, 0.8f}, Col8{120, 86, 50, 255}, true);
    for (int s = 0; s < 4; s++) {
        float a = s * TAU / 4 + PI / 4;
        mb.cylinderAxis({std::cos(a) * 0.75f, 1.1f, std::sin(a) * 0.75f}, {std::cos(a) * 3.0f, 12.8f, std::sin(a) * 3.0f}, 0.03f, 0.03f, 3, Col8{60, 50, 40, 255}, false);
    }
    return mb.upload();
}

void Renderer::init(const World& w, Font display, int q, bool shadowsOn) {
    w_ = &w;
    quality = q;
    shadows = shadowsOn;
    sh.load();
    tex.build(display, q);
    for (Texture2D* t : {&tex.grass, &tex.rock, &tex.sand, &tex.gravel, &tex.paved}) SetTextureFilter(*t, TEXTURE_FILTER_ANISOTROPIC_4X);
    for (auto& t : tex.road) SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);
    rlSetClipPlanes(0.4, 3200.0);

    mTerrain_ = makeMat(sh.terrain);
    mTerrain_.maps[1].texture = tex.grass;
    mTerrain_.maps[2].texture = tex.rock;
    mTerrain_.maps[3].texture = tex.sand;
    mTerrain_.maps[4].texture = tex.gravel;
    mTerrain_.maps[5].texture = tex.paved;
    for (int t = 0; t < 5; t++) {
        mRoad_[t] = makeMat(sh.road);
        mRoad_[t].maps[0].texture = tex.road[t];
    }
    mObj_ = makeMat(sh.object);
    mObj_.maps[0].texture = tex.facade;
    mInst_ = makeMat(sh.objectInst);
    mInst_.maps[0].texture = tex.facade;
    mCar_ = makeMat(sh.car);
    mWater_ = makeMat(sh.water);
    mSky_ = makeMat(sh.sky);
    mDepth_ = makeMat(sh.depth);
    mDepthInst_ = makeMat(sh.depthInst);

    buildTerrain();
    buildOuter();
    buildRoads();
    buildStatics();
    buildProps();

    // карта глубин для воды
    {
        const int S = 512;
        std::vector<uint8_t> px(S * S);
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                float wx = World::ORIGIN + (x + 0.5f) / S * 3072.0f, wz = World::ORIGIN + (y + 0.5f) / S * 3072.0f;
                float h = w.terrainH(wx, wz);
                px[y * S + x] = (uint8_t)(clamp01(-h / 8.0f) * 255);
            }
        Image im = {px.data(), S, S, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
        waterDepth = LoadTextureFromImage(im);
        SetTextureFilter(waterDepth, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(waterDepth, TEXTURE_WRAP_CLAMP);
    }
    {
        MeshBuilder mb;
        V3 n{0, 0, 1};
        Col8 c{255, 255, 255, 255};
        int a = mb.vert({-1, -1, 0}, n, 0, 0, c), b = mb.vert({1, -1, 0}, n, 1, 0, c), cc = mb.vert({1, 1, 0}, n, 1, 1, c), d = mb.vert({-1, 1, 0}, n, 0, 1, c);
        mb.tri(a, b, cc);
        mb.tri(a, cc, d);
        sky_ = mb.upload();
    }
    {
        MeshBuilder mb;
        const int G = 24;
        const float R = 7000;
        for (int j = 0; j <= G; j++)
            for (int i = 0; i <= G; i++) mb.vert({-R + 2 * R * i / G, World::WATER, -R + 2 * R * j / G}, {0, 1, 0}, 0, 0, Col8{255, 255, 255, 255});
        for (int j = 0; j < G; j++)
            for (int i = 0; i < G; i++) {
                int v00 = j * (G + 1) + i, v10 = v00 + 1, v01 = v00 + G + 1, v11 = v01 + 1;
                mb.tri(v00, v01, v10);
                mb.tri(v11, v10, v01);
            }
        water_ = mb.upload();
    }
    {
        MeshBuilder mb;
        Xf id;
        mb.box(id, {-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, Col8{255, 255, 255, 255}, true);
        cube_ = mb.upload();
    }
    plane_ = planeMesh();
    balloon_ = balloonMesh();
    if (shadows) buildShadow();
}

void Renderer::setQuality(int q, bool sh2) {
    quality = q;
    shadows = sh2;
    if (shadows) buildShadow();
    else freeShadow();
}

void Renderer::unload() {
    freeShadow();
    for (int l = 0; l < 2; l++)
        for (auto& s : terr_[l]) UnloadMesh(s.m);
    for (auto& s : outer_) UnloadMesh(s.m);
    for (auto& v : roads_)
        for (auto& s : v) UnloadMesh(s.m);
    for (auto& s : statics_) UnloadMesh(s.m);
    for (auto& c : far_)
        for (auto& s : c) UnloadMesh(s.m);
    for (int k = 0; k < PK_COUNT; k++)
        for (int v = 0; v < PROP_VARS; v++)
            if (propOk_[k][v]) UnloadMesh(prop_[k][v]);
    for (Mesh* m : {&rotor_, &cube_, &sky_, &water_, &plane_, &balloon_}) UnloadMesh(*m);
    for (auto& kv : cars_) {
        UnloadMesh(kv.second.body);
        UnloadMesh(kv.second.wheel);
    }
    cars_.clear();
    Material* mats[] = {&mTerrain_, &mRoad_[0], &mRoad_[1], &mRoad_[2], &mRoad_[3], &mRoad_[4], &mObj_, &mInst_, &mCar_, &mWater_, &mSky_, &mDepth_, &mDepthInst_};
    for (Material* m : mats) {
        RL_FREE(m->maps);
        m->maps = nullptr;
    }
    UnloadTexture(waterDepth);
    tex.unload();
    sh.unload();
}

const CarModel& Renderer::carModel(const CarSpec* s) {
    auto it = cars_.find(s->id);
    if (it != cars_.end()) return it->second;
    CarModel m = buildCarModel(*s);
    return cars_.emplace(s->id, m).first->second;
}

// ------------------------------------------------------------------ кадр
void Renderer::gatherProps(V3 cp, bool shadowPass, V3 focus, float radius) {
    const World& w = *w_;
    for (int k = 0; k < PK_COUNT; k++)
        for (int v = 0; v < PROP_VARS; v++) inst_[k][v].clear();
    float qk = quality == 0 ? 0.7f : quality == 1 ? 1.0f : 1.45f;
    float nearR = 200 * qk, smallR = 230 * qk, tallR = 460 * qk, bushR = 150 * qk;
    V3 ref = shadowPass ? focus : cp;
    float maxR = shadowPass ? radius : tallR;
    int i0 = clampf((ref.x - maxR - World::ORIGIN) / PCS, 0, PC - 1), i1 = clampf((ref.x + maxR - World::ORIGIN) / PCS, 0, PC - 1);
    int j0 = clampf((ref.z - maxR - World::ORIGIN) / PCS, 0, PC - 1), j1 = clampf((ref.z + maxR - World::ORIGIN) / PCS, 0, PC - 1);
    for (int j = j0; j <= j1; j++)
        for (int i = i0; i <= i1; i++) {
            int c = j * PC + i;
            if (cellProps_[c].empty()) continue;
            if (shadowPass) {
                if (cellMax_[c].x < focus.x - radius || cellMin_[c].x > focus.x + radius || cellMax_[c].z < focus.z - radius || cellMin_[c].z > focus.z + radius) continue;
            } else if (!visible(cellMin_[c], cellMax_[c])) continue;
            float ccx = World::ORIGIN + (i + 0.5f) * PCS, ccz = World::ORIGIN + (j + 0.5f) * PCS;
            bool nearCell = std::sqrt(sq(ccx - cp.x) + sq(ccz - cp.z)) < nearR;
            for (int idx : cellProps_[c]) {
                const Prop& p = w.props[idx];
                if (!p.alive) continue;
                float d2 = dist2xz(p.pos, ref);
                bool tree = p.kind == PK_PINE || p.kind == PK_OAK || p.kind == PK_BIRCH || p.kind == PK_PALM || p.kind == PK_ROCK;
                if (shadowPass) {
                    if (d2 > radius * radius) continue;
                } else if (tree) {
                    if (!nearCell) continue;
                } else {
                    float r = p.kind == PK_BUSH ? bushR : (p.kind == PK_LAMP || p.kind == PK_POLE || p.kind == PK_FLAG) ? tallR : smallR;
                    if (d2 > r * r) continue;
                }
                int v = std::min((int)p.var, propVariants(p.kind) - 1);
                inst_[p.kind][v].push_back(matYaw(p.pos, p.yaw, p.scale));
            }
        }
    for (int idx : turbines_) {
        const Prop& p = w.props[idx];
        if (shadowPass && dist2xz(p.pos, focus) > sq(radius + 60)) continue;
        if (!shadowPass && !visible(p.pos - V3{35, 2, 35}, p.pos + V3{35, 110, 35})) continue;
        inst_[PK_TURBINE][0].push_back(matYaw(p.pos, p.yaw, p.scale));
    }
}

void Renderer::drawProps(const Material& m) {
    for (int k = 0; k < PK_COUNT; k++)
        for (int v = 0; v < PROP_VARS; v++) {
            auto& L = inst_[k][v];
            if (L.empty() || !propOk_[k][v]) continue;
            DrawMeshInstanced(prop_[k][v], m, L.data(), (int)L.size());
            drawCalls++;
            instanceCount += (int)L.size();
        }
}

static void setU(Shader s, const char* n, float v) {
    int l = shaderLoc(s, n);
    if (l >= 0) SetShaderValue(s, l, &v, SHADER_UNIFORM_FLOAT);
}
static void setU3(Shader s, const char* n, V3 v) {
    int l = shaderLoc(s, n);
    float f[3] = {v.x, v.y, v.z};
    if (l >= 0) SetShaderValue(s, l, f, SHADER_UNIFORM_VEC3);
}
static V3 rgbOf(uint32_t c) { return {(c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f}; }

void Renderer::drawCars(const Scene& sc, bool depth) {
    for (const CarDraw& d : sc.cars) {
        if (!d.spec) continue;
        if (depth && !d.shadow) continue;
        if (!depth && !visible(d.pos - V3{3.5f, 3.5f, 3.5f}, d.pos + V3{3.5f, 3.5f, 3.5f})) continue;
        const CarModel& m = carModel(d.spec);
        const Material& mat = depth ? mDepth_ : mCar_;
        if (!depth) {
            setU3(sh.car, "uPaint", rgbOf(d.color));
            setU(sh.car, "uLights", d.lights);
            setU(sh.car, "uBrake", d.brake);
            setU(sh.car, "uReverse", d.reverse);
            setU(sh.car, "uDirt", d.dirt);
        }
        DrawMesh(m.body, mat, matFrom(d.pos, d.rot));
        for (int i = 0; i < 4; i++) {
            V3 wp = d.pos + qrot(d.rot, d.wheelC[i]);
            Q wq = qmul(d.rot, qmul(qaxis({0, 1, 0}, d.steer[i]), qaxis({1, 0, 0}, d.spin[i])));
            DrawMesh(m.wheel, mat, matFrom(wp, wq));
        }
        drawCalls += 5;
    }
}

void Renderer::drawDecor(const Scene& sc, bool depth) {
    const Material& mat = depth ? mDepth_ : mObj_;
    if (sc.plane) DrawMesh(plane_, mat, matFrom(sc.planePos, sc.planeRot, 1.6f));
    if (depth) return;
    // воздушные шары над фестивалем
    for (int i = 0; i < 6; i++) {
        float a = i * TAU / 6 + sc.time * 0.004f;
        float r = 260 + 120 * std::sin(i * 1.7f);
        V3 p{std::cos(a) * r + 60 * std::sin(sc.time * 0.01f + i), 150 + 60 * std::sin(i * 2.3f) + 6 * std::sin(sc.time * 0.2f + i), std::sin(a) * r};
        if (!visible(p - V3{10, 0, 10}, p + V3{10, 32, 10})) continue;
        DrawMesh(balloon_, mObj_, matYaw(p, i * 0.9f + sc.time * 0.02f, 1.0f));
        drawCalls++;
    }
}

void Renderer::drawDebris(const Scene& sc) {
    if (!sc.fx || sc.fx->debris.empty()) return;
    setU(sh.car, "uLights", 0);
    setU(sh.car, "uBrake", 0);
    setU(sh.car, "uReverse", 0);
    setU(sh.car, "uDirt", 0.5f);
    for (const Debris& d : sc.fx->debris) {
        float fade = clamp01(5.0f - d.life);
        V3 s = d.size * fade;
        setU3(sh.car, "uPaint", V3{d.c.r / 255.0f, d.c.g / 255.0f, d.c.b / 255.0f});
        Matrix M = matFrom(d.p, d.r);
        Matrix S = MatrixScale(std::max(s.x, 0.01f), std::max(s.y, 0.01f), std::max(s.z, 0.01f));
        DrawMesh(cube_, mCar_, MatrixMultiply(S, M));
        drawCalls++;
    }
}

void Renderer::choosePointLights(Scene& sc) {
    FrameLight& L = sc.L;
    if (L.night < 0.12f) return;
    V3 cp{sc.cam.position.x, sc.cam.position.y, sc.cam.position.z};
    struct C { float d; V3 p; };
    std::vector<C> cand;
    for (int idx : lamps_) {
        const Prop& p = w_->props[idx];
        if (!p.alive) continue;
        float d = dist2xz(p.pos, cp);
        if (d < 130 * 130) cand.push_back({d, Xf{p.pos, p.yaw, p.scale}.apply({0, 6.7f, 1.77f})});
    }
    std::sort(cand.begin(), cand.end(), [](const C& a, const C& b) { return a.d < b.d; });
    for (const C& c : cand) {
        if (L.pointCount >= 8) break;
        L.pointPos[L.pointCount] = c.p;
        L.pointCol[L.pointCount] = V3{1.0f, 0.74f, 0.45f} * (L.night * 0.55f);
        L.pointCount++;
    }
}

void Renderer::drawGlows(const Scene& sc) {
    V3 cp{sc.cam.position.x, sc.cam.position.y, sc.cam.position.z};
    V3 fwd = norm(V3{sc.cam.target.x, sc.cam.target.y, sc.cam.target.z} - cp);
    V3 right = norm(cross(fwd, V3{0, 1, 0}));
    V3 up = cross(right, fwd);
    float night = sc.L.night;
    bool blink = std::fmod(sc.time, 1.6f) < 0.8f;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(tex.glow.id);
    rlBegin(RL_QUADS);
    auto quad = [&](V3 p, float s, Color c) {
        V3 r = right * s, u = up * s;
        V3 a = p - r - u, b = p + r - u, cc = p + r + u, d = p - r + u;
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(0, 1); rlVertex3f(a.x, a.y, a.z);
        rlTexCoord2f(1, 1); rlVertex3f(b.x, b.y, b.z);
        rlTexCoord2f(1, 0); rlVertex3f(cc.x, cc.y, cc.z);
        rlTexCoord2f(0, 0); rlVertex3f(d.x, d.y, d.z);
    };
    for (const Glow& g : glows_) {
        if (g.prop >= 0 && !w_->props[g.prop].alive) continue;
        float a = g.mode == 0 ? night : g.mode == 1 ? 1.0f : (blink ? std::max(night, 0.45f) : 0.0f);
        if (a < 0.02f) continue;
        float d = len(g.p - cp);
        if (d > g.maxD) continue;
        a *= 1.0f - smoothstep(g.maxD * 0.7f, g.maxD, d);
        Color c = g.c;
        c.a = (unsigned char)(clamp01(a) * 200);
        quad(g.p, g.size * (1.0f + d * 0.004f), c);
    }
    for (const CarDraw& cd : sc.cars) {
        if (!cd.spec) continue;
        float dcam = len(cd.pos - cp);
        if (dcam > 900) continue;
        const CarModel& m = carModel(cd.spec);
        V3 f = qrot(cd.rot, {0, 0, 1});
        float k = 1.0f + dcam * 0.006f;
        if (cd.lights > 0.05f) {
            for (int i = 0; i < 2; i++) {
                V3 p = cd.pos + qrot(cd.rot, m.head[i]);
                float facing = clamp01(dot(f, norm(cp - p)) * 1.2f + 0.2f);
                quad(p, 0.7f * k, Color{255, 240, 220, (unsigned char)(170 * cd.lights * facing)});
            }
        }
        float tb = std::max(cd.lights * 0.45f, cd.brake);
        if (tb > 0.05f) {
            for (int i = 0; i < 2; i++) {
                V3 p = cd.pos + qrot(cd.rot, m.tail[i]);
                float facing = clamp01(-dot(f, norm(cp - p)) * 1.2f + 0.2f);
                quad(p, 0.45f * k * (0.7f + cd.brake * 0.5f), Color{255, 30, 20, (unsigned char)(200 * tb * facing)});
            }
        }
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

// Лента, повёрнутая к камере вокруг своей оси
static void ribbon(V3 a, V3 b, float wa, float wb, V3 cp, Color ca, Color cb) {
    V3 ax = b - a;
    V3 mid = (a + b) * 0.5f;
    V3 side = norm(cross(ax, cp - mid));
    V3 a0 = a - side * wa, a1 = a + side * wa, b0 = b - side * wb, b1 = b + side * wb;
    rlColor4ub(ca.r, ca.g, ca.b, ca.a);
    rlTexCoord2f(0, 0.5f); rlVertex3f(a0.x, a0.y, a0.z);
    rlTexCoord2f(1, 0.5f); rlVertex3f(a1.x, a1.y, a1.z);
    rlColor4ub(cb.r, cb.g, cb.b, cb.a);
    rlTexCoord2f(1, 0.5f); rlVertex3f(b1.x, b1.y, b1.z);
    rlTexCoord2f(0, 0.5f); rlVertex3f(b0.x, b0.y, b0.z);
}

void Renderer::drawBeams(const Scene& sc) {
    if (sc.L.night < 0.1f) return;
    V3 cp{sc.cam.position.x, sc.cam.position.y, sc.cam.position.z};
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(tex.glow.id);
    rlBegin(RL_QUADS);
    for (const Building& b : w_->buildings) {
        if (b.style != BS_LIGHTHOUSE) continue;
        V3 p = b.c + V3{0, b.h + 1.9f, 0};
        for (int k = 0; k < 2; k++) {
            float a = sc.time * 0.9f + k * PI;
            V3 d = norm(V3{std::cos(a), -0.03f, std::sin(a)});
            unsigned char al = (unsigned char)(90 * sc.L.night);
            ribbon(p, p + d * 320.0f, 0.8f, 16.0f, cp, Color{255, 240, 200, al}, Color{255, 240, 200, 0});
        }
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer::drawMarkers(const Scene& sc) {
    if (sc.markers.empty() && sc.gates.empty()) return;
    V3 cp{sc.cam.position.x, sc.cam.position.y, sc.cam.position.z};
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(tex.glow.id);
    rlBegin(RL_QUADS);
    for (const MarkerDraw& m : sc.markers) {
        float d = len(m.p - cp);
        if (d > 2500) continue;
        Color c0 = m.c, c1 = m.c;
        c0.a = 150;
        c1.a = 0;
        float wk = 1.0f + d * 0.002f;
        ribbon(m.p, m.p + V3{0, m.height, 0}, m.radius * 0.5f * wk, m.radius * 0.25f * wk, cp, c0, c1);
        // светящийся круг на земле
        float r = m.radius * 1.6f;
        V3 p = m.p + V3{0, 0.25f, 0};
        Color cg = m.c;
        cg.a = 120;
        rlColor4ub(cg.r, cg.g, cg.b, cg.a);
        rlTexCoord2f(0, 0); rlVertex3f(p.x - r, p.y, p.z - r);
        rlTexCoord2f(0, 1); rlVertex3f(p.x - r, p.y, p.z + r);
        rlTexCoord2f(1, 1); rlVertex3f(p.x + r, p.y, p.z + r);
        rlTexCoord2f(1, 0); rlVertex3f(p.x + r, p.y, p.z - r);
    }
    for (const GateDraw& g : sc.gates) {
        Color c = g.finish ? Color{255, 255, 255, 255} : g.next ? Color{255, 150, 40, 255} : Color{80, 180, 255, 255};
        c.a = g.next ? 190 : 90;
        Color top = c;
        top.a = 0;
        for (float sd : {-1.0f, 1.0f}) {
            V3 b = g.p + g.l * (sd * (g.halfW + 0.8f));
            ribbon(b, b + V3{0, g.next ? 9.0f : 6.0f, 0}, 0.9f, 0.5f, cp, c, top);
        }
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer::drawLine(const Scene& sc) {
    if (sc.line.empty()) return;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (const LinePt& q : sc.line) {
        V3 p = q.p + V3{0, 0.14f, 0};
        V3 t = q.t, l = q.l;
        V3 tip = p + t * 0.55f, tip2 = p;
        for (float sd : {1.0f, -1.0f}) {
            V3 back = p - t * 0.45f + l * (sd * 0.95f), back2 = back - t * 0.5f;
            rlColor4ub(q.c.r, q.c.g, q.c.b, q.c.a);
            rlTexCoord2f(0, 0);
            rlVertex3f(tip.x, tip.y, tip.z);
            rlVertex3f(back.x, back.y, back.z);
            rlVertex3f(back2.x, back2.y, back2.z);
            rlVertex3f(tip2.x, tip2.y, tip2.z);
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer::render(Scene& sc) {
    drawCalls = instanceCount = 0;
    FrameLight& L = sc.L;
    V3 cp{sc.cam.position.x, sc.cam.position.y, sc.cam.position.z};
    V3 ct{sc.cam.target.x, sc.cam.target.y, sc.cam.target.z};
    float nearP = (float)rlGetCullDistanceNear(), farP = (float)rlGetCullDistanceFar();
    float aspect = (float)GetRenderWidth() / std::max(1, GetRenderHeight());
    view_ = MatrixLookAt(sc.cam.position, sc.cam.target, sc.cam.up);
    proj_ = MatrixPerspective(sc.cam.fovy * DEG2RAD, aspect, nearP, farP);
    vp_ = MatrixMultiply(view_, proj_);
    setFrustum(vp_);
    L.camPos = cp;
    L.time = sc.time;
    choosePointLights(sc);

    // --- карта теней
    L.shadowOn = L.shadowOn && shadows && shadowRT_.id != 0;
    if (L.shadowOn) {
        V3 fwd = norm(ct - cp);
        V3 fl = norm(V3{fwd.x, 0, fwd.z});
        V3 focus = cp + fl * (shadowHalf_ * 0.55f);
        V3 sd = L.sunDir;
        if (sd.y < 0.12f) sd = norm(V3{sd.x, 0.12f, sd.z});
        Vector3 eye{focus.x + sd.x * 400, focus.y + sd.y * 400, focus.z + sd.z * 400};
        Matrix lv = MatrixLookAt(eye, Vector3{focus.x, focus.y, focus.z}, std::fabs(sd.y) > 0.98f ? Vector3{0, 0, 1} : Vector3{0, 1, 0});
        float texel = 2 * shadowHalf_ / shadowSize_;
        lv.m12 = std::floor(lv.m12 / texel) * texel;
        lv.m13 = std::floor(lv.m13 / texel) * texel;
        Matrix lp = MatrixOrtho(-shadowHalf_, shadowHalf_, -shadowHalf_, shadowHalf_, 20.0, 820.0);
        L.shadowVP = MatrixMultiply(lv, lp);
        rlActiveTextureSlot(SLOT_SHADOW);
        rlDisableTexture();
        rlActiveTextureSlot(0);
        BeginTextureMode(shadowRT_);
        rlClearScreenBuffers();
        rlSetMatrixProjection(lp);
        rlSetMatrixModelview(lv);
        rlEnableDepthTest();
        rlSetCullFace(RL_CULL_FACE_FRONT);
        float R = shadowHalf_ * 1.45f;
        for (const SMesh& s : statics_) {
            if (s.mx.x < focus.x - R || s.mn.x > focus.x + R || s.mx.z < focus.z - R || s.mn.z > focus.z + R) continue;
            DrawMesh(s.m, mDepth_, MatrixIdentity());
            drawCalls++;
        }
        gatherProps(cp, true, focus, R);
        drawProps(mDepthInst_);
        drawCars(sc, true);
        drawDecor(sc, true);
        rlSetCullFace(RL_CULL_FACE_BACK);
        EndTextureMode();
        L.shadowTex = shadowRT_.depth.id;
    } else L.shadowTex = 0;

    sh.setLight(L);
    Shaders::bind(SLOT_DEPTHMAP, waterDepth.id);
    sh.setSkyMatrix(MatrixInvert(vp_));

    BeginMode3D(sc.cam);
    // небо
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawMesh(sky_, mSky_, MatrixIdentity());
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    drawCalls++;

    // рельеф
    float qk = quality == 0 ? 0.7f : quality == 1 ? 1.0f : 1.5f;
    float lod0 = 520 * qk;
    for (size_t c = 0; c < terr_[0].size(); c++) {
        const SMesh& s = terr_[0][c];
        if (!visible(s.mn, s.mx)) continue;
        float dx = std::max(std::max(s.mn.x - cp.x, cp.x - s.mx.x), 0.0f), dz = std::max(std::max(s.mn.z - cp.z, cp.z - s.mx.z), 0.0f);
        int lod = std::sqrt(dx * dx + dz * dz) < lod0 ? 0 : 1;
        DrawMesh(terr_[lod][c].m, mTerrain_, MatrixIdentity());
        drawCalls++;
    }
    for (const SMesh& s : outer_) {
        if (!visible(s.mn, s.mx)) continue;
        DrawMesh(s.m, mTerrain_, MatrixIdentity());
        drawCalls++;
    }
    // дороги
    for (int t = 0; t < 5; t++)
        for (const SMesh& s : roads_[t]) {
            if (!visible(s.mn, s.mx)) continue;
            DrawMesh(s.m, mRoad_[t], MatrixIdentity());
            drawCalls++;
        }
    // здания, мосты, трамплины
    float statR = 2600;
    for (const SMesh& s : statics_) {
        if (!visible(s.mn, s.mx)) continue;
        if (dist2xz((s.mn + s.mx) * 0.5f, cp) > statR * statR) continue;
        DrawMesh(s.m, mObj_, MatrixIdentity());
        drawCalls++;
    }
    // объекты вблизи (инстансы) и деревья дальнего плана
    gatherProps(cp, false, cp, 0);
    drawProps(mInst_);
    float nearR = 200 * qk, farR = 1350 * qk;
    for (int j = 0; j < PC; j++)
        for (int i = 0; i < PC; i++) {
            int c = j * PC + i;
            if (far_[c].empty()) continue;
            float ccx = World::ORIGIN + (i + 0.5f) * PCS, ccz = World::ORIGIN + (j + 0.5f) * PCS;
            float d = std::sqrt(sq(ccx - cp.x) + sq(ccz - cp.z));
            if (d < nearR || d > farR) continue;
            if (!visible(cellMin_[c], cellMax_[c])) continue;
            for (const SMesh& s : far_[c]) {
                DrawMesh(s.m, mObj_, MatrixIdentity());
                drawCalls++;
            }
        }
    // роторы ветряков
    for (size_t k = 0; k < turbines_.size(); k++) {
        const Prop& p = w_->props[turbines_[k]];
        V3 hub = Xf{p.pos, p.yaw, p.scale}.apply({0, 72.7f, 5.35f});
        if (!visible(hub - V3{34, 34, 34}, hub + V3{34, 34, 34})) continue;
        Q q = qmul(qyaw(p.yaw), qaxis({0, 0, 1}, sc.time * 1.1f + k * 1.7f));
        DrawMesh(rotor_, mObj_, matFrom(hub, q, p.scale));
        drawCalls++;
    }
    drawCars(sc, false);
    drawDecor(sc, false);
    drawDebris(sc);
    // прозрачное и эффекты
    if (sc.fx) sc.fx->drawSkids(L);
    DrawMesh(water_, mWater_, MatrixIdentity());
    drawCalls++;
    drawLine(sc);
    if (sc.fx) sc.fx->drawParticles(sc.cam, tex, L);
    drawMarkers(sc);
    drawGlows(sc);
    drawBeams(sc);
    EndMode3D();
}

}  // namespace cl
