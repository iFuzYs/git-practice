#include "carmodel.h"

#include <vector>

#include "meshgen.h"

namespace cl {

namespace {

enum Mat { M_PAINT = 0, M_GLASS = 1, M_TRIM = 2, M_CHROME = 3, M_HEAD = 4, M_TAIL = 5, M_RUBBER = 6, M_REV = 7, M_UNDER = 8 };

struct Key {
    float t;      // 0 — зад, 1 — перёд
    float belt;   // высота линии плеча (доля высоты)
    float top;    // верх (крыша) или = belt
    float w;      // полуширина на плече (доля полуширины)
    float tw;     // полуширина крыши
    bool cabin;
};

// Профили кузовов по типам
std::vector<Key> profileFor(BodyStyle b) {
    switch (b) {
        case BODY_HATCH:
            return {{0.0f, 0.55f, 0.55f, 0.88f, 0.8f, false}, {0.03f, 0.62f, 0.78f, 0.97f, 0.75f, true}, {0.1f, 0.63f, 0.97f, 1.0f, 0.74f, true},
                    {0.5f, 0.62f, 1.0f, 1.0f, 0.74f, true}, {0.62f, 0.6f, 0.93f, 1.0f, 0.74f, true}, {0.74f, 0.58f, 0.62f, 1.0f, 0.75f, true},
                    {0.78f, 0.57f, 0.57f, 1.0f, 0.8f, false}, {0.95f, 0.5f, 0.5f, 0.96f, 0.8f, false}, {1.0f, 0.4f, 0.4f, 0.84f, 0.8f, false}};
        case BODY_WAGON:
            return {{0.0f, 0.56f, 0.56f, 0.88f, 0.8f, false}, {0.03f, 0.62f, 0.9f, 0.97f, 0.76f, true}, {0.08f, 0.62f, 0.98f, 1.0f, 0.76f, true},
                    {0.55f, 0.62f, 1.0f, 1.0f, 0.76f, true}, {0.64f, 0.6f, 0.93f, 1.0f, 0.76f, true}, {0.74f, 0.58f, 0.62f, 1.0f, 0.78f, true},
                    {0.78f, 0.57f, 0.57f, 1.0f, 0.8f, false}, {0.96f, 0.5f, 0.5f, 0.96f, 0.8f, false}, {1.0f, 0.42f, 0.42f, 0.86f, 0.8f, false}};
        case BODY_MUSCLE:
        case BODY_CLASSIC:
            return {{0.0f, 0.5f, 0.5f, 0.9f, 0.8f, false}, {0.03f, 0.62f, 0.62f, 0.98f, 0.8f, false}, {0.2f, 0.64f, 0.64f, 1.0f, 0.8f, false},
                    {0.26f, 0.64f, 0.8f, 1.0f, 0.72f, true}, {0.33f, 0.63f, 1.0f, 1.0f, 0.72f, true}, {0.52f, 0.62f, 1.0f, 1.0f, 0.72f, true},
                    {0.6f, 0.62f, 0.68f, 1.0f, 0.76f, true}, {0.63f, 0.62f, 0.62f, 1.0f, 0.8f, false}, {0.94f, 0.6f, 0.6f, 1.0f, 0.8f, false},
                    {1.0f, 0.5f, 0.5f, 0.94f, 0.8f, false}};
        case BODY_SUPER:
        case BODY_HYPER:
        case BODY_TRACK:
            return {{0.0f, 0.55f, 0.55f, 0.9f, 0.7f, false}, {0.04f, 0.66f, 0.66f, 1.0f, 0.7f, false}, {0.22f, 0.68f, 0.72f, 1.0f, 0.6f, true},
                    {0.36f, 0.66f, 1.0f, 1.0f, 0.58f, true}, {0.5f, 0.64f, 1.0f, 1.0f, 0.58f, true}, {0.66f, 0.6f, 0.62f, 0.98f, 0.62f, true},
                    {0.7f, 0.56f, 0.56f, 0.98f, 0.7f, false}, {0.92f, 0.42f, 0.42f, 0.96f, 0.7f, false}, {1.0f, 0.3f, 0.3f, 0.82f, 0.7f, false}};
        case BODY_ROADSTER:
            return {{0.0f, 0.55f, 0.55f, 0.88f, 0.8f, false}, {0.04f, 0.63f, 0.63f, 0.98f, 0.8f, false}, {0.3f, 0.66f, 0.66f, 1.0f, 0.8f, false},
                    {0.52f, 0.64f, 0.64f, 1.0f, 0.8f, false}, {0.6f, 0.63f, 0.95f, 1.0f, 0.78f, true}, {0.64f, 0.62f, 0.66f, 1.0f, 0.8f, true},
                    {0.67f, 0.6f, 0.6f, 1.0f, 0.8f, false}, {0.94f, 0.5f, 0.5f, 0.96f, 0.8f, false}, {1.0f, 0.4f, 0.4f, 0.84f, 0.8f, false}};
        case BODY_TRUCK:
            return {{0.0f, 0.55f, 0.55f, 0.96f, 0.9f, false}, {0.02f, 0.58f, 0.58f, 1.0f, 0.9f, false}, {0.42f, 0.58f, 0.58f, 1.0f, 0.9f, false},
                    {0.44f, 0.58f, 1.0f, 1.0f, 0.86f, true}, {0.62f, 0.58f, 1.0f, 1.0f, 0.86f, true}, {0.72f, 0.58f, 0.68f, 1.0f, 0.88f, true},
                    {0.74f, 0.58f, 0.6f, 1.0f, 0.9f, false}, {0.97f, 0.56f, 0.56f, 1.0f, 0.9f, false}, {1.0f, 0.5f, 0.5f, 0.96f, 0.9f, false}};
        case BODY_BUGGY:
            return {{0.0f, 0.45f, 0.45f, 0.7f, 0.6f, false}, {0.1f, 0.5f, 0.5f, 0.78f, 0.6f, false}, {0.7f, 0.46f, 0.46f, 0.7f, 0.6f, false},
                    {0.9f, 0.4f, 0.4f, 0.6f, 0.6f, false}, {1.0f, 0.34f, 0.34f, 0.5f, 0.5f, false}};
        case BODY_EV:
        case BODY_COUPE:
        case BODY_JDM:
        case BODY_RALLY:
        default:
            return {{0.0f, 0.54f, 0.54f, 0.88f, 0.8f, false}, {0.03f, 0.62f, 0.62f, 0.97f, 0.8f, false}, {0.13f, 0.66f, 0.66f, 1.0f, 0.8f, false},
                    {0.24f, 0.66f, 0.75f, 1.0f, 0.72f, true}, {0.32f, 0.65f, 0.97f, 1.0f, 0.7f, true}, {0.52f, 0.63f, 1.0f, 1.0f, 0.7f, true},
                    {0.66f, 0.61f, 0.66f, 1.0f, 0.74f, true}, {0.7f, 0.6f, 0.6f, 1.0f, 0.8f, false}, {0.93f, 0.52f, 0.52f, 0.98f, 0.8f, false},
                    {1.0f, 0.4f, 0.4f, 0.84f, 0.8f, false}};
    }
}

Key interp(const std::vector<Key>& k, float t) {
    if (t <= k.front().t) return k.front();
    for (size_t i = 0; i + 1 < k.size(); i++) {
        if (t <= k[i + 1].t) {
            float u = (t - k[i].t) / (k[i + 1].t - k[i].t);
            Key r;
            r.t = t;
            r.belt = mixf(k[i].belt, k[i + 1].belt, u);
            r.top = mixf(k[i].top, k[i + 1].top, u);
            r.w = mixf(k[i].w, k[i + 1].w, u);
            r.tw = mixf(k[i].tw, k[i + 1].tw, u);
            r.cabin = k[i].cabin && k[i + 1].cabin;
            return r;
        }
    }
    return k.back();
}

struct Section {
    float z;
    float bottom, belt, top, hw, thw;
    bool cabin;
    V3 p[8];  // одна сторона (x > 0): P0 центр низа ... P7 центр верха
};

void matVert(MeshBuilder& mb, V3 a, V3 b, V3 c, int mat, Col8 col) {
    V3 n = norm(cross(b - a, c - a));
    float u = (float)mat;
    int i0 = mb.vert(a, n, u, 0, col), i1 = mb.vert(b, n, u, 0, col), i2 = mb.vert(c, n, u, 0, col);
    mb.tri(i0, i1, i2);
}
void matQuad(MeshBuilder& mb, V3 a, V3 b, V3 c, V3 d, int mat, Col8 col = {40, 40, 44, 255}) {
    matVert(mb, a, b, c, mat, col);
    matVert(mb, a, c, d, mat, col);
}
// коробка с одним материалом (в системе машины)
void matBox(MeshBuilder& mb, V3 mn, V3 mx, int mat, Col8 col = {40, 40, 44, 255}) {
    V3 p[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
               {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
    matQuad(mb, p[3], p[2], p[6], p[7], mat, col);
    matQuad(mb, p[1], p[0], p[4], p[5], mat, col);
    matQuad(mb, p[2], p[1], p[5], p[6], mat, col);
    matQuad(mb, p[0], p[3], p[7], p[4], mat, col);
    matQuad(mb, p[7], p[6], p[5], p[4], mat, col);
    matQuad(mb, p[0], p[1], p[2], p[3], mat, col);
}
void matTube(MeshBuilder& mb, V3 a, V3 b, float r, int mat, Col8 col) {
    int start = mb.count();
    mb.cylinderAxis(a, b, r, r, 6, col, true);
    for (int i = start; i < mb.count(); i++) mb.uv[i * 2] = (float)mat;
}

}  // namespace

CarModel buildCarModel(const CarSpec& S) {
    CarModel cm;
    MeshBuilder mb;
    const float L = S.length, W = S.width * 0.5f, H = S.height, R = S.wheelR;
    const float g = -S.cgH;  // уровень земли в системе машины
    const float zF = S.wheelbase * (1.0f - S.frontWeight), zR = -S.wheelbase * S.frontWeight;
    // центр кузова по длине: колёсная база внутри кузова с одинаковыми свесами
    const float zc = (zF + zR) * 0.5f;
    const float z0 = zc - L * 0.5f;
    std::vector<Key> keys = profileFor(S.body);
    float clearance = S.body == BODY_TRUCK || S.body == BODY_BUGGY ? R * 0.95f : (S.body == BODY_RALLY ? R * 0.55f : R * 0.42f);
    if (S.body == BODY_SUPER || S.body == BODY_HYPER || S.body == BODY_TRACK) clearance = R * 0.32f;
    float archR = R * 1.13f;
    float wheelCY = R;  // центр колеса над землёй

    // сечения вдоль кузова
    std::vector<float> zs;
    for (int i = 0; i <= 44; i++) zs.push_back(z0 + L * i / 44.0f);
    for (float wz : {zF, zR})
        for (int k = -6; k <= 6; k++) zs.push_back(wz + archR * k / 6.0f);
    std::sort(zs.begin(), zs.end());
    zs.erase(std::unique(zs.begin(), zs.end(), [](float a, float b) { return std::fabs(a - b) < 0.02f; }), zs.end());

    std::vector<Section> secs;
    for (float z : zs) {
        if (z < z0 - 1e-4f || z > z0 + L + 1e-4f) continue;
        float t = (z - z0) / L;
        Key k = interp(keys, t);
        Section s;
        s.z = z;
        s.cabin = k.cabin;
        float bottom = clearance + (t < 0.06f ? (0.06f - t) * 1.5f : 0) + (t > 0.94f ? (t - 0.94f) * 2.0f : 0);
        for (float wz : {zF, zR}) {
            float dz = z - wz;
            if (std::fabs(dz) < archR) bottom = std::max(bottom, wheelCY + std::sqrt(archR * archR - dz * dz));
        }
        s.belt = H * k.belt;
        s.top = std::max(H * k.top, s.belt + 0.01f);
        bottom = std::min(bottom, s.belt - 0.12f);
        s.bottom = bottom;
        s.hw = W * k.w;
        s.thw = W * k.tw;
        float yb = g + s.bottom, ys = g + s.belt, yt = g + s.top;
        s.p[0] = {0, yb, z};
        s.p[1] = {s.hw * 0.9f, yb, z};
        s.p[2] = {s.hw, yb + 0.1f, z};
        s.p[3] = {s.hw, ys - (ys - yb) * 0.18f, z};
        s.p[4] = {s.hw * 0.97f, ys, z};
        if (s.cabin) {
            s.p[5] = {s.thw, yt - 0.05f, z};
            s.p[6] = {s.thw * 0.88f, yt, z};
        } else {
            s.p[5] = {s.hw * 0.86f, ys + 0.02f, z};
            s.p[6] = {s.hw * 0.5f, ys + 0.035f, z};
        }
        s.p[7] = {0, yt + (s.cabin ? 0.0f : 0.04f), z};
        secs.push_back(s);
    }

    Col8 trim{30, 30, 34, 255};
    // боковины, крыша, днище
    for (size_t i = 0; i + 1 < secs.size(); i++) {
        const Section& A = secs[i];
        const Section& B = secs[i + 1];
        bool cab = A.cabin && B.cabin;
        float dz = B.z - A.z;
        bool windowSlope = cab && std::fabs(A.top - B.top) / std::max(dz, 1e-3f) > 0.5f;
        bool pillarB = cab && std::fabs((A.z + B.z) * 0.5f - (zc + L * 0.02f)) < 0.06f;
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.0f : -1.0f;
            for (int k = 0; k < 7; k++) {
                V3 a0 = A.p[k], a1 = A.p[k + 1], b0 = B.p[k], b1 = B.p[k + 1];
                a0.x *= sx; a1.x *= sx; b0.x *= sx; b1.x *= sx;
                int mat = M_PAINT;
                if (k == 0) mat = M_UNDER;
                else if (k == 1) mat = M_TRIM;
                else if (k == 4 && cab && !pillarB) mat = M_GLASS;
                else if ((k == 5 || k == 6) && windowSlope) mat = M_GLASS;
                if (S.body == BODY_ROADSTER && cab && k >= 5 && !windowSlope) mat = M_UNDER;  // открытый верх
                // обход против часовой снаружи
                if (side == 0) matQuad(mb, a0, a1, b1, b0, mat, trim);
                else matQuad(mb, a0, b0, b1, a1, mat, trim);
            }
        }
    }
    // торцы: веер от центра сечения
    for (int end = 0; end < 2; end++) {
        const Section& s = end == 0 ? secs.front() : secs.back();
        V3 c{0, g + (s.bottom + s.top) * 0.5f, s.z};
        std::vector<V3> ring;
        for (int k = 0; k <= 7; k++) ring.push_back(s.p[k]);
        for (int k = 6; k >= 0; k--) ring.push_back(V3{-s.p[k].x, s.p[k].y, s.p[k].z});
        for (size_t k = 0; k + 1 < ring.size(); k++) {
            if (end == 1) matVert(mb, c, ring[k], ring[k + 1], M_PAINT, trim);
            else matVert(mb, c, ring[k + 1], ring[k], M_PAINT, trim);
        }
    }

    // --- детали
    const Section& F = secs.back();
    const Section& Bk = secs.front();
    float zfront = F.z, zrear = Bk.z;
    float fBelt = g + F.belt, rBelt = g + Bk.belt;
    // фары
    float hlY = fBelt - 0.1f, hlX = F.hw * 0.62f;
    if (S.body == BODY_EV) {
        matBox(mb, {-F.hw * 0.8f, hlY - 0.02f, zfront - 0.03f}, {F.hw * 0.8f, hlY + 0.03f, zfront + 0.015f}, M_HEAD);
    } else if (S.body == BODY_CLASSIC || S.body == BODY_BUGGY) {
        for (float sx : {-1.0f, 1.0f}) {
            int st = mb.count();
            mb.cylinderAxis({sx * hlX, hlY, zfront - 0.04f}, {sx * hlX, hlY, zfront + 0.03f}, 0.09f, 0.09f, 10, trim, true);
            for (int i = st; i < mb.count(); i++) mb.uv[i * 2] = M_HEAD;
        }
    } else {
        for (float sx : {-1.0f, 1.0f}) {
            float xa = sx * (hlX - 0.2f), xb = sx * (hlX + 0.16f);
            matBox(mb, {std::min(xa, xb), hlY - 0.05f, zfront - 0.06f}, {std::max(xa, xb), hlY + 0.05f, zfront + 0.012f}, M_HEAD);
        }
    }
    cm.zFront = zfront;
    cm.zRear = zrear;
    for (int i = 0; i < 2; i++) {
        float sx = i ? -1.0f : 1.0f;
        cm.head[i] = {S.body == BODY_EV ? sx * F.hw * 0.6f : sx * hlX, hlY, zfront + 0.03f};
        cm.tail[i] = {sx * Bk.hw * 0.66f, rBelt - 0.1f, zrear - 0.03f};
    }
    // решётка и номер спереди
    float grY = g + F.bottom + (F.belt - F.bottom) * 0.35f;
    matBox(mb, {-F.hw * 0.45f, grY - 0.08f, zfront - 0.05f}, {F.hw * 0.45f, grY + 0.08f, zfront + 0.01f}, M_TRIM, {20, 20, 22, 255});
    matBox(mb, {-0.25f, grY - 0.19f, zfront - 0.02f}, {0.25f, grY - 0.08f, zfront + 0.02f}, M_TRIM, {230, 230, 230, 255});
    // задние фонари, задний ход, номер, выхлоп
    float tlY = rBelt - 0.1f, tlX = Bk.hw * 0.66f;
    for (float sx : {-1.0f, 1.0f}) {
        float xa = sx * (tlX - 0.22f), xb = sx * (tlX + 0.16f);
        matBox(mb, {std::min(xa, xb), tlY - 0.05f, zrear - 0.012f}, {std::max(xa, xb), tlY + 0.05f, zrear + 0.05f}, M_TAIL);
        float xr = sx * (tlX - 0.3f);
        matBox(mb, {std::min(xr, xr + sx * 0.08f), tlY - 0.04f, zrear - 0.01f}, {std::max(xr, xr + sx * 0.08f), tlY + 0.03f, zrear + 0.04f}, M_REV);
    }
    float rpY = g + Bk.bottom + (Bk.belt - Bk.bottom) * 0.45f;
    matBox(mb, {-0.25f, rpY - 0.06f, zrear - 0.02f}, {0.25f, rpY + 0.06f, zrear + 0.02f}, M_TRIM, {230, 230, 230, 255});
    if (!S.electric) {
        int ex = (S.cylinders >= 8) ? 2 : 1;
        for (int e = 0; e < ex; e++) {
            float xx = ex == 1 ? Bk.hw * 0.5f : (e == 0 ? -1.0f : 1.0f) * Bk.hw * 0.55f;
            int st = mb.count();
            mb.cylinderAxis({xx, g + Bk.bottom + 0.08f, zrear + 0.12f}, {xx, g + Bk.bottom + 0.08f, zrear - 0.08f}, 0.045f, 0.05f, 8, trim, true);
            for (int i = st; i < mb.count(); i++) mb.uv[i * 2] = M_CHROME;
        }
    }
    // зеркала у основания лобового стекла
    {
        float zm = zc;
        for (auto& s : secs)
            if (s.cabin) zm = s.z;  // последнее (переднее) сечение салона
        for (float sx : {-1.0f, 1.0f}) {
            float x0 = sx * (W * 0.98f), x1 = sx * (W * 0.98f + 0.14f);
            matBox(mb, {std::min(x0, x1), g + H * 0.62f, zm - 0.18f}, {std::max(x0, x1), g + H * 0.62f + 0.09f, zm - 0.04f}, M_PAINT);
        }
    }
    // бамперы из хрома у классики и маслкара
    if (S.body == BODY_CLASSIC || S.body == BODY_MUSCLE) {
        float by = g + F.bottom + 0.12f;
        matBox(mb, {-F.hw * 0.98f, by - 0.05f, zfront - 0.02f}, {F.hw * 0.98f, by + 0.05f, zfront + 0.06f}, M_CHROME);
        by = g + Bk.bottom + 0.12f;
        matBox(mb, {-Bk.hw * 0.98f, by - 0.05f, zrear - 0.06f}, {Bk.hw * 0.98f, by + 0.05f, zrear + 0.02f}, M_CHROME);
    }
    // капот-воздухозаборник у маслкара
    if (S.body == BODY_MUSCLE) {
        float zh = zfront - L * 0.22f;
        float y = g + H * 0.62f;
        matBox(mb, {-0.28f, y, zh - 0.45f}, {0.28f, y + 0.08f, zh + 0.25f}, M_PAINT);
        matBox(mb, {-0.24f, y + 0.02f, zh + 0.24f}, {0.24f, y + 0.075f, zh + 0.27f}, M_TRIM, {10, 10, 10, 255});
    }
    // спойлеры
    if (S.body == BODY_TRACK || S.body == BODY_JDM || S.body == BODY_RALLY || S.body == BODY_HYPER || S.body == BODY_SUPER) {
        float wy = g + (S.body == BODY_TRACK ? H * 0.98f : S.body == BODY_RALLY ? H * 0.9f : H * 0.78f);
        float wz = zrear + (S.body == BODY_RALLY ? L * 0.08f : L * 0.06f);
        float ww = W * (S.body == BODY_TRACK ? 0.98f : 0.85f);
        matBox(mb, {-ww, wy, wz - 0.18f}, {ww, wy + 0.04f, wz + 0.16f}, S.body == BODY_TRACK ? M_TRIM : M_PAINT, {20, 20, 22, 255});
        for (float sx : {-0.55f, 0.55f}) matBox(mb, {sx * W - 0.03f, g + Bk.belt, wz - 0.04f}, {sx * W + 0.03f, wy, wz + 0.04f}, M_TRIM, {20, 20, 22, 255});
        if (S.body == BODY_TRACK || S.body == BODY_HYPER) {
            for (float sx : {-1.0f, 1.0f}) matBox(mb, {sx * ww - 0.02f, wy - 0.15f, wz - 0.2f}, {sx * ww + 0.02f, wy + 0.1f, wz + 0.18f}, M_TRIM, {20, 20, 22, 255});
        }
    }
    // боковые воздухозаборники суперкаров
    if (S.body == BODY_SUPER || S.body == BODY_HYPER || S.body == BODY_TRACK) {
        for (float sx : {-1.0f, 1.0f}) {
            float x = sx * (W * 0.995f);
            V3 q0{x, g + H * 0.3f, zc - L * 0.12f}, q1{x, g + H * 0.3f, zc - L * 0.02f}, q2{x, g + H * 0.5f, zc - L * 0.02f}, q3{x, g + H * 0.5f, zc - L * 0.12f};
            // нормаль наружу: для правой (x > 0) стороны обход обратный
            if (sx > 0) matQuad(mb, q0, q3, q2, q1, M_TRIM, {10, 10, 12, 255});
            else matQuad(mb, q0, q1, q2, q3, M_TRIM, {10, 10, 12, 255});
        }
        // диффузор
        matBox(mb, {-Bk.hw * 0.8f, g + Bk.bottom - 0.02f, zrear}, {Bk.hw * 0.8f, g + Bk.bottom + 0.1f, zrear + 0.35f}, M_TRIM, {15, 15, 18, 255});
    }
    // раллийный: люстра и брызговики
    if (S.body == BODY_RALLY) {
        float zr = zc;
        for (auto& s : secs)
            if (s.cabin) { zr = s.z; break; }
        matBox(mb, {-0.35f, g + H * 1.0f, zr + 0.3f}, {0.35f, g + H * 1.06f, zr + 0.9f}, M_PAINT);
        for (float sx : {-1.0f, 1.0f})
            for (float wz : {zF, zR}) matBox(mb, {sx * S.track * 0.5f - 0.18f, g + 0.05f, wz - R - 0.06f}, {sx * S.track * 0.5f + 0.18f, g + R * 0.9f, wz - R - 0.03f}, M_TRIM, {200, 30, 30, 255});
    }
    // пикап: кузов и дуга с фарами
    if (S.body == BODY_TRUCK) {
        float zb0 = zrear + 0.05f, zb1 = z0 + L * 0.42f;
        float yb = g + H * 0.58f;
        matBox(mb, {-W * 0.92f, yb - 0.02f, zb0}, {W * 0.92f, yb + 0.01f, zb1}, M_UNDER, {25, 25, 25, 255});
        matBox(mb, {-0.5f, g + H * 1.0f, z0 + L * 0.62f}, {0.5f, g + H * 1.04f, z0 + L * 0.66f}, M_TRIM, {20, 20, 22, 255});
        for (int i = 0; i < 4; i++) {
            float xx = -0.42f + i * 0.28f;
            matBox(mb, {xx - 0.07f, g + H * 1.04f, z0 + L * 0.66f - 0.02f}, {xx + 0.07f, g + H * 1.1f, z0 + L * 0.66f + 0.04f}, M_HEAD);
        }
        matBox(mb, {-F.hw * 0.7f, g + F.bottom, zfront}, {F.hw * 0.7f, g + F.bottom + 0.35f, zfront + 0.12f}, M_TRIM, {20, 20, 22, 255});
    }
    // багги: каркас безопасности
    if (S.body == BODY_BUGGY) {
        Col8 cage{40, 40, 44, 255};
        float y0 = g + H * 0.48f, y1 = g + H * 1.0f;
        float za = zc - 0.55f, zb = zc + 0.5f, xw = W * 0.62f;
        V3 pts[8] = {{xw, y0, za}, {-xw, y0, za}, {xw, y0, zb}, {-xw, y0, zb}, {xw * 0.8f, y1, za + 0.1f}, {-xw * 0.8f, y1, za + 0.1f}, {xw * 0.8f, y1, zb - 0.2f}, {-xw * 0.8f, y1, zb - 0.2f}};
        int e[12][2] = {{0, 4}, {1, 5}, {2, 6}, {3, 7}, {4, 5}, {6, 7}, {4, 6}, {5, 7}, {0, 2}, {1, 3}, {2, 3}, {0, 1}};
        for (auto& ed : e) matTube(mb, pts[ed[0]], pts[ed[1]], 0.035f, M_CHROME, cage);
        matBox(mb, {-0.3f, y0, zc - 0.35f}, {0.3f, y0 + 0.45f, zc - 0.25f}, M_UNDER, {30, 30, 30, 255});  // спинки сидений
    }
    // родстер: дуги безопасности
    if (S.body == BODY_ROADSTER) {
        float zr = zc - 0.25f;
        for (float sx : {-0.45f, 0.45f}) matTube(mb, {sx * W, g + H * 0.64f, zr}, {sx * W, g + H * 0.9f, zr - 0.05f}, 0.04f, M_CHROME, trim);
        matBox(mb, {-W * 0.7f, g + H * 0.6f, zc - 0.1f}, {W * 0.7f, g + H * 0.64f, zc + 0.6f}, M_UNDER, {60, 30, 25, 255});
    }
    cm.body = mb.upload();

    // --- колесо: ось по X
    MeshBuilder wb;
    float tw = S.wheelW * 0.5f;
    Col8 tire{25, 25, 27, 255};
    int st = wb.count();
    wb.cylinderAxis({-tw, 0, 0}, {tw, 0, 0}, R, R, 20, tire, false);
    for (int i = st; i < wb.count(); i++) wb.uv[i * 2] = M_RUBBER;
    // боковины шины и диск
    for (float sx : {-1.0f, 1.0f}) {
        float xo = sx * tw;
        int s0 = wb.count();
        // кольцо боковины
        for (int i = 0; i < 20; i++) {
            float a0 = i * TAU / 20, a1 = (i + 1) * TAU / 20;
            V3 o0{xo, std::sin(a0) * R, std::cos(a0) * R}, o1{xo, std::sin(a1) * R, std::cos(a1) * R};
            V3 i0{xo, std::sin(a0) * R * 0.68f, std::cos(a0) * R * 0.68f}, i1{xo, std::sin(a1) * R * 0.68f, std::cos(a1) * R * 0.68f};
            if (sx > 0) matQuad(wb, i0, o0, o1, i1, M_RUBBER, tire);
            else matQuad(wb, i0, i1, o1, o0, M_RUBBER, tire);
        }
        (void)s0;
        // диск: утопленный круг и спицы
        float xd = sx * (tw - 0.03f);
        for (int i = 0; i < 20; i++) {
            float a0 = i * TAU / 20, a1 = (i + 1) * TAU / 20;
            V3 c{xd, 0, 0}, p0{xd, std::sin(a0) * R * 0.68f, std::cos(a0) * R * 0.68f}, p1{xd, std::sin(a1) * R * 0.68f, std::cos(a1) * R * 0.68f};
            if (sx > 0) matVert(wb, c, p0, p1, M_UNDER, {20, 20, 22, 255});
            else matVert(wb, c, p1, p0, M_UNDER, {20, 20, 22, 255});
        }
        int spokes = S.body == BODY_CLASSIC ? 8 : 5;
        for (int i = 0; i < spokes; i++) {
            float a = i * TAU / spokes;
            V3 dir{0, std::sin(a), std::cos(a)}, side{0, std::cos(a), -std::sin(a)};
            V3 a0 = V3{xd + sx * 0.012f, 0, 0} + side * 0.04f, a1 = V3{xd + sx * 0.012f, 0, 0} - side * 0.04f;
            V3 b0 = a0 + dir * (R * 0.66f) + side * 0.02f, b1 = a1 + dir * (R * 0.66f) - side * 0.02f;
            if (sx > 0) matQuad(wb, a1, a0, b0, b1, M_CHROME, tire);
            else matQuad(wb, a0, a1, b1, b0, M_CHROME, tire);
        }
        // обод
        for (int i = 0; i < 20; i++) {
            float a0 = i * TAU / 20, a1 = (i + 1) * TAU / 20;
            float r0 = R * 0.64f, r1 = R * 0.69f;
            V3 i0{xd + sx * 0.012f, std::sin(a0) * r0, std::cos(a0) * r0}, o0{xd + sx * 0.012f, std::sin(a0) * r1, std::cos(a0) * r1};
            V3 i1{xd + sx * 0.012f, std::sin(a1) * r0, std::cos(a1) * r0}, o1{xd + sx * 0.012f, std::sin(a1) * r1, std::cos(a1) * r1};
            if (sx > 0) matQuad(wb, i0, o0, o1, i1, M_CHROME, tire);
            else matQuad(wb, i0, i1, o1, o0, M_CHROME, tire);
        }
    }
    cm.wheel = wb.upload();
    cm.ready = true;
    return cm;
}

}  // namespace cl
