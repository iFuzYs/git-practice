#include "world.h"

#include <chrono>
#include <cstring>

namespace cl {

const char* surfaceName(Surface s) {
    switch (s) {
        case SURF_ASPHALT: return "асфальт";
        case SURF_DIRT: return "грунт";
        case SURF_GRAVEL: return "гравий";
        case SURF_GRASS: return "трава";
        case SURF_SAND: return "песок";
        case SURF_ROCK: return "камень";
        case SURF_WATER: return "вода";
        case SURF_WOOD: return "дерево";
        default: return "?";
    }
}

// ---------------------------------------------------------------- сплайны
static V3 catmull(V3 p0, V3 p1, V3 p2, V3 p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

std::vector<V3> sampleSpline(const std::vector<V3>& c, bool loop, float step) {
    std::vector<V3> dense;
    int n = (int)c.size();
    if (n < 2) return c;
    int segs = loop ? n : n - 1;
    auto P = [&](int i) {
        if (loop) return c[((i % n) + n) % n];
        if (i < 0) return c[0] * 2.0f - c[1];
        if (i >= n) return c[n - 1] * 2.0f - c[n - 2];
        return c[i];
    };
    for (int s = 0; s < segs; s++) {
        float L = distxz(P(s), P(s + 1));
        int sub = std::max(4, (int)(L / 1.0f));
        for (int k = 0; k < sub; k++) dense.push_back(catmull(P(s - 1), P(s), P(s + 1), P(s + 2), (float)k / sub));
    }
    dense.push_back(loop ? c[0] : c[n - 1]);
    // равномерная перевыборка по длине
    std::vector<V3> out;
    out.push_back(dense[0]);
    float acc = 0, next = step;
    for (size_t i = 1; i < dense.size(); i++) {
        float d = distxz(dense[i - 1], dense[i]);
        while (acc + d >= next && d > 1e-6f) {
            float t = (next - acc) / d;
            out.push_back(lerp(dense[i - 1], dense[i], t));
            next += step;
        }
        acc += d;
    }
    if (!loop) {
        if (distxz(out.back(), dense.back()) > step * 0.3f) out.push_back(dense.back());
        else out.back() = dense.back();
    } else if (distxz(out.back(), out.front()) < step * 0.5f) {
        out.pop_back();
    }
    return out;
}

RoadPt Road::at(float s) const {
    if (pts.empty()) return {};
    if (loop) { s = std::fmod(s, length); if (s < 0) s += length; }
    else s = clampf(s, 0, length);
    int i = indexAt(s);
    int j = i + 1;
    if (j >= (int)pts.size()) { if (!loop) return pts.back(); j = 0; }
    float s0 = pts[i].s, s1 = (j == 0) ? length : pts[j].s;
    float t = s1 > s0 ? (s - s0) / (s1 - s0) : 0;
    RoadPt r = pts[i];
    r.p = lerp(pts[i].p, pts[j].p, t);
    r.t = norm(lerp(pts[i].t, pts[j].t, t));
    r.l = norm(lerp(pts[i].l, pts[j].l, t));
    r.s = s;
    return r;
}

int Road::indexAt(float s) const {
    // равномерный шаг 2 м — прямой расчёт индекса с поправкой
    int n = (int)pts.size();
    int i = clampf(s / 2.0f, 0, (float)(n - 1));
    while (i > 0 && pts[i].s > s) i--;
    while (i + 1 < n && pts[i + 1].s <= s) i++;
    return i;
}

bool Ramp::contains(float x, float z, float& h) const {
    float dx = x - c.x, dz = z - c.z;
    float lz = dx * sn + dz * cs, lx = dx * cs - dz * sn;
    if (std::fabs(lx) > hx || std::fabs(lz) > hz) return false;
    float t = (lz + hz) / (2 * hz);
    h = c.y + mixf(h0, h1, t);
    return true;
}

// ---------------------------------------------------------------- рельеф
static float distSeg2(float px, float pz, float ax, float az, float bx, float bz, float& t) {
    float dx = bx - ax, dz = bz - az;
    float l2 = dx * dx + dz * dz;
    t = l2 > 0 ? clamp01(((px - ax) * dx + (pz - az) * dz) / l2) : 0;
    float qx = ax + dx * t - px, qz = az + dz * t - pz;
    return qx * qx + qz * qz;
}

float World::riverDist(float x, float z, float& fordK) const {
    float best = 1e9f;
    for (auto& r : rivers) {
        for (size_t i = 0; i + 1 < r.size(); i++) {
            float t;
            float d = distSeg2(x, z, r[i].x, r[i].z, r[i + 1].x, r[i + 1].z, t);
            if (d < best) best = d;
        }
    }
    fordK = 0;
    for (auto& f : fords) {
        float d = std::sqrt(sq(x - f.x) + sq(z - f.z));
        fordK = std::max(fordK, smoothstep(34.0f, 14.0f, d));
    }
    return std::sqrt(best);
}

// Естественный рельеф с побережьем, горами, озером и рекой
float World::naturalH(float x, float z) const {
    float fk;
    float dr = riverDist(x, z, fk);
    return naturalH2(x, z, dr, fk);
}

float World::naturalH2(float x, float z, float dr, float fk) const {
    const Noise& n = noise_;
    float h = 14.0f + n.fbm(x * 0.0011f + 3.1f, z * 0.0011f - 7.3f, 5) * 44.0f;
    h += n.fbm(x * 0.0045f, z * 0.0045f, 3) * 5.0f;
    // холмы ветряков на северо-востоке
    {
        float t;
        float d = std::sqrt(distSeg2(x, z, 150, -900, 850, -900, t));
        h += smoothstep(430.0f, 110.0f, d) * 42.0f;
    }
    // горный массив на северо-западе
    float r = n.ridged(x * 0.0024f + 40.0f, z * 0.0024f - 13.0f, 5);
    {
        float d = std::sqrt(sq(x + 950) + sq(z + 950));
        float m = smoothstep(830.0f, 90.0f, d);
        h += m * m * (185.0f * r + 70.0f);
    }
    // стены гор по западному и северному краю
    {
        float ew = smoothstep(-1170.0f, -1470.0f, x) * (1.0f - smoothstep(1000.0f, 1260.0f, z));
        float en = smoothstep(-1170.0f, -1470.0f, z);
        float e = std::max(ew, en);
        h += e * (140.0f + 140.0f * r);
    }
    // побережье: восток и юг
    float coastX = 1160.0f + n.fbm(z * 0.002f + 5.0f, 1.7f, 3) * 60.0f + 100.0f * std::exp(-sq((z + 900) / 150.0f));
    float coastZ = 1300.0f + n.fbm(x * 0.002f - 3.0f, 8.2f, 3) * 45.0f;
    float d = std::min(coastX - x, coastZ - z);
    float cliff = std::exp(-(sq(x - 1200) + sq(z + 900)) / sq(130.0f));  // мыс с маяком
    if (d < 0) {
        float sea = mixf(0.35f, -16.0f, clamp01(-d / 220.0f)) + n.perlin(x * 0.01f, z * 0.01f) * 0.6f;
        h = mixf(sea, std::max(sea, h * 0.3f), cliff * clamp01(1.0f + d / 40.0f));
    } else {
        float beach = 0.35f + d * 0.034f;
        float k = smoothstep(70.0f, 380.0f, d);
        k = std::max(k, cliff);
        h = mixf(beach, h, k);
    }
    // озеро Зеркальное
    {
        float dl = std::sqrt(sq(x + 800) + sq(z - 650));
        float rl = 230.0f + 38.0f * n.perlin(x * 0.009f + 2.0f, z * 0.009f);
        if (h > 2.6f) h = mixf(h, 2.6f, smoothstep(rl + 170.0f, rl + 12.0f, dl));
        if (dl < rl + 12.0f) h = std::min(h, mixf(2.6f, -7.0f, smoothstep(rl + 12.0f, rl - 110.0f, dl)));
    }
    // река: долина и русло
    {
        if (dr < 110.0f) {
            float bank = 1.8f;
            if (h > bank) h = mixf(h, bank, smoothstep(110.0f, 16.0f, dr));
            if (dr < 16.0f) {
                float bed = mixf(-3.3f, -0.45f, fk);
                h = std::min(h, mixf(bank, bed, smoothstep(16.0f, 7.0f, dr)));
            }
        }
    }
    return h;
}

// Площадки: фестиваль, город, аэродром
struct Flat { float x0, z0, x1, z1, blend, target; };
static std::vector<Flat> g_flats;

float World::flattenAndCarve(float x, float z, float h) const {
    for (auto& f : g_flats) {
        float dx = std::max({f.x0 - x, 0.0f, x - f.x1});
        float dz = std::max({f.z0 - z, 0.0f, z - f.z1});
        float d = std::sqrt(dx * dx + dz * dz);
        if (d < f.blend) h = mixf(h, f.target, smoothstep(f.blend, 0.0f, d));
    }
    return h;
}

float World::sampleGrid(const std::vector<float>& g, float x, float z) const {
    float fx = (x - ORIGIN) / CELL, fz = (z - ORIGIN) / CELL;
    fx = clampf(fx, 0, N - 1.001f);
    fz = clampf(fz, 0, N - 1.001f);
    int i = (int)fx, j = (int)fz;
    float tx = fx - i, tz = fz - j;
    float a = g[idx(i, j)], b = g[idx(i + 1, j)], c = g[idx(i, j + 1)], d = g[idx(i + 1, j + 1)];
    return mixf(mixf(a, b, tx), mixf(c, d, tx), tz);
}

float World::terrainH(float x, float z) const {
    float fx = (x - ORIGIN) / CELL, fz = (z - ORIGIN) / CELL;
    if (fx < 0 || fz < 0 || fx >= N - 1 || fz >= N - 1) return -16.0f;
    int i = (int)fx, j = (int)fz;
    float tx = fx - i, tz = fz - j;
    // та же триангуляция, что у меша: диагональ (i+1,j)-(i,j+1)
    if (tx + tz <= 1.0f) {
        float h00 = height[idx(i, j)], h10 = height[idx(i + 1, j)], h01 = height[idx(i, j + 1)];
        return h00 + (h10 - h00) * tx + (h01 - h00) * tz;
    }
    float h11 = height[idx(i + 1, j + 1)], h10 = height[idx(i + 1, j)], h01 = height[idx(i, j + 1)];
    return h11 + (h01 - h11) * (1 - tx) + (h10 - h11) * (1 - tz);
}

V3 World::terrainN(float x, float z) const {
    float fx = (x - ORIGIN) / CELL, fz = (z - ORIGIN) / CELL;
    if (fx < 0 || fz < 0 || fx >= N - 1 || fz >= N - 1) return {0, 1, 0};
    int i = (int)fx, j = (int)fz;
    float tx = fx - i, tz = fz - j;
    // нормаль грани треугольника
    if (tx + tz <= 1.0f) {
        float h00 = height[idx(i, j)], h10 = height[idx(i + 1, j)], h01 = height[idx(i, j + 1)];
        return norm(V3{-(h10 - h00), CELL, -(h01 - h00)});
    }
    float h11 = height[idx(i + 1, j + 1)], h10 = height[idx(i + 1, j)], h01 = height[idx(i, j + 1)];
    return norm(V3{-(h11 - h01), CELL, -(h11 - h10)});
}

float World::roadDistAt(float x, float z) const { return sampleGrid(roadDist, x, z); }

Surface World::terrainSurface(float x, float z) const {
    float fx = (x - ORIGIN) / CELL, fz = (z - ORIGIN) / CELL;
    int i = clampf(fx + 0.5f, 0, N - 1), j = clampf(fz + 0.5f, 0, N - 1);
    const uint8_t* t = &tint[idx(i, j) * 4];
    float h = height[idx(i, j)];
    V3 n = normal[idx(i, j)];
    if (h < WATER - 0.25f) return SURF_WATER;
    if (t[3] > 128) return SURF_ASPHALT;
    if (t[1] > 120 || h < 1.2f) return SURF_SAND;
    if (n.y < 0.76f || (h > 230.0f && n.y < 0.9f)) return SURF_ROCK;
    if (t[0] > 150) return SURF_GRAVEL;
    return SURF_GRASS;
}

void World::buildTerrain() {
    raw.assign(N * N, 0);
    // цели выравнивания считаются по естественному рельефу
    g_flats.clear();
    float fest = clampf(naturalH(0, 0), 10.0f, 34.0f);
    float air = clampf(naturalH(620, -560), 6.0f, 36.0f);
    g_flats.push_back({-150, -150, 150, 150, 120, fest});   // фестиваль
    g_flats.push_back({560, 560, 1080, 1080, 90, 4.6f});    // город Порто-Бриз
    g_flats.push_back({150, -610, 1060, -510, 90, air});    // аэродром
    // поле расстояний до реки: штампуем сегменты
    std::vector<float> rd(N * N, 1e9f);
    for (auto& r : rivers)
        for (size_t s = 0; s + 1 < r.size(); s++) {
            V3 a = r[s], b = r[s + 1];
            float pad = 115;
            int i0 = (int)((std::min(a.x, b.x) - pad - ORIGIN) / CELL), i1 = (int)((std::max(a.x, b.x) + pad - ORIGIN) / CELL) + 1;
            int j0 = (int)((std::min(a.z, b.z) - pad - ORIGIN) / CELL), j1 = (int)((std::max(a.z, b.z) + pad - ORIGIN) / CELL) + 1;
            for (int j = std::max(j0, 0); j <= std::min(j1, N - 1); j++)
                for (int i = std::max(i0, 0); i <= std::min(i1, N - 1); i++) {
                    float t;
                    float d2 = distSeg2(vx(i), vx(j), a.x, a.z, b.x, b.z, t);
                    float& cur = rd[idx(i, j)];
                    if (d2 < cur) cur = d2;
                }
        }
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            float x = vx(i), z = vx(j);
            float fk = 0;
            for (auto& f : fords) fk = std::max(fk, smoothstep(34.0f, 14.0f, std::sqrt(sq(x - f.x) + sq(z - f.z))));
            raw[idx(i, j)] = flattenAndCarve(x, z, naturalH2(x, z, std::sqrt(rd[idx(i, j)]), fk));
        }
    // размытый рельеф — основа высот дорог (одинаков для всех дорог, поэтому развязки совпадают)
    blur_ = raw;
    std::vector<float> tmp(N * N);
    const int R = 7;
    for (int pass = 0; pass < 3; pass++) {
        for (int j = 0; j < N; j++) {
            float acc = 0; int cnt = 0;
            for (int i = -R; i <= R; i++) if (i >= 0 && i < N) { acc += blur_[idx(i, j)]; cnt++; }
            for (int i = 0; i < N; i++) {
                tmp[idx(i, j)] = acc / cnt;
                int a = i - R, b = i + R + 1;
                if (a >= 0) { acc -= blur_[idx(a, j)]; cnt--; }
                if (b < N) { acc += blur_[idx(b, j)]; cnt++; }
            }
        }
        for (int i = 0; i < N; i++) {
            float acc = 0; int cnt = 0;
            for (int j = -R; j <= R; j++) if (j >= 0 && j < N) { acc += tmp[idx(i, j)]; cnt++; }
            for (int j = 0; j < N; j++) {
                blur_[idx(i, j)] = acc / cnt;
                int a = j - R, b = j + R + 1;
                if (a >= 0) { acc -= tmp[idx(i, a)]; cnt--; }
                if (b < N) { acc += tmp[idx(i, b)]; cnt++; }
            }
        }
    }
    height = raw;
    tint.assign(N * N * 4, 0);
    roadDist.assign(N * N, 60.0f);
}

// ---------------------------------------------------------------- дороги
void World::addRoad(const std::string& name, RoadType type, const std::vector<V3>& ctrl, bool loop, float smoothM) {
    Road r;
    r.id = (int)roads.size();
    r.name = name;
    r.type = type;
    r.loop = loop;
    r.width = type == ROAD_HIGHWAY ? 15.0f : type == ROAD_MAIN ? 9.5f : type == ROAD_STREET ? 11.0f : type == ROAD_DIRT ? 7.5f : 46.0f;
    std::vector<V3> pts = sampleSpline(ctrl, loop, 2.0f);
    int n = (int)pts.size();
    std::vector<float> h(n), minH(n);
    for (int i = 0; i < n; i++) {
        float x = pts[i].x, z = pts[i].z;
        float rh = sampleGrid(raw, x, z);
        h[i] = std::max(sampleGrid(blur_, x, z), 1.6f);
        minH[i] = rh < 0.25f ? 3.8f : 1.6f;
    }
    // плавные въезды на мосты: ограничение снизу спадает с уклоном 7%
    {
        std::vector<float> m2 = minH;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < n; k++) {
                int i = pass == 0 ? k : n - 1 - k;
                int j = pass == 0 ? i - 1 : i + 1;
                if (j < 0 || j >= n) continue;
                m2[i] = std::max(m2[i], m2[j] - 2.0f * 0.07f);
            }
        minH = m2;
        for (int i = 0; i < n; i++) h[i] = std::max(h[i], minH[i]);
    }
    if (type == ROAD_RUNWAY) {
        float avg = 0;
        for (float v : h) avg += v;
        avg /= n;
        for (float& v : h) v = avg;
    } else {
        int W = std::max(1, (int)(smoothM / 2.0f));
        for (int pass = 0; pass < 3; pass++) {
            std::vector<float> o(n);
            for (int i = 0; i < n; i++) {
                float acc = 0; int cnt = 0;
                for (int k = -W; k <= W; k++) {
                    int q = i + k;
                    if (loop) q = ((q % n) + n) % n;
                    else if (q < 0 || q >= n) continue;
                    acc += h[q]; cnt++;
                }
                o[i] = acc / cnt;
            }
            h = o;
            // мосты не должны касаться воды
            for (int i = 0; i < n; i++) h[i] = std::max(h[i], minH[i]);
        }
        // ограничение уклона: крутые места «растягиваются» на соседей
        float gmax = (type == ROAD_HIGHWAY ? 0.09f : type == ROAD_MAIN ? 0.13f : type == ROAD_STREET ? 0.06f : 0.2f) * 2.0f;
        for (int pass = 0; pass < 6000; pass++) {
            bool changed = false;
            int segs = loop ? n : n - 1;
            for (int i = 0; i < segs; i++) {
                int j = (i + 1) % n;
                float d = h[j] - h[i];
                if (std::fabs(d) > gmax + 0.002f) {
                    float ex = (std::fabs(d) - gmax) * 0.5f * signf(d);
                    h[i] += ex;
                    h[j] -= ex;
                    changed = true;
                }
            }
            for (int i = 0; i < n; i++) h[i] = std::max(h[i], minH[i]);
            if (!changed) break;
        }
    }
    r.pts.resize(n);
    float s = 0;
    for (int i = 0; i < n; i++) {
        RoadPt& p = r.pts[i];
        p.p = {pts[i].x, h[i], pts[i].z};
        if (i > 0) s += len(p.p - r.pts[i - 1].p);
        p.s = s;
    }
    r.length = s + (loop ? len(r.pts[0].p - r.pts[n - 1].p) : 0);
    for (int i = 0; i < n; i++) {
        int a = i - 1, b = i + 1;
        if (loop) { a = (a + n) % n; b = b % n; }
        else { a = std::max(a, 0); b = std::min(b, n - 1); }
        V3 t = norm(r.pts[b].p - r.pts[a].p);
        r.pts[i].t = t;
        r.pts[i].l = norm(cross(V3{0, 1, 0}, V3{t.x, 0, t.z}));
        float rh = sampleGrid(raw, pts[i].x, pts[i].z);
        r.pts[i].bridge = (h[i] - rh > 2.4f && rh < 1.6f) || h[i] - rh > 14.0f;
    }
    roads.push_back(std::move(r));
}

void World::indexRoads() {
    roadGrid_.assign(RGN * RGN, {});
    for (auto& r : roads) {
        int n = (int)r.pts.size();
        int segs = r.loop ? n : n - 1;
        for (int i = 0; i < segs; i++) {
            V3 a = r.pts[i].p, b = r.pts[(i + 1) % n].p;
            float pad = r.halfW() + 1.0f;
            int x0 = (int)((std::min(a.x, b.x) - pad - ORIGIN) / RG), x1 = (int)((std::max(a.x, b.x) + pad - ORIGIN) / RG);
            int z0 = (int)((std::min(a.z, b.z) - pad - ORIGIN) / RG), z1 = (int)((std::max(a.z, b.z) + pad - ORIGIN) / RG);
            for (int gz = std::max(z0, 0); gz <= std::min(z1, RGN - 1); gz++)
                for (int gx = std::max(x0, 0); gx <= std::min(x1, RGN - 1); gx++)
                    roadGrid_[gz * RGN + gx].push_back(((uint32_t)r.id << 16) | (uint32_t)i);
        }
    }
}

bool World::roadAt(float x, float z, RoadHit& out, float margin) const {
    int gx = (int)((x - ORIGIN) / RG), gz = (int)((z - ORIGIN) / RG);
    if (gx < 0 || gz < 0 || gx >= RGN || gz >= RGN) return false;
    float best = 1e9f;
    bool found = false;
    for (uint32_t key : roadGrid_[gz * RGN + gx]) {
        const Road& r = roads[key >> 16];
        int i = key & 0xffff;
        int n = (int)r.pts.size();
        const RoadPt& a = r.pts[i];
        const RoadPt& b = r.pts[(i + 1) % n];
        float t;
        float d2 = distSeg2(x, z, a.p.x, a.p.z, b.p.x, b.p.z, t);
        float hw = r.halfW() + margin;
        if (d2 > hw * hw) continue;
        float d = std::sqrt(d2);
        // приоритет: ближе к центру относительно ширины
        float score = d / hw - (r.type == ROAD_HIGHWAY ? 0.05f : 0.0f);
        if (score < best) {
            best = score;
            found = true;
            out.road = r.id;
            out.idx = i;
            out.t = t;
            V3 c = lerp(a.p, b.p, t);
            V3 lft = norm(lerp(a.l, b.l, t));
            out.lateral = (x - c.x) * lft.x + (z - c.z) * lft.z;
            out.s = mixf(a.s, (i + 1 == n) ? r.length : b.s, t);
            out.h = c.y;
            out.tangent = norm(lerp(a.t, b.t, t));
        }
    }
    return found;
}

bool World::nearestRoad(float x, float z, float maxDist, RoadHit& out) const {
    int rad = (int)std::ceil(maxDist / RG);
    int gx0 = (int)((x - ORIGIN) / RG), gz0 = (int)((z - ORIGIN) / RG);
    float best = maxDist * maxDist;
    bool found = false;
    for (int gz = gz0 - rad; gz <= gz0 + rad; gz++)
        for (int gx = gx0 - rad; gx <= gx0 + rad; gx++) {
            if (gx < 0 || gz < 0 || gx >= RGN || gz >= RGN) continue;
            for (uint32_t key : roadGrid_[gz * RGN + gx]) {
                const Road& r = roads[key >> 16];
                int i = key & 0xffff, n = (int)r.pts.size();
                const RoadPt& a = r.pts[i];
                const RoadPt& b = r.pts[(i + 1) % n];
                float t;
                float d2 = distSeg2(x, z, a.p.x, a.p.z, b.p.x, b.p.z, t);
                if (d2 < best) {
                    best = d2;
                    found = true;
                    out.road = r.id; out.idx = i; out.t = t;
                    V3 c = lerp(a.p, b.p, t);
                    V3 lft = norm(lerp(a.l, b.l, t));
                    out.lateral = (x - c.x) * lft.x + (z - c.z) * lft.z;
                    out.s = mixf(a.s, (i + 1 == n) ? r.length : b.s, t);
                    out.h = c.y;
                    out.tangent = norm(lerp(a.t, b.t, t));
                }
            }
        }
    return found;
}

GroundHit World::ground(float x, float z, float yRef) const {
    GroundHit g;
    g.h = terrainH(x, z);
    g.n = terrainN(x, z);
    g.surf = terrainSurface(x, z);
    RoadHit rh;
    if (roadAt(x, z, rh)) {
        const Road& r = roads[rh.road];
        const RoadPt& p = r.pts[rh.idx];
        bool under = p.bridge && yRef < rh.h - 1.6f;
        if (!under && rh.h > g.h - 1.0f) {
            g.h = rh.h;
            V3 t = rh.tangent;
            V3 l = norm(cross(V3{0, 1, 0}, V3{t.x, 0, t.z}));
            g.n = norm(cross(t, l));
            g.surf = p.bridge && r.type != ROAD_DIRT ? SURF_ASPHALT : r.surface();
            if (p.bridge && r.type == ROAD_DIRT) g.surf = SURF_WOOD;
            g.road = rh.road;
        }
    }
    for (const Ramp& rp : ramps) {
        float h;
        if (rp.contains(x, z, h) && h > g.h && h < yRef + 1.2f) {
            g.h = h;
            V3 fwd{rp.sn, 0, rp.cs};
            float slope = (rp.h1 - rp.h0) / (2 * rp.hz);
            g.n = norm(V3{0, 1, 0} - fwd * slope);
            g.surf = SURF_WOOD;
            g.road = -1;
        }
    }
    if (g.h < WATER) {
        g.water = true;
        g.waterDepth = WATER - g.h;
    }
    return g;
}

void World::carveRoads() {
    // «штампуем» каждый сегмент в сетку: вершина берёт ближайшую дорогу
    std::vector<float> bestK(N * N, 1e9f), target(N * N, 0), wgt(N * N, 0);
    for (auto& r : roads) {
        float hw = r.halfW();
        float blend = r.type == ROAD_HIGHWAY ? 16.0f : r.type == ROAD_RUNWAY ? 24.0f : r.type == ROAD_DIRT ? 7.0f : r.type == ROAD_STREET ? 6.0f : 11.0f;
        int n = (int)r.pts.size();
        int segs = r.loop ? n : n - 1;
        for (int s = 0; s < segs; s++) {
            const RoadPt& a = r.pts[s];
            const RoadPt& b = r.pts[(s + 1) % n];
            float pad = hw + blend + 1;
            int i0 = (int)((std::min(a.p.x, b.p.x) - pad - ORIGIN) / CELL), i1 = (int)((std::max(a.p.x, b.p.x) + pad - ORIGIN) / CELL) + 1;
            int j0 = (int)((std::min(a.p.z, b.p.z) - pad - ORIGIN) / CELL), j1 = (int)((std::max(a.p.z, b.p.z) + pad - ORIGIN) / CELL) + 1;
            for (int j = std::max(j0, 0); j <= std::min(j1, N - 1); j++)
                for (int i = std::max(i0, 0); i <= std::min(i1, N - 1); i++) {
                    float x = vx(i), z = vx(j), t;
                    float d = std::sqrt(distSeg2(x, z, a.p.x, a.p.z, b.p.x, b.p.z, t));
                    float edge = d - hw;
                    int k = idx(i, j);
                    if (edge < roadDist[k]) roadDist[k] = std::max(edge, 0.0f);
                    if (edge > blend) continue;
                    float hr = mixf(a.p.y, b.p.y, t);
                    bool bridge = (t < 0.5f ? a.bridge : b.bridge);
                    if (bridge) continue;
                    float kk = edge / (hw + blend);
                    if (kk < bestK[k]) {
                        bestK[k] = kk;
                        target[k] = hr - 0.12f;
                        wgt[k] = edge <= 0.6f ? 1.0f : 1.0f - smoothstep(0.6f, blend, edge);
                    }
                }
        }
    }
    for (int k = 0; k < N * N; k++) {
        if (wgt[k] > 0) height[k] = mixf(height[k], target[k], wgt[k]);
        float rd = roadDist[k];
        // обочина: гравий у края полотна
        float sh = 1.0f - smoothstep(0.5f, 4.5f, rd);
        tint[k * 4 + 0] = (uint8_t)(clamp01(sh) * 255);
    }
}

// Колея кросс-кантри: рельеф вдоль троп выравнивается с ограничением уклона
void World::carveTrails() {
    trackTint_.assign(N * N, 0);
    for (auto& t : trails) {
        int n = (int)t.pts.size();
        if (n < 3) continue;
        std::vector<float> h(n);
        std::vector<char> fixedH(n, 0);
        for (int i = 0; i < n; i++) h[i] = std::max(sampleGrid(blur_, t.pts[i].x, t.pts[i].z), 0.3f);
        // броды оставляем на уровне воды
        for (int i = 0; i < n; i++) {
            float fk = 0;
            for (auto& f : fords) fk = std::max(fk, smoothstep(40.0f, 18.0f, std::sqrt(sq(t.pts[i].x - f.x) + sq(t.pts[i].z - f.z))));
            if (fk > 0) h[i] = mixf(h[i], -0.35f, fk);
        }
        for (int pass = 0; pass < 2; pass++) {
            std::vector<float> o(n);
            for (int i = 0; i < n; i++) {
                float acc = 0; int cnt = 0;
                for (int k = -3; k <= 3; k++) { int q = i + k; if (q < 0 || q >= n) continue; acc += h[q]; cnt++; }
                o[i] = acc / cnt;
            }
            h = o;
        }
        // пересечения с дорогами: колея выходит точно на полотно
        for (int i = 0; i < n; i++) {
            RoadHit rh;
            if (roadAt(t.pts[i].x, t.pts[i].z, rh, 4.0f)) { h[i] = rh.h - 0.1f; fixedH[i] = 1; }
        }
        float gmax = 0.23f * 4.0f;  // шаг точек 4 м
        for (int pass = 0; pass < 8000; pass++) {
            bool ch = false;
            for (int i = 0; i + 1 < n; i++) {
                float d = h[i + 1] - h[i];
                if (std::fabs(d) > gmax + 0.004f) {
                    float ex = (std::fabs(d) - gmax) * signf(d);
                    if (fixedH[i] && fixedH[i + 1]) continue;
                    if (fixedH[i]) h[i + 1] -= ex;
                    else if (fixedH[i + 1]) h[i] += ex;
                    else { h[i] += ex * 0.5f; h[i + 1] -= ex * 0.5f; }
                    ch = true;
                }
            }
            if (!ch) break;
        }
        for (int i = 0; i < n; i++) t.pts[i].y = h[i];
        // штампуем колею в сетку
        for (int s2 = 0; s2 + 1 < n; s2++) {
            V3 a = t.pts[s2], b = t.pts[s2 + 1];
            float pad = 15;
            int i0 = (int)((std::min(a.x, b.x) - pad - ORIGIN) / CELL), i1 = (int)((std::max(a.x, b.x) + pad - ORIGIN) / CELL) + 1;
            int j0 = (int)((std::min(a.z, b.z) - pad - ORIGIN) / CELL), j1 = (int)((std::max(a.z, b.z) + pad - ORIGIN) / CELL) + 1;
            for (int j = std::max(j0, 0); j <= std::min(j1, N - 1); j++)
                for (int i = std::max(i0, 0); i <= std::min(i1, N - 1); i++) {
                    float tt;
                    float d = std::sqrt(distSeg2(vx(i), vx(j), a.x, a.z, b.x, b.z, tt));
                    if (d > 14) continue;
                    int k = idx(i, j);
                    if (roadDist[k] < 3.0f) continue;  // дороги не трогаем
                    float wgt = d < 5.0f ? 1.0f : 1.0f - smoothstep(5.0f, 14.0f, d);
                    float target = mixf(a.y, b.y, tt);
                    float cur = height[k];
                    float nh = mixf(cur, target, wgt);
                    // из нескольких сегментов берём самое сильное влияние
                    if (wgt * 255 > trackTint_[k]) {
                        height[k] = nh;
                        trackTint_[k] = (uint8_t)(wgt * 255);
                    }
                }
        }
    }
}

// Каналы подкраски рельефа: обочина (уже есть), песок, лесная подстилка, мощение
void World::computeTint() {
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            int k = idx(i, j);
            float x = vx(i), z = vx(j), h = height[k];
            float sand = 1.0f - smoothstep(1.0f, 2.4f, h);
            float coastD = std::min(1160.0f - x, 1300.0f - z);
            if (coastD < 120) sand = std::max(sand, 1.0f - smoothstep(2.6f, 4.2f, h));
            float forest = smoothstep(-0.05f, 0.25f, noise_.fbm(x * 0.0032f + 8.0f, z * 0.0032f - 2.0f, 4));
            // мощёные площадки: фестиваль, город, перрон аэродрома
            float paved = 0;
            float fd = std::sqrt(x * x + z * z);
            paved = std::max(paved, 1.0f - smoothstep(136.0f, 140.0f, fd));
            if (x > 562 && x < 1074 && z > 562 && z < 1084) paved = 1;
            if (x > 176 && x < 1044 && z > -690 && z < -583) paved = 1;
            if (paved > 0.5f) { sand = 0; forest = 0; }
            // колея кросс-кантри — укатанный грунт
            if (!trackTint_.empty() && trackTint_[k] > 0) {
                float tk = trackTint_[k] / 255.0f;
                tint[k * 4 + 0] = (uint8_t)std::max<float>(tint[k * 4 + 0], smoothstep(0.55f, 0.95f, tk) * 190.0f);
                forest *= 1.0f - tk;
            }
            tint[k * 4 + 1] = (uint8_t)(clamp01(sand) * 255);
            tint[k * 4 + 2] = (uint8_t)(clamp01(forest) * 255);
            tint[k * 4 + 3] = (uint8_t)(clamp01(paved) * 255);
        }
}

void World::computeNormals() {
    normal.assign(N * N, V3{0, 1, 0});
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            float hl = height[idx(std::max(i - 1, 0), j)], hr = height[idx(std::min(i + 1, N - 1), j)];
            float hd = height[idx(i, std::max(j - 1, 0))], hu = height[idx(i, std::min(j + 1, N - 1))];
            normal[idx(i, j)] = norm(V3{hl - hr, 2 * CELL, hd - hu});
        }
}

void World::buildRoads() {
    roads.clear();
    // Прибрежное шоссе — большое кольцо
    addRoad("Прибрежное шоссе", ROAD_HIGHWAY,
            {{980, 0, -1150}, {1070, 0, -860}, {1085, 0, -500}, {1080, 0, -120}, {1065, 0, 260}, {1000, 0, 500}, {780, 0, 555}, {575, 0, 640},
             {545, 0, 900}, {470, 0, 1150}, {100, 0, 1225}, {-350, 0, 1215}, {-780, 0, 1110}, {-1100, 0, 880}, {-1215, 0, 520}, {-1215, 0, 120},
             {-1120, 0, -260}, {-860, 0, -470}, {-500, 0, -620}, {-160, 0, -880}, {250, 0, -1070}, {640, 0, -1175}},
            true, 60);
    addRoad("Северная дорога", ROAD_MAIN, {{0, 0, -60}, {60, 0, -320}, {20, 0, -600}, {-160, 0, -880}}, false, 36);
    addRoad("Восточная дорога", ROAD_MAIN, {{60, 0, 0}, {320, 0, -70}, {620, 0, -190}, {880, 0, -150}, {1080, 0, -120}}, false, 36);
    addRoad("Южная дорога", ROAD_MAIN, {{0, 0, 60}, {-40, 0, 300}, {-20, 0, 540}, {60, 0, 760}, {110, 0, 1000}, {100, 0, 1225}}, false, 36);
    addRoad("Западная дорога", ROAD_MAIN, {{-60, 0, 0}, {-360, 0, 50}, {-720, 0, -30}, {-1000, 0, 40}, {-1215, 0, 120}}, false, 36);
    {
        // u — к вершине (на северо-запад), v — поперёк склона
        const float k2 = 0.70710678f;
        auto UV = [&](float u, float v) { return V3{(-u + v) * k2, 0, (-u - v) * k2}; };
        std::vector<V3> c;
        c.push_back({-500, 0, -620});
        float u0 = 800;
        const int legs = 8;
        for (int k = 0; k < legs; k++) {
            float dir = (k % 2 == 0) ? -1.0f : 1.0f;
            c.push_back(UV(u0 + k * 66 + 12, -dir * 95));
            c.push_back(UV(u0 + k * 66 + 54, dir * 95));
            if (k + 1 < legs) c.push_back(UV(u0 + k * 66 + 66, dir * 124));
        }
        c.push_back(UV(u0 + legs * 66 + 4, 0));
        addRoad("Серпантин Орлиного пика", ROAD_MAIN, c, false, 36);
    }
    addRoad("Аэродромная", ROAD_MAIN, {{620, 0, -190}, {625, 0, -380}, {620, 0, -536}}, false, 30);
    addRoad("Взлётная полоса", ROAD_RUNWAY, {{180, 0, -560}, {1040, 0, -560}}, false, 0);
    addRoad("Маячная", ROAD_MAIN, {{1070, 0, -860}, {1140, 0, -890}, {1200, 0, -905}}, false, 20);
    // городская сетка Порто-Бриз
    const char* hnames[] = {"ул. Гаванская", "ул. Солнечная", "ул. Морская", "ул. Пальмовая"};
    float hz[] = {660, 780, 900, 1020};
    for (int k = 0; k < 4; k++) addRoad(hnames[k], ROAD_STREET, {{560, 0, hz[k]}, {810, 0, hz[k]}, {1060, 0, hz[k]}}, false, 12);
    const char* vnames[] = {"пр. Маяковый", "пр. Рыбацкий", "пр. Портовый", "Набережная"};
    float vxs[] = {640, 760, 880, 1060};
    float vz0[] = {605, 565, 540, 520};
    for (int k = 0; k < 4; k++) addRoad(vnames[k], ROAD_STREET, {{vxs[k], 0, vz0[k]}, {vxs[k], 0, 800}, {vxs[k], 0, 1060}}, false, 12);
    // грунтовки
    addRoad("Лесная тропа", ROAD_DIRT,
            {{-200, 0, -110}, {-330, 0, -290}, {-560, 0, -390}, {-790, 0, -300}, {-860, 0, -150}, {-700, 0, -105}, {-480, 0, -170}, {-300, 0, -60}},
            true, 10);
    addRoad("Лесной съезд", ROAD_DIRT, {{-300, 0, -60}, {-330, 0, -5}, {-345, 0, 48}}, false, 8);
    addRoad("Фермерская грунтовка", ROAD_DIRT, {{60, 0, 760}, {-180, 0, 840}, {-430, 0, 920}, {-690, 0, 1010}, {-940, 0, 995}}, false, 10);
    addRoad("Дюны", ROAD_DIRT, {{1085, 0, -500}, {1122, 0, -400}, {1130, 0, -150}, {1124, 0, 60}, {1070, 0, 150}}, false, 8);
    addRoad("Горная грунтовка", ROAD_DIRT, {{-790, 0, -300}, {-920, 0, -420}, {-1040, 0, -560}, {-1105, 0, -690}, {-1150, 0, -780}}, false, 10);
    indexRoads();
}

void World::generate(uint32_t sd) {
    auto t0 = std::chrono::steady_clock::now();
    seed = sd;
    noise_ = Noise(sd);
    rivers.clear();
    fords.clear();
    rivers.push_back({{-560, 0, 620}, {-420, 0, 540}, {-300, 0, 470}, {-120, 0, 440}, {0, 0, 420}, {170, 0, 395}, {320, 0, 380}, {470, 0, 350},
                      {620, 0, 330}, {780, 0, 300}, {900, 0, 280}, {1060, 0, 262}, {1250, 0, 250}});
    // сглаживаем реку сплайном
    rivers[0] = sampleSpline(rivers[0], false, 8.0f);
    fords.push_back({-360, 0, 505});
    fords.push_back({420, 0, 360});
    buildTerrain();
    buildRoads();
    carveRoads();
    defineTrails();
    carveTrails();
    computeNormals();
    computeTint();
    placeProps();
    auto t1 = std::chrono::steady_clock::now();
    genMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

const Place* World::place(const std::string& id) const {
    for (auto& p : places)
        if (p.id == id) return &p;
    return nullptr;
}

void World::addCol(const Collider& c0) {
    Collider c = c0;
    c.cs = std::cos(c.yaw);
    c.sn = std::sin(c.yaw);
    int id = (int)cols.size();
    cols.push_back(c);
    float ext = c.shape == COL_BOX ? std::sqrt(c.hx * c.hx + c.hz * c.hz) : c.r;
    int x0 = (int)((c.c.x - ext - ORIGIN) / CG), x1 = (int)((c.c.x + ext - ORIGIN) / CG);
    int z0 = (int)((c.c.z - ext - ORIGIN) / CG), z1 = (int)((c.c.z + ext - ORIGIN) / CG);
    for (int gz = std::max(z0, 0); gz <= std::min(z1, CGN - 1); gz++)
        for (int gx = std::max(x0, 0); gx <= std::min(x1, CGN - 1); gx++) colGrid_[gz * CGN + gx].push_back(id);
}

void World::queryCols(float x, float z, float r, std::vector<int>& out) const {
    out.clear();
    int x0 = (int)((x - r - ORIGIN) / CG), x1 = (int)((x + r - ORIGIN) / CG);
    int z0 = (int)((z - r - ORIGIN) / CG), z1 = (int)((z + r - ORIGIN) / CG);
    for (int gz = std::max(z0, 0); gz <= std::min(z1, CGN - 1); gz++)
        for (int gx = std::max(x0, 0); gx <= std::min(x1, CGN - 1); gx++)
            for (int id : colGrid_[gz * CGN + gx]) {
                if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
            }
}

}  // namespace cl
