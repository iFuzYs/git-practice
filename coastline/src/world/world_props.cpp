// Расстановка объектов мира: леса, город, фестиваль, фермы, ориентиры, трамплины
#include "world.h"

namespace cl {

int World::roadIndex(const std::string& name) const {
    for (auto& r : roads)
        if (r.name == name) return r.id;
    return -1;
}

const Trail* World::trail(const std::string& id) const {
    for (auto& t : trails)
        if (t.id == id) return &t;
    return nullptr;
}

static std::vector<uint8_t> g_trailMask;

bool World::nearTrail(float x, float z, float) const {
    int i = (int)((x - ORIGIN) / CELL + 0.5f), j = (int)((z - ORIGIN) / CELL + 0.5f);
    if (i < 0 || j < 0 || i >= N || j >= N || g_trailMask.empty()) return false;
    return g_trailMask[idx(i, j)] != 0;
}

void World::defineTrails() {
    trails.clear();
    auto mk = [&](const char* id, const char* name, std::vector<V3> ctrl) {
        Trail t;
        t.id = id;
        t.name = name;
        t.pts = sampleSpline(ctrl, false, 4.0f);
        for (auto& p : t.pts) p.y = terrainH(p.x, p.z);
        trails.push_back(std::move(t));
    };
    mk("hills", "Через холмы",
       {{-110, 0, -110}, {-190, 0, -330}, {-130, 0, -560}, {-270, 0, -760}, {-430, 0, -840}, {-560, 0, -760}, {-620, 0, -700}});
    mk("river", "Речная долина",
       {{820, 0, 240}, {620, 0, 285}, {470, 0, 305}, {440, 0, 330}, {420, 0, 360}, {400, 0, 392}, {330, 0, 432}, {160, 0, 456}, {-40, 0, 482},
        {-240, 0, 512}, {-330, 0, 545}, {-360, 0, 505}, {-385, 0, 470}, {-450, 0, 478}, {-540, 0, 500}});
    mk("descent", "Спуск с пика",
       {{-935, 0, -925}, {-1010, 0, -800}, {-1075, 0, -600}, {-1010, 0, -380}, {-890, 0, -210}, {-770, 0, -10}, {-700, 0, 190}, {-655, 0, 370}});
    mk("sky", "Небесный вызов",
       {{260, 0, -560}, {560, 0, -560}, {760, 0, -430}, {820, 0, -260}, {600, 0, -120}, {360, 0, -40}, {160, 0, 10}, {40, 0, 20}});
    g_trailMask.assign(N * N, 0);
    for (auto& t : trails) {
        for (auto& p : t.pts) {
            int ci = (int)((p.x - ORIGIN) / CELL), cj = (int)((p.z - ORIGIN) / CELL);
            for (int j = cj - 3; j <= cj + 3; j++)
                for (int i = ci - 3; i <= ci + 3; i++)
                    if (i >= 0 && j >= 0 && i < N && j < N && sq(vx(i) - p.x) + sq(vx(j) - p.z) < 11.0f * 11.0f) g_trailMask[idx(i, j)] = 1;
        }
    }
}

int World::addProp(PropKind k, V3 p, float yaw, float s, uint8_t var) {
    Prop pr;
    pr.kind = k;
    pr.pos = p;
    pr.yaw = yaw;
    pr.scale = s;
    pr.var = var;
    int id = (int)props.size();
    Collider c;
    c.c = p;
    c.y0 = p.y;
    c.prop = id;
    c.yaw = yaw;
    switch (k) {
        case PK_PINE: c.r = 0.32f * s; c.y1 = p.y + 9 * s; c.smash = s < 0.72f; break;
        case PK_OAK: c.r = 0.45f * s; c.y1 = p.y + 7 * s; c.smash = s < 0.72f; break;
        case PK_BIRCH: c.r = 0.22f * s; c.y1 = p.y + 8 * s; c.smash = s < 0.8f; break;
        case PK_PALM: c.r = 0.26f * s; c.y1 = p.y + 9 * s; break;
        case PK_BUSH: c.r = 0.9f * s; c.y1 = p.y + 1.4f * s; c.smash = true; break;
        case PK_ROCK: c.shape = COL_SPHERE; c.r = 1.25f * s; c.c.y = p.y + 0.25f * s; c.y1 = p.y + 2.3f * s; break;
        case PK_FENCE: c.shape = COL_BOX; c.hx = 1.6f; c.hz = 0.12f; c.y1 = p.y + 1.25f; c.smash = true; break;
        case PK_BOARD: c.shape = COL_BOX; c.hx = 1.7f; c.hz = 0.25f; c.y1 = p.y + 3.2f; c.smash = true; break;
        case PK_CONE: c.r = 0.28f; c.y1 = p.y + 0.75f; c.smash = true; break;
        case PK_HAY: c.r = 0.8f; c.y1 = p.y + 1.4f; c.smash = true; break;
        case PK_LAMP: c.r = 0.16f; c.y1 = p.y + 7.5f; c.smash = true; break;
        case PK_TURBINE: c.r = 2.1f; c.y1 = p.y + 74.0f; break;
        case PK_POLE: c.r = 0.18f; c.y1 = p.y + 9.0f; c.smash = true; break;
        case PK_BARRIER: c.shape = COL_BOX; c.hx = 0.18f; c.hz = 2.05f; c.y1 = p.y + 0.95f; break;
        case PK_FLAG: c.r = 0.12f; c.y1 = p.y + 9.0f; c.smash = true; break;
        default: c.r = 0.4f; c.y1 = p.y + 2; break;
    }
    pr.col = (int)cols.size();
    props.push_back(pr);
    addCol(c);
    return id;
}

void World::addBuilding(BuildStyle st, V3 c, float hx, float hz, float h, float yaw, uint32_t color, uint8_t var, bool collide) {
    Building b;
    b.style = st;
    b.c = c;
    b.hx = hx;
    b.hz = hz;
    b.h = h;
    b.yaw = yaw;
    b.color = color;
    b.var = var;
    buildings.push_back(b);
    if (collide) {
        Collider k;
        k.shape = COL_BOX;
        k.c = c;
        k.hx = hx;
        k.hz = hz;
        k.yaw = yaw;
        k.y0 = c.y - 2.0f;
        k.y1 = c.y + h;
        addCol(k);
    }
}

void World::addRamp(V3 c, float yaw, float w, float len, float h) {
    Ramp r;
    r.c = c;
    r.c.y = terrainH(c.x, c.z);
    r.hx = w * 0.5f;
    r.hz = len * 0.5f;
    r.yaw = yaw;
    r.cs = std::cos(yaw);
    r.sn = std::sin(yaw);
    r.h0 = 0.0f;
    r.h1 = h;
    ramps.push_back(r);
    // задняя стенка клина: низкая и тонкая, чтобы не мешать съезду с кромки,
    // но не пускать машину сквозь трамплин сзади
    Collider k;
    k.shape = COL_BOX;
    V3 fwd{r.sn, 0, r.cs};
    k.c = r.c + fwd * (r.hz - 0.35f);
    k.hx = r.hx;
    k.hz = 0.35f;
    k.yaw = yaw;
    k.y0 = r.c.y - 1.0f;
    k.y1 = r.c.y + h * 0.6f;
    addCol(k);
}

static uint32_t rgb(int r, int g, int b) { return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r; }

void World::placeProps() {
    props.clear();
    buildings.clear();
    ramps.clear();
    cols.clear();
    boards.clear();
    places.clear();
    colGrid_.assign(CGN * CGN, {});
    Rng rng(seed * 7919u + 13u);

    auto H = [&](float x, float z) { return terrainH(x, z); };
    auto P = [&](float x, float z) { return V3{x, H(x, z), z}; };

    // --- ключевые места
    places.push_back({"festival", "Фестиваль Coastline", P(0, 40), PI});
    places.push_back({"town", "Порто-Бриз", P(820, 720), 0});
    places.push_back({"summit", "Орлиный пик", P(-950, -950), 0.6f});
    places.push_back({"airfield", "Аэродром «Чайка»", P(300, -560), PI * 0.5f});
    places.push_back({"lighthouse", "Маяк Санта-Лус", P(1190, -900), -PI * 0.5f});
    places.push_back({"lake", "Озеро Зеркальное", P(-560, 470), 0});
    places.push_back({"farm", "Фермы Долины", P(-300, 1000), 0});
    places.push_back({"windfarm", "Ветряной хребет", P(500, -860), 0});
    places.push_back({"beach", "Пляж Дюн", P(1120, -150), 0});

    // --- фестиваль: сцена и трибуны по диагоналям, выезды дорог свободны
    {
        V3 c = P(0, 0);
        auto facing = [](float x, float z) { return std::atan2(-x, -z); };  // лицом к центру
        float sy = facing(-78, -78);
        V3 stage{-78, c.y, -78};
        addBuilding(BS_STAGE, stage, 22, 8, 15, sy, rgb(30, 30, 38));
        V3 sx{std::cos(sy), 0, -std::sin(sy)};
        addBuilding(BS_SCREEN, stage + sx * 31.0f, 5.5f, 0.6f, 13, sy, rgb(20, 20, 24));
        addBuilding(BS_SCREEN, stage - sx * 31.0f, 5.5f, 0.6f, 13, sy, rgb(20, 20, 24));
        addBuilding(BS_GRANDSTAND, V3{84, c.y, -84}, 26, 7, 9, facing(84, -84), rgb(230, 230, 235));
        addBuilding(BS_GRANDSTAND, V3{-84, c.y, 84}, 26, 7, 9, facing(-84, 84), rgb(230, 230, 235));
        uint32_t tentCol[] = {rgb(255, 92, 60), rgb(255, 200, 40), rgb(60, 190, 255), rgb(255, 255, 255), rgb(150, 90, 255)};
        float tentDeg[] = {25, 48, 68, 112, 162, 200, 255, 338};
        for (int i = 0; i < 8; i++) {
            float a = tentDeg[i] * DEG;
            float x = std::cos(a) * 116, z = std::sin(a) * 116;
            if (roadDistAt(x, z) < 10) continue;
            addBuilding(BS_TENT, P(x, z), 7, 7, 7, a, tentCol[i % 5], (uint8_t)i);
        }
        // флаги по кругу
        for (int i = 0; i < 44; i++) {
            float a = i * TAU / 44;
            float x = std::cos(a) * 150, z = std::sin(a) * 150;
            if (roadDistAt(x, z) < 3) continue;
            addProp(PK_FLAG, P(x, z), a, 1, (uint8_t)(i % 5));
        }
        // арки над выездами
        for (const char* rn : {"Северная дорога", "Восточная дорога", "Южная дорога", "Западная дорога"}) {
            int ri = roadIndex(rn);
            if (ri < 0) continue;
            const Road& r = roads[ri];
            for (auto& p : r.pts) {
                if (std::sqrt(p.p.x * p.p.x + p.p.z * p.p.z) > 138) {
                    float yaw = std::atan2(p.t.x, p.t.z);
                    addBuilding(BS_ARCH, p.p, r.halfW() + 2.0f, 1.0f, 11, yaw, rgb(255, 120, 40), 0, false);
                    for (float sd : {-1.0f, 1.0f}) {
                        Collider k;
                        k.c = p.p + p.l * (sd * (r.halfW() + 2.0f));
                        k.r = 0.8f;
                        k.y0 = p.p.y - 1;
                        k.y1 = p.p.y + 11;
                        addCol(k);
                    }
                    break;
                }
            }
        }
        // выставочные машины перед сценой
        V3 sf{std::sin(sy), 0, std::cos(sy)};
        for (int i = 0; i < 4; i++) {
            V3 q = stage + sf * 26.0f + sx * ((i - 1.5f) * 12.0f);
            q.y = c.y;
            float yaw = sy + PI + (i - 1.5f) * 0.25f;
            places.push_back({"showcar" + std::to_string(i), "", q, yaw});
            Collider k;
            k.shape = COL_BOX;
            k.c = q;
            k.hx = 1.2f;
            k.hz = 2.4f;
            k.yaw = yaw;
            k.y0 = q.y - 1;
            k.y1 = q.y + 1.6f;
            addCol(k);
        }
        // конусы для разминки в юго-восточном секторе
        for (int i = 0; i < 8; i++) addProp(PK_CONE, P(34 + i * 10.0f, 36 + (i % 2) * 3.0f), 0, 1);
    }

    // --- город Порто-Бриз
    {
        uint32_t pastel[] = {rgb(245, 222, 190), rgb(240, 196, 170), rgb(250, 240, 210), rgb(200, 226, 222), rgb(236, 205, 214), rgb(226, 214, 180), rgb(250, 250, 244), rgb(210, 200, 230)};
        float xs[] = {575, 640, 760, 880, 1060};
        float zs[] = {575, 660, 780, 900, 1020, 1080};
        for (int bx = 0; bx < 4; bx++)
            for (int bz = 0; bz < 5; bz++) {
                float x0 = xs[bx] + (bx == 0 ? 8.0f : 10.0f), x1 = xs[bx + 1] - 10.0f;
                float z0 = zs[bz] + (bz == 0 ? 8.0f : 10.0f), z1 = zs[bz + 1] - (bz == 4 ? 4.0f : 10.0f);
                if (x1 - x0 < 16 || z1 - z0 < 16) continue;
                float cxb = (x0 + x1) * 0.5f, czb = (z0 + z1) * 0.5f;
                float centr = std::sqrt(sq(cxb - 820) + sq(czb - 840));
                // периметр квартала
                for (int side = 0; side < 4; side++) {
                    float a0 = (side % 2 == 0) ? x0 : z0, a1 = (side % 2 == 0) ? x1 : z1;
                    float pos = a0;
                    while (pos < a1 - 10) {
                        float w = rng.range(12, 22);
                        if (pos + w > a1) w = a1 - pos;
                        if (w < 9) break;
                        float d = rng.range(12, 18);
                        float cx, cz, hx, hz;
                        if (side == 0) { cx = pos + w / 2; cz = z0 + d / 2; hx = w / 2; hz = d / 2; }
                        else if (side == 2) { cx = pos + w / 2; cz = z1 - d / 2; hx = w / 2; hz = d / 2; }
                        else if (side == 1) { cz = pos + w / 2; cx = x1 - d / 2; hz = w / 2; hx = d / 2; }
                        else { cz = pos + w / 2; cx = x0 + d / 2; hz = w / 2; hx = d / 2; }
                        // угловые участки не дублируем
                        bool skip = (side == 1 || side == 3) && (cz - hz < z0 + 17 || cz + hz > z1 - 17);
                        if (!skip && (z1 - z0) > 20 && (x1 - x0) > 20) {
                            float h;
                            BuildStyle st;
                            if (centr < 150 && rng.chance(0.5f)) { h = rng.range(22, 46); st = BS_TOWER; }
                            else if (rng.chance(0.55f)) { h = rng.range(7, 13); st = BS_SHOP; }
                            else { h = rng.range(6, 11); st = BS_HOUSE; }
                            addBuilding(st, P(cx, cz), hx - 0.6f, hz - 0.6f, h, 0, pastel[rng.irange(0, 7)], (uint8_t)rng.irange(0, 3));
                        }
                        pos += w + rng.range(0.5f, 3.0f);
                    }
                }
            }
        // фонари вдоль улиц и пальмы на набережной
        for (auto& r : roads) {
            if (r.type != ROAD_STREET) continue;
            for (size_t i = 6; i < r.pts.size(); i += 16) {
                const RoadPt& p = r.pts[i];
                for (float sd : {-1.0f, 1.0f}) {
                    V3 q = p.p + p.l * (sd * (r.halfW() + 1.6f));
                    q.y = H(q.x, q.z);
                    addProp(PK_LAMP, q, std::atan2(p.l.x * -sd, p.l.z * -sd), 1);
                }
            }
        }
        for (float z = 540; z < 1100; z += 18) {
            V3 q = P(1076 + rng.range(-1, 1), z);
            if (q.y > 0.5f) addProp(PK_PALM, q, rng.range(0, TAU), rng.range(0.9f, 1.25f));
        }
    }

    // --- аэродром
    {
        for (int i = 0; i < 3; i++) addBuilding(BS_HANGAR, P(320 + i * 110.0f, -640), 20, 17, 13, 0, rgb(200, 205, 210));
        addBuilding(BS_TOWERMAST, P(760, -635), 4.5f, 4.5f, 24, 0, rgb(240, 240, 240));
        for (int i = 0; i < 14; i++) addProp(PK_CONE, P(220 + i * 14.0f, -598), 0, 1);
    }

    // --- ориентиры
    addBuilding(BS_LIGHTHOUSE, P(1208, -906), 4, 4, 34, 0, rgb(250, 250, 250));
    addBuilding(BS_HOUSE, P(1188, -880), 5, 4, 5, 0.3f, rgb(236, 90, 70));
    addBuilding(BS_OBSERVATORY, P(-994, -998), 9, 9, 12, 0, rgb(236, 236, 240));
    addBuilding(BS_TOWERMAST, P(-930, -1004), 2, 2, 45, 0, rgb(210, 60, 50));
    for (float x = 190; x <= 840; x += 108) {
        float z = -905 + noise_.perlin(x * 0.01f, 3.3f) * 25;
        if (roadDistAt(x, z) < 10) continue;
        addProp(PK_TURBINE, P(x, z), 0.4f, 1);
    }
    // домики у озера
    for (int i = 0; i < 4; i++) {
        float a = -0.2f + i * 0.28f;
        V3 q = P(-800 + std::cos(a) * 300, 650 - std::sin(a) * 300);
        if (q.y > 2) addBuilding(BS_HOUSE, q, 4.5f, 3.5f, 5, a, rgb(180, 120, 80), 1);
    }
    // фермы: амбары, тюки, заборы
    {
        V3 bpos[] = {{-250, 0, 975}, {-520, 0, 1050}, {-150, 0, 1085}, {-700, 0, 1095}};
        for (auto& b : bpos) addBuilding(BS_BARN, P(b.x, b.z), 9, 13, 10, rng.range(-0.3f, 0.3f), rgb(170, 50, 40));
        for (int i = 0; i < 26; i++) {
            float x = rng.range(-650, -120), z = rng.range(870, 1170);
            if (roadDistAt(x, z) < 6 || H(x, z) < 2) continue;
            addProp(PK_HAY, P(x, z), rng.range(0, TAU), 1);
        }
        int ri = roadIndex("Фермерская грунтовка");
        if (ri >= 0) {
            const Road& r = roads[ri];
            for (size_t i = 20; i + 20 < r.pts.size(); i += 2) {
                const RoadPt& p = r.pts[i];
                float fenceN = noise_.perlin(p.p.x * 0.006f, p.p.z * 0.006f);
                if (fenceN < -0.1f) continue;
                for (float sd : {-1.0f, 1.0f}) {
                    V3 q = p.p + p.l * (sd * (r.halfW() + 2.6f));
                    q.y = H(q.x, q.z);
                    addProp(PK_FENCE, q, std::atan2(p.t.x, p.t.z) + PI * 0.5f, 1);
                }
            }
        }
        // столбы вдоль южной дороги
        ri = roadIndex("Южная дорога");
        if (ri >= 0) {
            const Road& r = roads[ri];
            for (size_t i = 60; i < r.pts.size(); i += 22) {
                const RoadPt& p = r.pts[i];
                if (p.bridge) continue;
                V3 q = p.p + p.l * (r.halfW() + 3.5f);
                q.y = H(q.x, q.z);
                addProp(PK_POLE, q, std::atan2(p.t.x, p.t.z), 1);
            }
        }
    }
    // пляжные хижины у дюн
    for (int i = 0; i < 3; i++) addBuilding(BS_HOUSE, P(1140, -300 + i * 140.0f), 3, 3, 3.5f, 0.2f * i, rgb(60, 170, 200), 2);

    // --- ограждения мостов
    for (auto& r : roads) {
        if (r.type == ROAD_RUNWAY) continue;
        for (size_t i = 0; i < r.pts.size(); i += 2) {
            const RoadPt& p = r.pts[i];
            if (!p.bridge) continue;
            for (float sd : {-1.0f, 1.0f}) {
                V3 q = p.p + p.l * (sd * (r.halfW() + 0.25f));
                addProp(PK_BARRIER, q, std::atan2(p.t.x, p.t.z), 1);
            }
        }
    }

    // --- трамплины «Опасность»
    addRamp({180, 0, -62}, PI * 0.5f, 7, 10, 2.6f);    // у фестиваля, на восток
    addRamp({200, 0, 322}, 0.0f, 7, 11, 3.0f);         // через реку, на юг
    addRamp({-300, 0, 895}, -PI * 0.5f, 7, 10, 2.6f);  // поле фермы, на запад
    addRamp({1105, 0, -250}, 0.0f, 7, 10, 2.4f);       // дюны, на юг
    addRamp({410, 0, -770}, PI, 7, 10, 2.8f);          // склон у ветряков, на север

    // --- деревья, кусты, камни
    int trees = 0;
    auto inFlat = [&](float x, float z) {
        if (x * x + z * z < 205 * 205) return true;
        if (x > 520 && x < 1110 && z > 520 && z < 1120) return true;
        if (x > 120 && x < 1090 && z > -680 && z < -470) return true;
        return false;
    };
    for (float gz = -1420; gz < 1420; gz += 9)
        for (float gx = -1420; gx < 1420; gx += 9) {
            float x = gx + rng.range(-3.6f, 3.6f), z = gz + rng.range(-3.6f, 3.6f);
            float h = H(x, z);
            float rd = roadDistAt(x, z);
            if (rd < 4.0f + rng.range(0, 5)) continue;
            if (inFlat(x, z) || nearTrail(x, z, 11)) continue;
            bool nearRamp = false;
            for (auto& rp : ramps) if (sq(rp.c.x - x) + sq(rp.c.z - z) < 70 * 70) nearRamp = true;
            if (nearRamp) continue;
            V3 n = terrainN(x, z);
            float forest = noise_.fbm(x * 0.0032f + 8.0f, z * 0.0032f - 2.0f, 4);
            float coastD = std::min(1160.0f - x, 1300.0f - z);
            if (h < 1.3f) {
                // пляж: редкие пальмы
                if (h > 0.55f && coastD > 5 && rng.chance(0.03f)) { addProp(PK_PALM, V3{x, h, z}, rng.range(0, TAU), rng.range(0.85f, 1.3f)); trees++; }
                continue;
            }
            if (n.y < 0.72f) {
                if (rng.chance(0.08f)) addProp(PK_ROCK, V3{x, h, z}, rng.range(0, TAU), rng.range(0.8f, 2.6f), (uint8_t)rng.irange(0, 2));
                continue;
            }
            float pTree = forest > -0.12f ? smoothstep(-0.12f, 0.1f, forest) * 0.86f : 0.02f;
            if (h > 270) pTree *= 0.25f;
            if (coastD < 160) pTree = 0.06f;
            if (!rng.chance(pTree)) {
                if (forest > -0.05f && rng.chance(0.05f)) addProp(PK_BUSH, V3{x, h, z}, rng.range(0, TAU), rng.range(0.7f, 1.3f));
                else if (h > 120 && rng.chance(0.02f)) addProp(PK_ROCK, V3{x, h, z}, rng.range(0, TAU), rng.range(0.7f, 1.8f), (uint8_t)rng.irange(0, 2));
                continue;
            }
            PropKind k;
            if (coastD < 160) k = PK_PALM;
            else if (h > 110 || z < -950) k = PK_PINE;
            else {
                float m = noise_.perlin(x * 0.004f + 40, z * 0.004f);
                k = m > 0.15f ? PK_PINE : (m > -0.15f ? PK_OAK : PK_BIRCH);
            }
            float s = rng.range(0.75f, 1.35f);
            if (rng.chance(0.08f)) s = rng.range(0.5f, 0.7f);
            addProp(k, V3{x, h, z}, rng.range(0, TAU), s, (uint8_t)rng.irange(0, 2));
            trees++;
        }
    treeCount = trees;

    // --- доски опыта (как в «Горизонте»): 30 штук у дорог и тропинок
    {
        Rng br(seed + 991);
        int want = 30;
        std::vector<int> ridx;
        for (auto& r : roads)
            if (r.type != ROAD_RUNWAY && r.type != ROAD_STREET && r.length > 400) ridx.push_back(r.id);
        int guard = 0;
        while ((int)boards.size() < want && guard++ < 4000) {
            const Road& r = roads[ridx[br.irange(0, (int)ridx.size() - 1)]];
            float s = br.range(40, r.length - 40);
            RoadPt p = r.at(s);
            if (p.bridge) continue;
            float sd = br.chance(0.5f) ? 1.0f : -1.0f;
            V3 q = p.p + p.l * (sd * (r.halfW() + br.range(4.0f, 9.0f)));
            q.y = H(q.x, q.z);
            if (q.y < 1.0f || inFlat(q.x, q.z)) continue;
            bool close = false;
            for (int b : boards) if (dist2xz(props[b].pos, q) < 250 * 250) close = true;
            if (close) continue;
            boards.push_back(addProp(PK_BOARD, q, std::atan2(p.t.x, p.t.z) + PI * 0.5f, 1));
        }
    }
}

}  // namespace cl
