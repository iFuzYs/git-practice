// Автотесты физики и мира без окна. Выход с кодом 1, если что-то вне допусков.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../src/sim/car_spec.h"
#include "../src/sim/events.h"
#include "../src/sim/vehicle.h"
#include "../src/world/world.h"

using namespace cl;

static int g_fail = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        if (!(cond)) {                           \
            printf("  FAIL: " __VA_ARGS__);      \
            printf("\n");                        \
            g_fail++;                            \
        }                                        \
    } while (0)

static const float DT = 1.0f / 120.0f;

static void settle(Vehicle& v, const World& w, float sec) {
    v.in = Controls{};
    v.in.handbrake = true;
    v.in.brake = 1;
    v.as.autoGear = false;  // чтобы тормоз на месте не включил заднюю
    for (int i = 0; i < (int)(sec / DT); i++) v.step(DT, w);
    v.as.autoGear = true;
    v.gear = 1;
    v.in = Controls{};
}

static V3 runwayStart(const World& w, float& yaw) {
    const Road& r = w.roads[w.roadIndex("Взлётная полоса")];
    RoadPt p = r.at(20);
    yaw = std::atan2(p.t.x, p.t.z);
    return p.p;
}

static void testAccel(const World& w) {
    printf("== Разгон, максималка, торможение (взлётная полоса)\n");
    printf("  %-18s %4s %6s %7s %7s %7s %6s\n", "машина", "PI", "0-100", "V@20с", "Vmax", "торм.м", "дрейф");
    for (auto& spec : carCatalog()) {
        Vehicle v;
        float yaw;
        V3 p = runwayStart(w, yaw);
        v.init(&spec, p, yaw);
        settle(v, w, 1.0f);
        v.in = Controls{};
        v.in.throttle = 1;
        float t = 0, t100 = -1, v20 = 0, vmax = 0;
        V3 start = v.pos;
        for (int i = 0; i < (int)(26 / DT); i++) {
            v.step(DT, w);
            t += DT;
            if (t100 < 0 && v.kmh() >= 100) t100 = t;
            if (std::fabs(t - 20) < DT * 0.5f) v20 = v.kmh();
            vmax = std::max(vmax, v.kmh());
            // полоса 860 м: разворачиваемся телепортом, если кончается
            const Road& r = w.roads[w.roadIndex("Взлётная полоса")];
            RoadHit rh;
            if (w.roadAt(v.pos.x, v.pos.z, rh) && rh.s > r.length - 60) {
                RoadPt q = r.at(30);
                V3 vel = v.vel;
                float sp = len(vel);
                V3 f = v.fwd();
                v.pos = q.p + V3{0, v.pos.y - q.p.y, 0};
                v.vel = V3{f.x, 0, f.z} * sp;
            }
        }
        float lateral = std::fabs(dot(v.pos - start, V3{0, 0, 1}));
        // торможение со 100 км/ч
        v.teleport(p, yaw);
        settle(v, w, 0.5f);
        v.vel = v.fwd() * (100 / 3.6f);
        for (auto& wh : v.w) wh.omega = (100 / 3.6f) / wh.radius;
        V3 b0 = v.pos;
        v.in = Controls{};
        v.in.brake = 1;
        float bt = 0;
        do { v.step(DT, w); bt += DT; } while (v.speed > 0.3f && bt < 10);
        float bd = distxz(v.pos, b0);
        printf("  %-18s %4d %6.1f %7.0f %7.0f %7.1f %6.1f\n", spec.id.c_str(), spec.pi, t100, v20, vmax, bd, lateral);
        CHECK(t100 > 0 && t100 < 19.0f, "%s: 0-100 = %.1f c", spec.id.c_str(), t100);
        CHECK(vmax > spec.topSpeedKmh * 0.75f && vmax < spec.topSpeedKmh * 1.15f, "%s: Vmax %.0f при заявленной %.0f", spec.id.c_str(), vmax, spec.topSpeedKmh);
        CHECK(bd > 25 && bd < 60, "%s: тормозной путь %.1f м", spec.id.c_str(), bd);
        CHECK(!v.upsideDown, "%s перевернулась", spec.id.c_str());
    }
}

static void testCorner(const World& w) {
    printf("== Поворот и занос на площадке фестиваля\n");
    for (const char* id : {"lastochka", "bison", "sirocco", "strela", "medved"}) {
        const CarSpec* s = findCar(id);
        Vehicle v;
        V3 c = w.place("festival")->p;
        v.init(s, V3{-80, c.y, 60}, PI * 0.5f);
        settle(v, w, 1.0f);
        v.in = Controls{};
        float maxLat = 0, spin = 0;
        for (int i = 0; i < (int)(14 / DT); i++) {
            float t = i * DT;
            float target = 45 / 3.6f;
            v.in.throttle = v.speed < target ? 0.7f : 0.15f;
            v.in.steer = t > 3 ? 0.55f : 0;
            v.step(DT, w);
            if (t > 6) {
                // боковое ускорение ≈ скорость × скорость рыскания
                maxLat = std::max(maxLat, std::fabs(v.speed * v.yawRate));
                spin = std::max(spin, std::fabs(v.driftAngle));
            }
        }
        printf("  %-10s боковое %.1f м/с², макс. угол скольжения %.0f°, скорость %.0f км/ч\n", id, maxLat, spin / DEG, v.kmh());
        CHECK(maxLat > 3 && maxLat < 16, "%s: боковое ускорение %.1f", id, maxLat);
        CHECK(spin < 25 * DEG, "%s: разворот в спокойном повороте (%.0f°)", id, spin / DEG);
        // ручник на скорости — машина должна пойти в занос
        v.teleport(V3{-80, c.y, 60}, PI * 0.5f);
        settle(v, w, 0.5f);
        v.in = Controls{};
        float maxDrift = 0;
        for (int i = 0; i < (int)(9 / DT); i++) {
            float t = i * DT;
            v.in.throttle = t < 4 ? 1 : 0.4f;
            if (t > 4 && t < 5.2f) { v.in.handbrake = true; v.in.steer = 1; }
            else { v.in.handbrake = false; v.in.steer = t > 5.2f ? -0.3f : 0; }
            v.step(DT, w);
            if (t > 4) maxDrift = std::max(maxDrift, std::fabs(v.driftAngle));
        }
        printf("             ручник: угол заноса до %.0f°\n", maxDrift / DEG);
        CHECK(maxDrift > 10 * DEG, "%s: ручник не заносит (%.0f°)", id, maxDrift / DEG);
        CHECK(!v.upsideDown, "%s перевернулась после ручника", id);
    }
}

static void testSettleAndDrop(const World& w) {
    printf("== Падение с высоты и стоянка на уклоне\n");
    const CarSpec* s = findCar("vector");
    Vehicle v;
    V3 c = w.place("festival")->p;
    v.init(s, V3{0, c.y + 2.5f, 60}, 0);
    float maxAbsVy = 0;
    for (int i = 0; i < (int)(4 / DT); i++) {
        v.step(DT, w);
        if (i * DT > 2.5f) maxAbsVy = std::max(maxAbsVy, std::fabs(v.vel.y));
    }
    float ride = v.pos.y - w.terrainH(v.pos.x, v.pos.z);
    printf("  после падения: высота ЦМ %.2f м, остаточная верт. скорость %.3f м/с\n", ride, maxAbsVy);
    CHECK(maxAbsVy < 0.05f, "машина не успокоилась после падения");
    CHECK(ride > 0.3f && ride < 0.8f, "странная высота кузова %.2f", ride);
    // стоянка на серпантине с тормозом
    const Road& r = w.roads[w.roadIndex("Серпантин Орлиного пика")];
    float bestGrade = 0, bestS = 0;
    for (size_t i = 5; i + 5 < r.pts.size(); i++) {
        float g = std::fabs(r.pts[i + 5].p.y - r.pts[i - 5].p.y) / 20.0f;
        if (g > bestGrade) { bestGrade = g; bestS = r.pts[i].s; }
    }
    RoadPt p = r.at(bestS);
    v.teleport(p.p, std::atan2(p.t.x, p.t.z));
    settle(v, w, 1.5f);
    V3 p0 = v.pos;
    settle(v, w, 3.0f);
    float creep = distxz(v.pos, p0);
    printf("  на уклоне %.0f%% с тормозом сползло на %.3f м\n", bestGrade * 100, creep);
    CHECK(creep < 0.1f, "машина сползает на тормозе (%.2f м)", creep);
}

static void testObstacles(const World& w) {
    printf("== Столкновение с деревом и трамплин\n");
    // найдём крупное дерево в открытом месте
    int tree = -1;
    for (size_t i = 0; i < w.props.size(); i++) {
        const Prop& p = w.props[i];
        if ((p.kind == PK_OAK || p.kind == PK_PINE) && p.scale > 1.1f && w.cols[p.col].smash == false) {
            // перед деревом 40 м ровного места?
            V3 a = p.pos + V3{-40, 0, 0};
            bool ok = true;
            std::vector<int> ids;
            for (float d = 5; d < 40; d += 4) {
                w.queryCols(p.pos.x - d, p.pos.z, 4, ids);
                for (int id : ids) if (id != p.col) ok = false;
            }
            if (ok && std::fabs(w.terrainH(a.x, a.z) - p.pos.y) < 1.5f && w.terrainN(a.x, a.z).y > 0.97f) { tree = (int)i; break; }
        }
    }
    CHECK(tree >= 0, "не нашёл дерево для теста");
    if (tree >= 0) {
        const Prop& p = w.props[tree];
        Vehicle v;
        v.init(findCar("lastochka"), V3{p.pos.x - 30, w.terrainH(p.pos.x - 30, p.pos.z), p.pos.z}, PI * 0.5f);
        settle(v, w, 0.5f);
        v.vel = v.fwd() * 14.0f;
        for (auto& wh : v.w) wh.omega = 14.0f / wh.radius;
        v.in = Controls{};
        v.in.throttle = 0.3f;
        float maxImpact = 0;
        float minDist = 1e9f;
        for (int i = 0; i < (int)(4 / DT); i++) {
            // рулим точно на ствол
            V3 to = p.pos - v.pos;
            float ang = std::atan2(dot(to, v.left()), dot(to, v.fwd()));
            v.in.steer = clampf(ang * 3.0f, -1, 1);
            v.step(DT, w);
            for (auto& im : v.impacts) maxImpact = std::max(maxImpact, im.speed);
            v.impacts.clear();
            minDist = std::min(minDist, distxz(v.pos, p.pos));
        }
        printf("  удар %.1f м/с, минимальное расстояние до ствола %.2f м\n", maxImpact, minDist);
        CHECK(maxImpact > 6, "удар о дерево не зарегистрирован");
        CHECK(minDist > 1.0f, "машина прошла сквозь дерево");
    }
    // трамплин
    const Ramp& rp = w.ramps[0];
    Vehicle v;
    V3 fwd{rp.sn, 0, rp.cs};
    V3 st = rp.c - fwd * 60.0f;
    st.y = w.terrainH(st.x, st.z);
    v.init(findCar("sirocco"), st, rp.yaw);
    settle(v, w, 0.5f);
    v.in = Controls{};
    v.in.throttle = 1;
    float maxAir = 0, maxH = 0;
    bool upside = false;
    for (int i = 0; i < (int)(9 / DT); i++) {
        v.step(DT, w);
        maxAir = std::max(maxAir, v.airTime);
        maxH = std::max(maxH, v.pos.y - w.terrainH(v.pos.x, v.pos.z));
        if (v.upsideDown) upside = true;
    }
    printf("  трамплин: в воздухе %.2f с, высота %.1f м, перевернулась: %s\n", maxAir, maxH, upside ? "да" : "нет");
    CHECK(maxAir > 0.6f, "трамплин не подбросил машину (%.2f с)", maxAir);
    CHECK(!upside, "машина перевернулась после трамплина");
}

static void testWorld(const World& w) {
    printf("== Мир\n");
    printf("  генерация %.0f мс, дорог %zu, объектов %zu (деревьев %d), зданий %zu, коллайдеров %zu\n", w.genMs, w.roads.size(), w.props.size(), w.treeCount,
           w.buildings.size(), w.cols.size());
    int bad = 0;
    for (auto& r : w.roads)
        for (auto& p : r.pts) {
            if (!finite(p.p)) bad++;
            if (p.p.y < 1.0f) bad++;
        }
    CHECK(bad == 0, "точки дорог ниже воды или NaN: %d", bad);
    CHECK(w.boards.size() == 30, "досок опыта %zu вместо 30", w.boards.size());
    CHECK(w.treeCount > 15000, "мало деревьев: %d", w.treeCount);
    // деревья не растут на дорогах
    int onRoad = 0;
    RoadHit rh;
    for (auto& p : w.props)
        if (p.kind <= PK_PALM && w.roadAt(p.pos.x, p.pos.z, rh, 1.0f)) onRoad++;
    CHECK(onRoad == 0, "деревьев на дорогах: %d", onRoad);
    for (auto& pl : w.places) CHECK(pl.p.y > 0.3f, "место %s под водой", pl.id.c_str());
}


static void testRaces(const World& w, const char* only) {
    printf("== Гонки: автопилот игрока против соперников\n");
    Festival fest;
    fest.build(w);
    printf("  событий %zu, трюков %zu\n", fest.events.size(), fest.stunts.size());
    CHECK(fest.events.size() >= 14, "событий меньше 14: %zu", fest.events.size());
    CHECK(fest.stunts.size() >= 17, "трюков меньше 17: %zu", fest.stunts.size());
    for (auto& e : fest.events) {
        if (only && e.id != only) continue;
        const char* carId = e.type == EV_DIRT || e.type == EV_CROSS ? "sirocco" : (e.type == EV_SHOWCASE ? "strela" : "vector");
        Vehicle player;
        player.id = 1;
        player.init(findCar(carId), e.route.pts[0].p, 0);
        RaceSession rs;
        rs.autodrive = true;
        rs.start(e, w, player, 2, 77);
        float limit = rs.total / 12.0f + 60.0f;  // не медленнее ~43 км/ч в среднем
        int respawns = 0;
        float simT = 0;
        const float dt = 1.0f / 120.0f;
        float lastProg = 0, stallT = 0, maxStall = 0;
        V3 stallPos;
        bool trace = getenv("RACE_TRACE") != nullptr;
        float tracePrint = 0;
        while (simT < limit + 5) {
            if (rs.over) {
                // даём соперникам доехать (до 90 с после финиша игрока)
                bool all = true;
                for (auto& r : rs.racers) all &= r.finished;
                if (all || simT > rs.playerFinishT + 95) break;
            }
            for (int k = 0; k < 4; k++) {
                player.step(dt, w);
                rs.substep(dt, w, player);
                player.smashes.clear(); player.impacts.clear();
                for (auto& r : rs.racers) { r.car.smashes.clear(); r.car.impacts.clear(); }
            }
            if (rs.playerTrack.wantsRespawn) respawns++;
            rs.frame(dt * 4, w, player);
            if (rs.over) rs.over = false, rs.overT = -1e9f;  // продолжаем симуляцию соперников
            simT += dt * 4;
            float pr = rs.playerProgress();
            if (!rs.playerFinished) {
                if (pr - lastProg < 0.5f * 4 * dt * 10) { stallT += dt * 4; if (stallT > maxStall) { maxStall = stallT; stallPos = player.pos; } }
                else stallT = 0;
            }
            lastProg = pr;
            if (trace && simT >= tracePrint) {
                tracePrint += 10;
                printf("    t=%4.0f prog=%6.0f v=%5.1f pos=(%.0f %.0f %.0f) gear=%d lat=%.1f gates=%d wrong=%.1f\n", simT, pr, player.kmh(), player.pos.x, player.pos.y, player.pos.z, player.gear, rs.playerTrack.lateral, rs.gatesPassed, rs.wrongWayT);
            }
        }
        int aiDone = 0;
        for (auto& r : rs.racers) aiDone += r.finished;
        printf("  %-22s %-5s %5.0f м  финиш %s за %5.1f с (%3.0f км/ч), место %d/%d, соперников доехало %d/%zu\n", e.id.c_str(), carId, rs.total,
               rs.playerFinished ? "да " : "НЕТ", rs.playerFinishT, rs.total / std::max(rs.playerFinishT, 1.0f) * 3.6f, rs.playerPlace,
               (int)rs.racers.size() + 1 + (e.type == EV_SHOWCASE), aiDone, rs.racers.size());
        if (maxStall > 8) printf("    остановка %.0f с около (%.0f, %.0f, %.0f)\n", maxStall, stallPos.x, stallPos.y, stallPos.z);
        for (auto& r : rs.racers)
            if (!r.finished)
                printf("    не доехал %s (%s): прогресс %.0f/%.0f у (%.0f, %.0f, %.0f) v=%.0f\n", r.name.c_str(), r.car.spec->id.c_str(), r.ai.progress - e.startS, rs.total, r.car.pos.x,
                       r.car.pos.y, r.car.pos.z, r.car.kmh());
        CHECK(rs.playerFinished, "%s: автопилот не доехал (прогресс %.0f из %.0f, чекпоинтов %d/%zu)", e.id.c_str(), rs.playerProgress(), rs.total, rs.gatesPassed, rs.gates.size());
        CHECK(rs.racers.empty() || aiDone * 2 >= (int)rs.racers.size(), "%s: соперники застряли (%d/%zu)", e.id.c_str(), aiDone, rs.racers.size());
    }
}

int main(int argc, char** argv) {
    World w;
    w.generate(2026);
    bool only = argc > 1;
    auto want = [&](const char* n) { return !only || std::strcmp(argv[1], n) == 0; };
    if (want("world")) testWorld(w);
    if (want("accel")) testAccel(w);
    if (want("corner")) testCorner(w);
    if (want("drop")) testSettleAndDrop(w);
    if (want("obst")) testObstacles(w);
    if (want("races")) testRaces(w, argc > 2 ? argv[2] : nullptr);
    printf(g_fail ? "\nПРОВАЛЕНО проверок: %d\n" : "\nВСЕ ПРОВЕРКИ ПРОЙДЕНЫ\n", g_fail);
    return g_fail ? 1 : 0;
}
