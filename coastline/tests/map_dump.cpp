// Отладочный инструмент: генерирует мир и сохраняет карту сверху в PNG
#include <cstdio>
#include <vector>

#include "../src/world/world.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace cl;

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "map.png";
    int S = argc > 2 ? atoi(argv[2]) : 1024;
    World w;
    w.generate(2026);
    printf("gen %.0f ms, roads %zu, props %zu, trees %d, cols %zu, buildings %zu, boards %zu\n", w.genMs, w.roads.size(), w.props.size(), w.treeCount,
           w.cols.size(), w.buildings.size(), w.boards.size());
    std::vector<unsigned char> img(S * S * 3);
    float span = 3072.0f;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float wx = World::ORIGIN + (x + 0.5f) / S * span, wz = World::ORIGIN + (y + 0.5f) / S * span;
            float h = w.terrainH(wx, wz);
            V3 n = w.terrainN(wx, wz);
            float shade = clamp01(0.55f + dot(n, norm(V3{-0.5f, 0.8f, -0.4f})) * 0.6f);
            float r, g, b;
            if (h < 0) { float d = clamp01(-h / 12); r = mixf(0.25f, 0.05f, d); g = mixf(0.62f, 0.22f, d); b = mixf(0.7f, 0.42f, d); shade = 1; }
            else {
                Surface s = w.terrainSurface(wx, wz);
                if (s == SURF_SAND) { r = 0.9f; g = 0.82f; b = 0.6f; }
                else if (s == SURF_ROCK) { r = 0.55f; g = 0.52f; b = 0.5f; }
                else if (s == SURF_GRAVEL) { r = 0.6f; g = 0.55f; b = 0.45f; }
                else { float t = clamp01(h / 250); r = mixf(0.35f, 0.5f, t); g = mixf(0.6f, 0.5f, t); b = mixf(0.28f, 0.4f, t); }
                if (h > 260) { r = g = b = 0.9f; }
            }
            RoadHit rh;
            if (w.roadAt(wx, wz, rh)) {
                const Road& rd = w.roads[rh.road];
                bool br = rd.pts[rh.idx].bridge;
                if (rd.type == ROAD_DIRT) { r = 0.62f; g = 0.45f; b = 0.3f; }
                else if (rd.type == ROAD_HIGHWAY) { r = 0.95f; g = 0.75f; b = 0.25f; }
                else { r = 0.92f; g = 0.92f; b = 0.9f; }
                if (br) { r = 1.0f; g = 0.3f; b = 0.3f; }
                shade = 1;
            }
            unsigned char* p = &img[(y * S + x) * 3];
            p[0] = (unsigned char)(clamp01(r * shade) * 255);
            p[1] = (unsigned char)(clamp01(g * shade) * 255);
            p[2] = (unsigned char)(clamp01(b * shade) * 255);
        }
    auto dot2 = [&](float wx, float wz, int rad, unsigned char R, unsigned char Gc, unsigned char B) {
        int cx = (int)((wx - World::ORIGIN) / span * S), cy = (int)((wz - World::ORIGIN) / span * S);
        for (int dy = -rad; dy <= rad; dy++)
            for (int dx = -rad; dx <= rad; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= S || y >= S || dx * dx + dy * dy > rad * rad) continue;
                unsigned char* p = &img[(y * S + x) * 3];
                p[0] = R; p[1] = Gc; p[2] = B;
            }
    };
    for (auto& pr : w.props) {
        if (pr.kind <= PK_BIRCH) dot2(pr.pos.x, pr.pos.z, 0, 20, 70, 25);
        else if (pr.kind == PK_PALM) dot2(pr.pos.x, pr.pos.z, 0, 40, 120, 40);
    }
    for (auto& b : w.buildings) dot2(b.c.x, b.c.z, 2, 120, 60, 160);
    for (int b : w.boards) dot2(w.props[b].pos.x, w.props[b].pos.z, 4, 255, 120, 0);
    for (auto& rp : w.ramps) dot2(rp.c.x, rp.c.z, 5, 255, 0, 255);
    for (auto& t : w.trails)
        for (size_t i = 0; i < t.pts.size(); i += 3) dot2(t.pts[i].x, t.pts[i].z, 1, 0, 200, 255);
    for (auto& p : w.places) dot2(p.p.x, p.p.z, 6, 255, 255, 0);
    stbi_write_png(out, S, S, 3, img.data(), S * 3);
    // сводка по высотам мест
    for (auto& p : w.places) printf("place %-10s %7.1f %6.1f %7.1f\n", p.id.c_str(), p.p.x, p.p.y, p.p.z);
    for (auto& r : w.roads) {
        int br = 0; float mn = 1e9f, mx = -1e9f, maxGrade = 0;
        for (size_t i = 0; i < r.pts.size(); i++) {
            br += r.pts[i].bridge;
            mn = std::min(mn, r.pts[i].p.y); mx = std::max(mx, r.pts[i].p.y);
            if (i) maxGrade = std::max(maxGrade, std::fabs(r.pts[i].p.y - r.pts[i - 1].p.y) / 2.0f);
        }
        printf("road %-28s len %6.0f  h %5.1f..%5.1f  grade %4.1f%%  bridge %d\n", r.name.c_str(), r.length, mn, mx, maxGrade * 100, br);
    }
    return 0;
}
