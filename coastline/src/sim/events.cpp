#include "events.h"

#include <algorithm>

namespace cl {

const char* eventTypeName(EventType t) {
    switch (t) {
        case EV_ROAD: return "Шоссейная гонка";
        case EV_DIRT: return "Грунтовая гонка";
        case EV_CROSS: return "Кросс-кантри";
        case EV_DRAG: return "Дрэг-рейсинг";
        case EV_SHOWCASE: return "Шоу";
    }
    return "";
}

const char* stuntTypeName(StuntType t) {
    switch (t) {
        case ST_TRAP: return "Радар";
        case ST_ZONE: return "Зона скорости";
        case ST_DRIFT: return "Дрифт-зона";
        case ST_JUMP: return "Опасный знак";
    }
    return "";
}

const char* StuntDef::unit() const {
    switch (type) {
        case ST_TRAP: case ST_ZONE: return "км/ч";
        case ST_DRIFT: return "очк.";
        case ST_JUMP: return "м";
    }
    return "";
}

int Festival::eventIndex(const std::string& id) const {
    for (size_t i = 0; i < events.size(); i++)
        if (events[i].id == id) return (int)i;
    return -1;
}

void Festival::build(const World& w) {
    events.clear();
    stunts.clear();
    const std::string H = "Прибрежное шоссе";
    auto ev = [&](const char* id, const char* name, const char* desc, EventType type, bool circuit, int laps) {
        EventDef e;
        e.id = id;
        e.name = name;
        e.desc = desc;
        e.type = type;
        e.circuit = circuit;
        e.laps = laps;
        return e;
    };
    auto finish = [&](EventDef e) {
        bool ok;
        if (!e.trail.empty()) {
            const Trail* t = w.trail(e.trail);
            ok = t != nullptr;
            if (ok) buildTrailRoute(w, *t, e.route);
        } else {
            ok = buildRoadRoute(w, e.legs, e.circuit, e.route);
        }
        if (!ok) return;
        // стартовая линия: у кольца — в нуле, у спринта — с запасом под стартовую решётку
        int rows = (e.opponents + 2) / 2;
        e.startS = e.circuit ? 0.0f : std::min(10.0f + rows * 9.0f, e.route.length * 0.2f);
        RoutePt p = e.route.at(e.startS);
        e.marker = p.p;
        e.markerYaw = std::atan2(p.t.x, p.t.z);
        events.push_back(std::move(e));
    };

    EventDef e;
    e = ev("coast_sprint", "Прибрежный спринт", "Северное шоссе, поворот у маяка и вниз вдоль пляжей до Порто-Бриз.", EV_ROAD, false, 1);
    e.legs = {{H, 250, -1070, 1000, 500, +1}};
    e.reward[0] = 32000; finish(e);

    e = ev("fest_ring", "Кольцо фестиваля", "От фестиваля на восток, по шоссе вдоль берега и обратно через северные холмы.", EV_ROAD, true, 2);
    e.legs = {{"Восточная дорога", 60, 0, 1080, -120}, {H, 1080, -120, -160, -880, -1}, {"Северная дорога", -160, -880, 0, -60}};
    e.reward[0] = 45000; e.xp = 5000; finish(e);

    e = ev("hillclimb", "Серпантин Орлиного пика", "Шестнадцать шпилек и двести метров вверх. Тормозите заранее.", EV_ROAD, false, 1);
    e.legs = {{"Серпантин Орлиного пика", -500, -620, -958, -958}};
    e.opponents = 5; e.reward[0] = 38000; finish(e);

    e = ev("town_lights", "Огни Порто-Бриз", "Три круга по улицам приморского городка.", EV_ROAD, true, 3);
    e.legs = {{"ул. Гаванская", 640, 660, 880, 660}, {"пр. Портовый", 880, 660, 880, 900}, {"ул. Морская", 880, 900, 640, 900}, {"пр. Маяковый", 640, 900, 640, 660}};
    e.reward[0] = 34000; finish(e);

    e = ev("grand_loop", "Большая петля", "Полный круг по прибрежному шоссе: пляжи, город, холмы и горы.", EV_ROAD, true, 1);
    e.legs = {{H, 1080, -120, 1080, -120, +1}};
    e.reward[0] = 60000; e.xp = 7000; finish(e);

    e = ev("drag", "Дрэг на взлётной полосе", "Восемьсот метров по прямой. Всё решает старт.", EV_DRAG, false, 1);
    e.legs = {{"Взлётная полоса", 190, -560, 1030, -560}};
    e.opponents = 3; e.reward[0] = 20000; e.reward[1] = 12000; e.reward[2] = 8000; e.xp = 2000; finish(e);

    e = ev("forest_ring", "Лесная тропа", "Два круга по грунтовке через сосновый лес.", EV_DIRT, true, 2);
    e.legs = {{"Лесная тропа", -300, -60, -300, -60, +1}};
    e.reward[0] = 36000; finish(e);

    e = ev("farm_rally", "Фермерское ралли", "Грунтовка вдоль полей и заборов до западного шоссе.", EV_DIRT, false, 1);
    e.legs = {{"Фермерская грунтовка", 60, 760, -940, 995}};
    e.reward[0] = 30000; finish(e);

    e = ev("dunes", "Дюны", "Песчаная трасса вдоль океана.", EV_DIRT, false, 1);
    e.legs = {{"Дюны", 1085, -500, 1070, 150}};
    e.opponents = 5; e.reward[0] = 26000; finish(e);

    e = ev("mountain_dirt", "Горная грунтовка", "Подъём по каменистой дороге на западный склон.", EV_DIRT, false, 1);
    e.legs = {{"Горная грунтовка", -790, -300, -1150, -780}};
    e.opponents = 5; e.reward[0] = 30000; finish(e);

    e = ev("cross_hills", "Через холмы", "Напрямик от фестиваля к подножию Орлиного пика.", EV_CROSS, false, 1);
    e.trail = "hills"; e.opponents = 5; e.reward[0] = 34000; finish(e);

    e = ev("cross_river", "Речная долина", "Вдоль реки с двумя бродами. Брызги гарантированы.", EV_CROSS, false, 1);
    e.trail = "river"; e.opponents = 5; e.reward[0] = 36000; finish(e);

    e = ev("cross_descent", "Спуск с пика", "С вершины через склоны и луга к озеру Зеркальное.", EV_CROSS, false, 1);
    e.trail = "descent"; e.opponents = 5; e.reward[0] = 40000; finish(e);

    e = ev("showcase_sky", "Финал: Небесный вызов", "Обгоните пилотажный самолёт от аэродрома до сцены фестиваля.", EV_SHOWCASE, false, 1);
    e.trail = "sky"; e.opponents = 0; e.unlockAfter = 6; e.reward[0] = 150000; e.reward[1] = 0; e.reward[2] = 0; e.xp = 15000; finish(e);

    // --- трюки
    auto roadS = [&](const std::string& road, float x, float z, int& ri) {
        ri = w.roadIndex(road);
        if (ri < 0) return 0.0f;
        const Road& r = w.roads[ri];
        float bd = 1e18f, bs = 0;
        for (auto& p : r.pts) {
            float d = sq(p.p.x - x) + sq(p.p.z - z);
            if (d < bd) { bd = d; bs = p.s; }
        }
        return bs;
    };
    auto stunt = [&](const char* id, const char* name, StuntType t, const std::string& road, float x0, float z0, float x1, float z1, float a, float b, float c) {
        StuntDef s;
        s.id = id;
        s.name = name;
        s.type = t;
        int r0;
        s.s0 = roadS(road, x0, z0, r0);
        s.road = r0;
        int r1;
        s.s1 = t == ST_TRAP ? s.s0 : roadS(road, x1, z1, r1);
        if (s.road < 0) return;
        s.stars[0] = a; s.stars[1] = b; s.stars[2] = c;
        RoadPt p = w.roads[s.road].at(s.s0);
        s.pos = p.p;
        s.yaw = std::atan2(p.t.x, p.t.z);
        stunts.push_back(s);
    };
    stunt("trap_coast", "Радар «Прибрежный»", ST_TRAP, H, 1085, -300, 0, 0, 160, 190, 220);
    stunt("trap_runway", "Радар «Взлётка»", ST_TRAP, "Взлётная полоса", 900, -560, 0, 0, 200, 240, 280);
    stunt("trap_east", "Радар «Восточный»", ST_TRAP, "Восточная дорога", 450, -140, 0, 0, 150, 175, 200);
    stunt("trap_south", "Радар «Южный берег»", ST_TRAP, H, -120, 1231, 0, 0, 170, 200, 230);
    stunt("trap_west", "Радар «Западный»", ST_TRAP, "Западная дорога", -850, 0, 0, 0, 140, 165, 190);
    stunt("zone_north", "Зона «Северное шоссе»", ST_ZONE, H, -160, -880, 250, -1070, 150, 175, 200);
    stunt("zone_south", "Зона «Южная прямая»", ST_ZONE, "Южная дорога", -40, 300, 60, 760, 130, 155, 180);
    stunt("zone_west", "Зона «Западный берег»", ST_ZONE, H, -1215, 520, -1215, 120, 150, 180, 210);
    {
        int ri = w.roadIndex("Серпантин Орлиного пика");
        if (ri >= 0) {
            const Road& r = w.roads[ri];
            StuntDef s;
            s.id = "zone_serp"; s.name = "Зона «Нижние шпильки»"; s.type = ST_ZONE; s.road = ri; s.s0 = 20; s.s1 = std::min(700.0f, r.length * 0.35f);
            s.stars[0] = 65; s.stars[1] = 80; s.stars[2] = 95;
            RoadPt p = r.at(s.s0); s.pos = p.p; s.yaw = std::atan2(p.t.x, p.t.z);
            stunts.push_back(s);
            StuntDef d;
            d.id = "drift_serp"; d.name = "Дрифт «Шпильки»"; d.type = ST_DRIFT; d.road = ri; d.s0 = r.length * 0.45f; d.s1 = r.length * 0.85f;
            d.stars[0] = 12000; d.stars[1] = 26000; d.stars[2] = 45000;
            RoadPt q = r.at(d.s0); d.pos = q.p; d.yaw = std::atan2(q.t.x, q.t.z);
            stunts.push_back(d);
        }
    }
    stunt("drift_town", "Дрифт «Морская»", ST_DRIFT, "ул. Морская", 580, 900, 1040, 900, 6000, 14000, 24000);
    stunt("drift_forest", "Дрифт «Лесной круг»", ST_DRIFT, "Лесная тропа", -330, -290, -790, -300, 9000, 20000, 34000);
    stunt("drift_north", "Дрифт «Северный»", ST_DRIFT, "Северная дорога", 60, -320, -100, -800, 8000, 17000, 30000);
    const char* jumpNames[] = {"Опасный знак «Фестиваль»", "Опасный знак «Через реку»", "Опасный знак «Ферма»", "Опасный знак «Дюны»", "Опасный знак «Ветряки»"};
    for (int i = 0; i < (int)w.ramps.size() && i < 5; i++) {
        StuntDef s;
        s.id = "jump" + std::to_string(i);
        s.name = jumpNames[i];
        s.type = ST_JUMP;
        s.ramp = i;
        s.stars[0] = 35; s.stars[1] = 55; s.stars[2] = 75;
        s.pos = w.ramps[i].c;
        s.yaw = w.ramps[i].yaw;
        stunts.push_back(s);
    }
}

// ---------------------------------------------------------------- гонка
static const char* kDriverNames[] = {"Алекс «Бриз» Кравец", "Марина Солнцева", "Тимур Ветров", "Ева «Клык» Остин", "Даниил Прибой", "Ника Рей",
                                     "Лео Мартинес", "Кира «Дрифт» Ли", "Ян Громов", "Софи Бланш", "Рустам Шторм", "Оля Кобальт"};

void RaceSession::start(const EventDef& e, const World& w, Vehicle& player, int diff, uint32_t seed) {
    ev = &e;
    difficulty = diff;
    racers.clear();
    Rng rng(seed);
    const Route& R = e.route;
    int n = e.opponents;
    total = e.circuit ? R.length * e.laps : R.length - e.startS - 6.0f;
    // стартовая решётка: игрок в последнем ряду
    auto gridPos = [&](int slot, V3& p, float& yaw) {
        int row = slot / 2, col = slot % 2;
        float s = e.startS - 6.0f - row * 9.0f;
        RoutePt q = R.at(s);
        float half = std::min(q.halfW, 3.2f);
        float lat = (col == 0 ? 1.0f : -1.0f) * half * 0.62f;
        if (e.type == EV_DRAG) { s = e.startS - 6.0f; q = R.at(s); lat = (slot - 1.5f) * 5.0f; }
        p = q.p + q.l * lat;
        p.y = w.ground(p.x, p.z, q.p.y + 3).h;
        yaw = std::atan2(q.t.x, q.t.z);
    };
    // соперники одного класса с игроком
    const auto& cat = carCatalog();
    int ppi = player.spec->pi;
    std::vector<const CarSpec*> pool;
    for (int widen = 50; pool.size() < 3 && widen < 1000; widen += 50) {
        pool.clear();
        for (auto& c : cat)
            if (std::abs(c.pi - ppi) <= widen) pool.push_back(&c);
    }
    static const float skills[] = {0.8f, 0.88f, 0.94f, 0.99f};
    for (int i = 0; i < n; i++) {
        Racer r;
        const CarSpec* spec = pool[rng.irange(0, (int)pool.size() - 1)];
        if (e.type == EV_DIRT || e.type == EV_CROSS) {
            // на грунте чаще раллийные и внедорожные машины
            for (int tries = 0; tries < 4; tries++) {
                const CarSpec* c = pool[rng.irange(0, (int)pool.size() - 1)];
                if (c->looseGrip > 1.0f) { spec = c; break; }
            }
        }
        V3 p;
        float yaw;
        gridPos(i, p, yaw);
        r.car.id = 10 + i;
        r.car.ai = true;
        r.car.init(spec, p, yaw);
        uint32_t palette[] = {0xff2030e0, 0xffe0a020, 0xff30c050, 0xfff0f0f0, 0xff202020, 0xff00a0ff, 0xffc040c0, 0xff3060ff, 0xff808080, 0xff40e0e0};
        r.car.color = palette[(seed + i * 3) % 10];
        r.name = kDriverNames[(seed + i * 5) % 12];
        r.baseSkill = skills[std::clamp(diff, 0, 3)] * rng.range(0.96f, 1.03f);
        r.ai.skill = r.baseSkill;
        r.ai.offset = rng.range(-1.0f, 1.0f);
        r.ai.reset(&R, r.car, e.startS - 6.0f - (i / 2) * 9.0f);
        r.car.gripScale = 1.03f;
        racers.push_back(std::move(r));
    }
    V3 p;
    float yaw;
    gridPos(n, p, yaw);
    player.teleport(p, yaw);
    playerTrack.reset(&R, player, e.startS - 6.0f - (n / 2) * 9.0f);
    playerTrack.skill = 0.97f;
    t = -3.5f;
    gates.clear();
    float spacing = e.type == EV_CROSS || e.type == EV_SHOWCASE ? 120.0f : 180.0f;
    for (float s = spacing; s < total - 10; s += spacing) gates.push_back(s);
    gates.push_back(total);
    gatesPassed = 0;
    lap = 1;
    lapStart = 0;
    lastLap = bestLap = 0;
    playerFinished = false;
    playerPlace = 0;
    over = false;
    overT = 0;
    wrongWayT = 0;
    missedGate = false;
    overtakes = 0;
    planeS = 0;
    if (e.type == EV_SHOWCASE) {
        // время самолёта — чуть быстрее хорошего заезда на машине игрока
        AIDriver est;
        est.reset(&R, player, 0);
        float tt = 0;
        for (float s = 0; s < R.length; s += 4) tt += 4 / std::max(8.0f, est.targetSpeedAt(s) * 0.93f);
        planeTotalT = tt * (diff >= 2 ? 0.93f : diff == 1 ? 1.0f : 1.1f) + 4.0f;
    }
    lastPosition_ = position();
}

void RaceSession::respawnRacer(Racer& r, const World& w) {
    const Route& R = ev->route;
    float s = r.ai.localS() - 8.0f;
    RoutePt q = R.at(s);
    V3 p = q.p + q.l * clampf(r.ai.offset, -q.halfW, q.halfW);
    p.y = w.ground(p.x, p.z, q.p.y + 3).h;
    r.car.teleport(p, std::atan2(q.t.x, q.t.z));
    float v = std::max(8.0f, r.ai.targetSpeedAt(s) * 0.5f);
    r.car.vel = V3{q.t.x, 0, q.t.z} * v;
    for (auto& wh : r.car.w) wh.omega = v / wh.radius;
    r.ai.wantsRespawn = false;
}

void RaceSession::respawnPlayer(Vehicle& player, const World& w) {
    const Route& R = ev->route;
    float s = playerTrack.localS() - 4.0f;
    RoutePt q = R.at(s);
    V3 p = q.p;
    p.y = w.ground(p.x, p.z, q.p.y + 3).h;
    player.teleport(p, std::atan2(q.t.x, q.t.z));
}

void RaceSession::substep(float dt, const World& w, Vehicle& player) {
    std::vector<const Vehicle*> all;
    all.reserve(racers.size() + 1);
    all.push_back(&player);
    for (auto& r : racers) all.push_back(&r.car);
    bool hold = t < 0;
    for (auto& r : racers) {
        if (hold) {
            r.car.in = Controls{};
            r.car.in.brake = 1;
            r.car.as.autoGear = false;
            r.car.gear = 1;
        } else if (r.finished) {
            Controls c = r.ai.drive(r.car, dt, all);
            c.throttle = 0;
            c.brake = r.car.speed > 3 ? 0.5f : 1.0f;
            r.car.in = c;
        } else {
            r.car.as.autoGear = true;
            // подтягивание: отстающие чуть быстрее, лидеры чуть медленнее
            float diff = r.ai.progress - playerTrack.progress;
            float k = 1.0f;
            if (diff > 60) k = mixf(1.0f, 0.93f, clamp01((diff - 60) / 220));
            else if (diff < -60) k = mixf(1.0f, 1.06f, clamp01((-diff - 60) / 220));
            r.ai.skill = r.baseSkill * k;
            r.car.in = r.ai.drive(r.car, dt, all);
        }
        r.car.step(dt, w);
    }
    // столкновения между машинами
    for (size_t i = 0; i < racers.size(); i++) {
        collideCars(player, racers[i].car);
        for (size_t j = i + 1; j < racers.size(); j++) collideCars(racers[i].car, racers[j].car);
    }
    if (hold) {
        player.in.throttle = std::min(player.in.throttle, 1.0f);
        player.in.brake = 1;
        player.as.autoGear = false;
        player.gear = 1;
    } else {
        player.as.autoGear = playerAutoGear;
    }
}

void RaceSession::frame(float dt, const World& w, Vehicle& player) {
    const Route& R = ev->route;
    float prevT = t;
    t += dt;
    // отслеживание игрока и автопилот
    Controls ac = playerTrack.drive(player, dt, {});
    if (autodrive && t > 0) player.in = ac;
    if (prevT < 0 && t >= 0) { lapStart = 0; }
    if (t < 0) return;
    for (auto& r : racers) {
        if (r.ai.wantsRespawn) respawnRacer(r, w);
        if (!r.finished && r.ai.progress - ev->startS >= total) {
            r.finished = true;
            r.finishT = t;
        }
    }
    if (playerTrack.wantsRespawn && autodrive) {
        respawnPlayer(player, w);
        playerTrack.wantsRespawn = false;
    }
    playerTrack.wantsRespawn = false;
    float prog = playerProgress();
    // чекпоинты по порядку
    if (!playerFinished && gatesPassed < (int)gates.size()) {
        float g = gates[gatesPassed];
        float lim = (ev->type == EV_CROSS || ev->type == EV_SHOWCASE) ? 22.0f : R.at(ev->startS + g).halfW + 7.0f;
        if (prog >= g - 2.0f) {
            if (std::fabs(playerTrack.lateral) < lim || prog >= total - 1) {
                gatesPassed++;
                missedGate = false;
            } else if (prog > g + 25) {
                missedGate = true;
            }
        }
    }
    // круги
    if (ev->circuit) {
        int lp = std::clamp((int)(prog / R.length) + 1, 1, ev->laps);
        if (lp > lap) {
            lastLap = t - lapStart;
            if (bestLap == 0 || lastLap < bestLap) bestLap = lastLap;
            lapStart = t;
            lap = lp;
        }
    }
    // разворот
    V3 dir = R.at(playerTrack.localS()).t;
    if (player.speed > 5 && dot(player.vel, dir) < -3) wrongWayT += dt;
    else wrongWayT = 0;
    if (!playerFinished && gatesPassed >= (int)gates.size()) {
        playerFinished = true;
        playerFinishT = t;
        if (ev->circuit) {
            lastLap = t - lapStart;
            if (bestLap == 0 || lastLap < bestLap) bestLap = lastLap;
        }
        playerPlace = position();
    }
    // самолёт в шоу
    if (ev->type == EV_SHOWCASE) {
        float k = clamp01(t / planeTotalT);
        planeS = R.length * (k * k * (3 - 2 * k) * 0.25f + k * 0.75f);
    }
    int pos = position();
    if (pos < lastPosition_) overtakes += lastPosition_ - pos;
    lastPosition_ = pos;
    if (playerFinished) {
        overT += dt;
        bool allDone = true;
        for (auto& r : racers) allDone &= r.finished;
        if (overT > 4.0f || allDone) over = true;
    }
}

std::vector<int> RaceSession::standings() const {
    struct E { int id; bool fin; float ft; float prog; };
    std::vector<E> v;
    float pp = playerFinished ? total + 1000 : playerTrack.progress - ev->startS;
    v.push_back({-1, playerFinished, playerFinishT, pp});
    for (size_t i = 0; i < racers.size(); i++) {
        const Racer& r = racers[i];
        v.push_back({(int)i, r.finished, r.finishT, r.ai.progress - ev->startS});
    }
    if (ev->type == EV_SHOWCASE) {
        bool planeFin = planeS >= ev->route.length - 1;
        v.push_back({-2, planeFin, planeTotalT, planeS - ev->startS});
    }
    std::sort(v.begin(), v.end(), [](const E& a, const E& b) {
        if (a.fin != b.fin) return a.fin;
        if (a.fin) return a.ft < b.ft;
        return a.prog > b.prog;
    });
    std::vector<int> out;
    for (auto& e : v) out.push_back(e.id);
    return out;
}

int RaceSession::position() const {
    auto s = standings();
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] == -1) return (int)i + 1;
    return 1;
}

V3 RaceSession::nextGatePos() const {
    float g = gatesPassed < (int)gates.size() ? gates[gatesPassed] : total;
    return ev->route.at(ev->startS + g).p;
}

// ---------------------------------------------------------------- трюки
static int starsFor(const StuntDef& s, float v) {
    int st = 0;
    for (int i = 0; i < 3; i++)
        if (v >= s.stars[i]) st = i + 1;
    return st;
}

void StuntTracker::update(float dt, const World& w, const Festival& f, const Vehicle& v, float driftPts, std::vector<StuntResult>& out) {
    RoadHit rh;
    bool onRoad = w.roadAt(v.pos.x, v.pos.z, rh, 2.0f);
    int road = onRoad ? rh.road : -1;
    float s = onRoad ? rh.s : -1;
    status.clear();
    // радары: пересечение линии
    if (onRoad && road == lastRoad && lastRoadS >= 0) {
        for (size_t i = 0; i < f.stunts.size(); i++) {
            const StuntDef& st = f.stunts[i];
            if (st.type != ST_TRAP || st.road != road) continue;
            if ((lastRoadS - st.s0) * (s - st.s0) <= 0 && std::fabs(s - lastRoadS) < 30) {
                StuntResult r;
                r.stunt = (int)i;
                r.value = v.kmh();
                r.stars = starsFor(st, r.value);
                out.push_back(r);
            }
        }
    }
    // зоны скорости и дрифта
    if (activeZone < 0 && onRoad && road == lastRoad && lastRoadS >= 0) {
        for (size_t i = 0; i < f.stunts.size(); i++) {
            const StuntDef& st = f.stunts[i];
            if ((st.type != ST_ZONE && st.type != ST_DRIFT) || st.road != road) continue;
            bool enterA = (lastRoadS - st.s0) * (s - st.s0) <= 0 && s > lastRoadS;
            bool enterB = (lastRoadS - st.s1) * (s - st.s1) <= 0 && s < lastRoadS;
            if ((enterA || enterB) && std::fabs(s - lastRoadS) < 30) {
                activeZone = (int)i;
                zoneT = 0;
                zoneScore = 0;
                zoneDist = 0;
                break;
            }
        }
    } else if (activeZone >= 0) {
        const StuntDef& st = f.stunts[activeZone];
        zoneT += dt;
        if (st.type == ST_DRIFT) zoneScore += driftPts;
        float lo = std::min(st.s0, st.s1), hi = std::max(st.s0, st.s1);
        bool offRoad = !onRoad || road != st.road;
        if (offRoad) zoneDist += dt;
        else zoneDist = 0;
        if (zoneDist > 2.0f || zoneT > 240) {
            activeZone = -1;
        } else if (onRoad && road == st.road && (s < lo - 3 || s > hi + 3)) {
            StuntResult r;
            r.stunt = activeZone;
            r.value = st.type == ST_ZONE ? (hi - lo) / std::max(zoneT, 0.1f) * 3.6f : zoneScore;
            r.stars = starsFor(st, r.value);
            out.push_back(r);
            activeZone = -1;
        } else {
            status = st.name;
            statusValue = st.type == ST_ZONE ? std::min(v.kmh(), 999.0f) : zoneScore;
        }
    }
    lastRoad = road;
    lastRoadS = s;
    // прыжки с трамплинов
    for (size_t i = 0; i < f.stunts.size(); i++) {
        const StuntDef& st = f.stunts[i];
        if (st.type != ST_JUMP) continue;
        float h;
        if (w.ramps[st.ramp].contains(v.pos.x, v.pos.z, h) && v.wheelsOnGround > 0) {
            jumpRamp = (int)i;
            airborne = false;
        }
    }
    if (jumpRamp >= 0) {
        if (!airborne && v.wheelsOnGround == 0) {
            float h;
            if (!w.ramps[f.stunts[jumpRamp].ramp].contains(v.pos.x, v.pos.z, h)) {
                airborne = true;
                takeoff = v.pos;
            }
        } else if (airborne && v.wheelsOnGround >= 2) {
            float d = distxz(v.pos, takeoff);
            StuntResult r;
            r.stunt = jumpRamp;
            r.value = d;
            r.stars = starsFor(f.stunts[jumpRamp], d);
            if (d > 8) out.push_back(r);
            jumpRamp = -1;
            airborne = false;
        } else if (!airborne && v.wheelsOnGround > 0) {
            float h;
            if (!w.ramps[f.stunts[jumpRamp].ramp].contains(v.pos.x, v.pos.z, h) && distxz(v.pos, w.ramps[f.stunts[jumpRamp].ramp].c) > 20) jumpRamp = -1;
        }
    }
}

}  // namespace cl
