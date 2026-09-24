#include "game.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "rlgl.h"

#if defined(PLATFORM_WEB)
#include <emscripten.h>
EM_JS(void, js_save, (const char* s), {
    try { localStorage.setItem('coastline_save', UTF8ToString(s)); } catch (e) {}
});
EM_JS(char*, js_load, (), {
    var s = null;
    try { s = localStorage.getItem('coastline_save'); } catch (e) {}
    if (!s) return 0;
    var n = lengthBytesUTF8(s) + 1;
    var p = _malloc(n);
    stringToUTF8(s, p, n);
    return p;
});
#endif

namespace cl {

Game* g_game = nullptr;

static const char* kTrafficNames[] = {"Drivatar Лёха", "Drivatar Настя", "Drivatar Гоша", "Drivatar Вика", "Drivatar Макс", "Drivatar Дина", "Drivatar Фёдор", "Drivatar Аля", "Drivatar Рома"};

// ------------------------------------------------------------------ запуск
void Game::parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--script" && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            std::string line;
            while (std::getline(f, line))
                if (!line.empty() && line[0] != '#') script.push_back(line);
        } else if (a == "--fixed") fixedStep = true;
        else if (a == "--nosave") noSave = true;
        else if (a == "--save" && i + 1 < argc) savePath = argv[++i];
        else if (a == "--cmd" && i + 1 < argc) script.push_back(argv[++i]);
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &winW, &winH);
    }
}

void Game::init() {
    g_game = this;
    ui.init();
    audio.init();
    load();
    applySettings();
    state = GS_BOOT;
}

void Game::shutdown() {
    if (booted_) {
        save();
        rend.unload();
        UnloadTexture(mapTex);
    }
    audio.shutdown();
    ui.unload();
}

void Game::applySettings() {
    Settings& s = prof.settings;
    audio.setVolumes(s.master, s.music, s.sfx, s.music_on);
    rig.mode = std::clamp(s.camera, 0, CAM_COUNT - 1);
    fx.quality = s.quality == 0 ? 0.5f : s.quality == 1 ? 1.0f : 1.4f;
}

void Game::updateBoot() {
    // тяжёлые шаги по одному за кадр, чтобы экран загрузки успевал обновляться
    switch (bootStep_) {
        case 0: bootMsg_ = "Создаём побережье…"; break;
        case 1:
            world.generate(2026);
            bootMsg_ = "Размечаем трассы фестиваля…";
            break;
        case 2:
            fest.build(world);
            bootMsg_ = "Строим город и лес…";
            break;
        case 3:
            rend.init(world, ui.font(F_DISPLAY, 96), prof.settings.quality, prof.settings.shadows);
            bootMsg_ = "Рисуем карту…";
            break;
        case 4: {
            Image im = buildMapImage(world, 1024);
            mapTex = LoadTextureFromImage(im);
            GenTextureMipmaps(&mapTex);
            SetTextureFilter(mapTex, TEXTURE_FILTER_TRILINEAR);
            UnloadImage(im);
            bootMsg_ = "Заводим моторы…";
            break;
        }
        case 5: {
            // маршруты для машин в свободной езде
            int hr = world.roadIndex("Прибрежное шоссе");
            if (hr >= 0) {
                const Road& r = world.roads[hr];
                for (int dir : {1, -1}) {
                    Route rt;
                    std::vector<Leg> legs = {Leg{r.name, r.pts[0].p.x, r.pts[0].p.z, r.pts[0].p.x, r.pts[0].p.z, dir}};
                    if (buildRoadRoute(world, legs, true, rt)) trafficRoutes.push_back(std::move(rt));
                }
            }
            int fr = world.roadIndex("Лесная тропа");
            if (fr >= 0) {
                const Road& r = world.roads[fr];
                Route rt;
                std::vector<Leg> legs = {Leg{r.name, r.pts[0].p.x, r.pts[0].p.z, r.pts[0].p.x, r.pts[0].p.z, 1}};
                if (buildRoadRoute(world, legs, true, rt)) trafficRoutes.push_back(std::move(rt));
            }
            // разбитые доски опыта остаются разбитыми
            for (int b : prof.boards)
                if (b >= 0 && b < (int)world.boards.size()) world.props[world.boards[b]].alive = false;
            const Place* fp = world.place("festival");
            V3 p = fp ? fp->p : V3{0, 0, 0};
            const CarSpec* spec = currentSpec();
            player.init(spec ? spec : findCar("lastochka"), V3{p.x + 20, world.terrainH(p.x + 20, p.z + 30), p.z + 30}, 0);
            if (!prof.garage.empty()) player.color = prof.garage[std::clamp(prof.current, 0, (int)prof.garage.size() - 1)].color;
            rig.reset(player);
            bootMsg_ = "Готово";
            break;
        }
        default:
            booted_ = true;
            enterState(GS_TITLE);
            if (!startCmd.empty()) command(startCmd);
            return;
    }
    bootStep_++;
    bootProgress_ = bootStep_ / 6.0f;
}

const CarSpec* Game::currentSpec() const {
    if (prof.garage.empty()) return findCar("lastochka");
    int i = std::clamp(prof.current, 0, (int)prof.garage.size() - 1);
    const CarSpec* s = findCar(prof.garage[i].id);
    return s ? s : findCar("lastochka");
}

void Game::setPlayerCar(const std::string& id, uint32_t color, bool keepPos) {
    const CarSpec* s = findCar(id);
    if (!s) return;
    V3 p = player.pos - V3{0, player.spec ? player.spec->cgH : 0.5f, 0};
    float yaw = player.spec ? player.yaw() : 0;
    if (!keepPos) {
        const Place* fp = world.place("festival");
        p = fp ? fp->p : V3{0, 0, 0};
    }
    GroundHit g = world.ground(p.x, p.z, p.y + 2);
    player.init(s, V3{p.x, g.h, p.z}, yaw);
    player.color = color;
    rig.reset(player);
    fx.clear();
    rewind.clear();
}

void Game::teleportPlayer(V3 p, float yaw) {
    GroundHit g = world.ground(p.x, p.z, p.y + 5);
    player.teleport(V3{p.x, g.h, p.z}, yaw);
    rig.reset(player);
    fx.clear();
    rewind.clear();
    skills.reset();
}

void Game::resetToRoad() {
    if (state == GS_RACE && raceEvent >= 0) {
        race.respawnPlayer(player, world);
        rig.reset(player);
        rewind.clear();
        return;
    }
    RoadHit rh;
    if (world.nearestRoad(player.pos.x, player.pos.z, 400, rh)) {
        const Road& r = world.roads[rh.road];
        RoadPt p = r.at(rh.s);
        float yaw = std::atan2(p.t.x, p.t.z);
        if (dot(player.fwd(), p.t) < 0) yaw += PI;
        V3 q = p.p + p.l * (dot(player.fwd(), p.t) < 0 ? r.halfW() * 0.5f : -r.halfW() * 0.5f);
        q.y = p.p.y;
        player.teleport(q, yaw);
    } else {
        player.teleport(player.pos - V3{0, player.spec->cgH, 0} + V3{0, 1, 0}, player.yaw());
    }
    rig.reset(player);
    rewind.clear();
}

void Game::newGame(const std::string& starter) {
    Settings keep = prof.settings;
    prof = Profile{};
    prof.settings = keep;
    prof.started = true;
    prof.credits = 20000;
    prof.wheelspins = 1;
    const CarSpec* s = findCar(starter);
    prof.garage.push_back({starter, s ? s->color : 0xff3050e0});
    prof.current = 0;
    prof.timeOfDay = 10.0f;
    for (auto& p : world.props) p.alive = true;
    setPlayerCar(starter, prof.garage[0].color, false);
    const Place* fp = world.place("festival");
    if (fp) teleportPlayer(fp->p + V3{18, 0, 26}, 0.7f);
    save();
}

// ------------------------------------------------------------------ сохранение
void Game::save() {
    if (noSave || !prof.started) return;
    std::string s = prof.serialize();
#if defined(PLATFORM_WEB)
    js_save(s.c_str());
#else
    std::ofstream f(savePath, std::ios::binary);
    f << s;
#endif
}

void Game::load() {
    if (noSave) return;
    std::string s;
#if defined(PLATFORM_WEB)
    char* p = js_load();
    if (p) {
        s = p;
        free(p);
    }
#else
    std::ifstream f(savePath, std::ios::binary);
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        s = ss.str();
    }
#endif
    if (!s.empty()) {
        Profile p;
        if (p.parse(s)) prof = p;
    }
}

// ------------------------------------------------------------------ прогресс
void Game::addXP(int xp, const std::string& why) {
    (void)why;
    if (xp <= 0) return;
    int lv = prof.addXP(xp);
    if (lv > 0) {
        // вращение за уровень начисляет сам профиль (Profile::addXP)
        levelUps += lv;
        ui.toast("Новый уровень " + std::to_string(prof.level) + "!", "Получено вращение колеса удачи", pal::yellow, 4.0f);
        audio.play(SFX_LEVEL);
    }
}

void Game::addCredits(int cr) { prof.credits = std::max(0, prof.credits + cr); }

void Game::enterState(GameState s) {
    if (s == GS_PAUSE && state != GS_PAUSE) pauseFrom = state;
    state = s;
    stateT = 0;
    sel = 0;
    if (s == GS_PAUSE) {
        tabSel = 0;
        confirmReset = false;
    }
    if (s == GS_DRIVE && traffic.empty()) spawnTraffic();
    audio.setMusicIntensity(s == GS_RACE ? 1.0f : 0.0f);
}

// ------------------------------------------------------------------ трафик
void Game::spawnTraffic() {
    traffic.clear();
    if (trafficRoutes.empty()) return;
    std::vector<const CarSpec*> pool;
    for (auto& c : carCatalog())
        if (c.pi < 760) pool.push_back(&c);
    Rng rng((uint32_t)(simTime * 7) + 99);
    uint32_t colors[] = {0xff2030e0, 0xffe0a020, 0xff30c050, 0xfff0f0f0, 0xff202020, 0xff00a0ff, 0xffc040c0, 0xff3060ff, 0xff808080, 0xff40e0e0};
    int counts[3] = {4, 3, 2};
    int n = 0;
    for (size_t ri = 0; ri < trafficRoutes.size() && ri < 3; ri++) {
        const Route& R = trafficRoutes[ri];
        for (int k = 0; k < counts[ri]; k++) {
            TrafficCar t;
            t.route = (int)ri;
            float s = R.length * (k + rng.range(0.1f, 0.6f)) / counts[ri];
            RoutePt q = R.at(s);
            const CarSpec* spec = pool[rng.irange(0, (int)pool.size() - 1)];
            if (ri == 2) spec = findCar(rng.chance(0.5f) ? "medved" : "sirocco");
            V3 p = q.p + q.l * (-q.halfW * 0.45f);
            p.y = world.ground(p.x, p.z, q.p.y + 3).h;
            t.car.id = 100 + n;
            t.car.ai = true;
            t.car.init(spec, p, std::atan2(q.t.x, q.t.z));
            t.car.color = colors[rng.irange(0, 9)];
            t.ai.skill = ri == 2 ? 0.62f : rng.range(0.66f, 0.8f);
            t.ai.offset = -q.halfW * 0.45f;
            t.ai.reset(&R, t.car, s);
            t.name = kTrafficNames[n % 9];
            traffic.push_back(std::move(t));
            n++;
        }
    }
}

void Game::clearTraffic() { traffic.clear(); }

void Game::updateTraffic(float dt) {
    // дальние машины не моделируются физикой — просто едут по маршруту
    for (auto& t : traffic) {
        const Route& R = trafficRoutes[t.route];
        float d = len(t.car.pos - player.pos);
        bool far = d > 420.0f;
        if (far) {
            float v = std::max(8.0f, t.ai.targetSpeedAt(t.ai.localS()) * 0.85f);
            float s = t.ai.localS() + v * dt;
            if (s >= R.length) s -= R.length;
            RoutePt q = R.at(s);
            V3 p = q.p + q.l * t.ai.offset;
            p.y = q.p.y;
            if (!t.frozen || frameNo % 15 == 0) {
                t.car.teleport(p, std::atan2(q.t.x, q.t.z));
                t.car.vel = V3{q.t.x, 0, q.t.z} * v;
                for (auto& wh : t.car.w) wh.omega = v / wh.radius;
            }
            t.ai.reset(&R, t.car, s);
            t.frozen = true;
        } else t.frozen = false;
    }
}

// ------------------------------------------------------------------ ввод
void Game::readControls(float dt, Controls& c) {
    c = Controls{};
    if (forcedFrames > 0) {
        forcedFrames--;
        c = forced;
        return;
    }
    float kmh = player.kmh();
    bool thr = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
    bool brk = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
    float st = ((IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) ? 1.0f : 0.0f) - ((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) ? 1.0f : 0.0f);
    c.throttle = thr ? 1.0f : 0.0f;
    c.brake = brk ? 1.0f : 0.0f;
    c.handbrake = IsKeyDown(KEY_SPACE);
    float padSteer = 0;
    bool pad = false;
    if (IsGamepadAvailable(0)) {
        float rt = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER), lt = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER);
        // курки: от −1 (отпущен) до 1
        float r = clamp01((rt + 1) * 0.5f), l = clamp01((lt + 1) * 0.5f);
        if (r > 0.04f) c.throttle = std::max(c.throttle, r);
        if (l > 0.04f) c.brake = std::max(c.brake, l);
        float sx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        if (std::fabs(sx) > 0.12f) {
            float v = (std::fabs(sx) - 0.12f) / 0.88f;
            padSteer = -signf(sx) * std::pow(v, 1.4f);
            pad = true;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) c.handbrake = true;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) pendingShiftUp_ = true;
        if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) pendingShiftDown_ = true;
    }
    if (pad) steerSmooth = padSteer;
    else {
        float rate = (st == 0 || signf(st) != signf(steerSmooth)) ? 7.0f : 3.6f;
        steerSmooth = moveTowards(steerSmooth, st, rate * dt);
        steerSmooth *= 1.0f;
    }
    float lim = pad ? 1.0f : mixf(1.0f, 0.5f, clamp01(kmh / 220.0f));
    c.steer = clampf(steerSmooth * lim, -1, 1);
    if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_LEFT_SHIFT)) pendingShiftUp_ = true;
    if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_LEFT_CONTROL)) pendingShiftDown_ = true;
    c.shiftUp = pendingShiftUp_;
    c.shiftDown = pendingShiftDown_;
}

// ------------------------------------------------------------------ цикл
void Game::frame() {
    float dt = fixedStep ? 1.0f / 60.0f : std::min(GetFrameTime(), 0.1f);
    realTime += dt;
    frameNo++;
    fpsAvg = mixf(fpsAvg, dt > 0 ? 1.0f / dt : 60.0f, 0.05f);
    ui.beginFrame(dt);
    // сценарий тестов
    while (scriptPos < script.size() && scriptWait <= 0) {
        std::string r = command(script[scriptPos]);
        if (r == "not ready") break;  // ждём окончания загрузки
        std::printf("[cmd] %s -> %s\n", script[scriptPos].c_str(), r.c_str());
        std::fflush(stdout);
        scriptPos++;
    }
    if (scriptWait > 0) scriptWait--;
    if (scriptPos >= script.size() && !script.empty() && scriptWait <= 0 && quitAfterScript) quit = true;

    if (state == GS_BOOT) {
        BeginDrawing();
        ClearBackground(Color{8, 10, 18, 255});
        drawBoot();
        EndDrawing();
        updateBoot();
        return;
    }
    stateT += dt;
    if (IsKeyPressed(KEY_F3)) debugHud = !debugHud;
    if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;

    bool world3d = true;
    switch (state) {
        case GS_TITLE: {
            // облёт фестиваля
            const Place* fp = world.place("festival");
            V3 c = fp ? fp->p : V3{0, 0, 0};
            rig.orbit(dt, c + V3{0, 6, 0}, 150, 45, 0.05f);
            prof.timeOfDay = prof.started ? prof.timeOfDay : 18.6f;
            fx.update(dt, world);
            break;
        }
        case GS_STARTER: {
            const Place* sp = world.place("showcar" + std::to_string(1 + starterSel));
            if (sp) {
                V3 c = sp->p;
                rig.orbit(dt, c, 7.5f, 1.8f, 0.25f);
            }
            break;
        }
        case GS_DRIVE:
        case GS_RACE:
            updateDrive(dt);
            break;
        case GS_EVENT_CARD:
        case GS_RESULTS:
            rig.orbit(dt, player.pos, 9, 2.5f, 0.2f);
            fx.update(dt, world);
            break;
        case GS_PAUSE:
        case GS_WHEELSPIN:
            if (pauseFrom == GS_DRIVE && (tab == TAB_GARAGE || tab == TAB_AUTOSHOW) && state == GS_PAUSE) {
                const Place* sp = world.place("showcar0");
                if (sp) rig.orbit(dt, sp->p, 7.0f, 1.6f, orbitSpeed);
            }
            break;
        default: break;
    }
    updateAudio(dt);

    BeginDrawing();
    ClearBackground(BLACK);
    if (world3d) {
        Scene sc;
        buildScene(sc);
        rend.render(sc);
    }
    switch (state) {
        case GS_TITLE: drawTitle(dt); break;
        case GS_STARTER: drawStarter(dt); break;
        case GS_DRIVE: drawHud(dt); break;
        case GS_RACE: drawRaceHud(dt); break;
        case GS_EVENT_CARD: drawEventCard(dt); break;
        case GS_RESULTS: drawResults(dt); break;
        case GS_PAUSE: drawPause(dt); break;
        case GS_WHEELSPIN: drawWheelspin(dt); break;
        default: break;
    }
    if (rewinding) drawRewindOverlay();
    ui.drawToasts(dt);
    if (showHelp) drawHelp();
    if (debugHud) drawDebug();
    drawFade(dt);
    if (!pendingShot.empty()) {
        rlDrawRenderBatchActive();  // 2D копится в пакете — дорисовываем перед чтением кадра
        int w = GetRenderWidth(), h = GetRenderHeight();
        unsigned char* px = rlReadScreenPixels(w, h);
        Image im = {px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(im, pendingShot.c_str());
        RL_FREE(px);
        pendingShot.clear();
    }
    EndDrawing();
}

void Game::updateDrive(float dt) {
    // системные клавиши
    bool pauseKey = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT));
    bool mapKey = IsKeyPressed(KEY_M) || IsKeyPressed(KEY_TAB) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_LEFT));
    if (pauseKey) {
        tab = state == GS_RACE ? TAB_SETTINGS : TAB_MAP;
        enterState(GS_PAUSE);
        audio.play(SFX_UI_OK);
        return;
    }
    if (mapKey && state == GS_DRIVE) {
        tab = TAB_MAP;
        enterState(GS_PAUSE);
        audio.play(SFX_UI_OK);
        return;
    }
    if (IsKeyPressed(KEY_C) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))) {
        rig.mode = (rig.mode + 1) % CAM_COUNT;
        prof.settings.camera = rig.mode;
        ui.toast("Камера: " + std::string(camModeName(rig.mode)), "", pal::cyan, 1.2f);
    }
    if (IsKeyPressed(KEY_L)) {
        lightsManual = true;
        lightsOn = !lightsOn;
    }
    if (IsKeyPressed(KEY_T)) resetToRoad();
    if (state == GS_DRIVE && nearEvent >= 0 && (IsKeyPressed(KEY_ENTER) || (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP) && false))) {
        enterState(GS_EVENT_CARD);
        audio.play(SFX_UI_OK);
        return;
    }
    if (state == GS_DRIVE && nearEvent >= 0 && IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
        enterState(GS_EVENT_CARD);
        return;
    }

    bool rw = IsKeyDown(KEY_R) || IsKeyDown(KEY_BACKSPACE) || (IsGamepadAvailable(0) && IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_UP));
    if (rw && rewind.size() > 1 && !(state == GS_RACE && race.t < 0)) {
        if (!rewinding) {
            rewinding = true;
            audio.play(SFX_WHOOSH);
            skills.reset();
        }
        doRewind(dt);
        CameraInput ci;
        rig.update(dt, player, world, ci);
        return;
    }
    if (rewinding) {
        rewinding = false;
        fx.clear();
    }

    readControls(dt, ctl_);
    float scale = 1.0f;
    acc += dt * scale;
    int steps = 0;
    while (acc >= DT && steps < 12) {
        physicsStep(DT, steps == 0);
        acc -= DT;
        steps++;
        simTime += DT;
    }
    if (steps >= 12) acc = 0;
    if (steps > 0) {
        pendingShiftUp_ = pendingShiftDown_ = false;
    }
    postPhysics(dt);
}

void Game::physicsStep(float dt, bool first) {
    Settings& s = prof.settings;
    Controls c = ctl_;
    if (!first) c.shiftUp = c.shiftDown = false;
    bool auto_ = state == GS_RACE && race.autodrive;
    if (!auto_) player.in = c;
    else if (!first) player.in.shiftUp = player.in.shiftDown = false;
    player.as.abs = s.abs;
    player.as.tcs = s.tcs;
    player.as.stm = s.stm;
    player.as.autoGear = s.autoGear;
    player.as.steerAssist = s.steerAssist;
    if (state == GS_RACE) race.playerAutoGear = s.autoGear;
    player.step(dt, world);
    if (state == GS_RACE) {
        race.substep(dt, world, player);
    } else {
        std::vector<const Vehicle*> all;
        all.push_back(&player);
        for (auto& t : traffic) all.push_back(&t.car);
        for (auto& t : traffic) {
            if (t.frozen) continue;
            t.car.in = t.ai.drive(t.car, dt, all);
            t.car.as = Assists{};
            t.car.step(dt, world);
            if (t.ai.wantsRespawn) {
                const Route& R = trafficRoutes[t.route];
                float ss = t.ai.localS();
                RoutePt q = R.at(ss);
                V3 p = q.p + q.l * t.ai.offset;
                p.y = q.p.y;
                t.car.teleport(p, std::atan2(q.t.x, q.t.z));
                t.ai.wantsRespawn = false;
            }
        }
        for (auto& t : traffic)
            if (!t.frozen) collideCars(player, t.car);
    }
}

void Game::handleSmashes() {
    auto process = [&](Vehicle& v, bool isPlayer) {
        for (const SmashEvent& e : v.smashes) {
            if (e.prop < 0 || e.prop >= (int)world.props.size()) continue;
            Prop& p = world.props[e.prop];
            if (!p.alive) continue;
            p.alive = false;
            p.respawn = 75.0f;
            float dcam = len(p.pos - rig.pos());
            if (dcam < 250) fx.smash(p, v.vel);
            if (dcam < 120) audio.play(SFX_SMASH, clamp01(e.speed / 20.0f) * clamp01(1.2f - dcam / 120.0f));
            if (isPlayer) {
                smashCount_++;
                auto it = std::find(world.boards.begin(), world.boards.end(), e.prop);
                if (it != world.boards.end()) {
                    int bi = (int)(it - world.boards.begin());
                    if (std::find(prof.boards.begin(), prof.boards.end(), bi) == prof.boards.end()) {
                        prof.boards.push_back(bi);
                        addXP(2000, "Доска опыта");
                        addCredits(1000);
                        ui.toast("Доска опыта " + std::to_string(prof.boards.size()) + " / " + std::to_string(world.boards.size()), "+2000 опыта   +1000 CR", pal::orange, 3.0f);
                        audio.play(SFX_REWARD);
                    }
                    p.respawn = -1;  // доски не восстанавливаются
                }
            }
        }
        v.smashes.clear();
        for (const ImpactEvent& im : v.impacts) {
            float dcam = len(im.pos - rig.pos());
            if (im.speed > 4.0f && dcam < 150) fx.impact(im.pos, norm(v.vel * -1.0f + V3{0, 2, 0}), im.speed);
            if (isPlayer) {
                lastCrash_ = std::max(lastCrash_, im.speed);
                rig.shake = std::max(rig.shake, clamp01(im.speed / 18.0f));
                audio.play(SFX_IMPACT, clamp01(im.speed / 16.0f));
            } else if (dcam < 60 && im.speed > 5) audio.play(SFX_IMPACT, clamp01(im.speed / 20.0f) * 0.5f);
        }
        v.impacts.clear();
    };
    process(player, true);
    for (auto& r : race.racers) process(r.car, false);
    for (auto& t : traffic) process(t.car, false);
}

void Game::postPhysics(float dt) {
    handleSmashes();
    // восстановление разрушенного
    if (frameNo % 30 == 0) {
        for (auto& p : world.props) {
            if (p.alive || p.respawn < 0) continue;
            p.respawn -= dt * 30;
            if (p.respawn <= 0 && dist2xz(p.pos, player.pos) > 90 * 90) {
                p.alive = true;
                p.respawn = 0;
            }
        }
    }
    // навыки
    int ot = 0;
    static int lastOvertakes = 0;
    if (state == GS_RACE) {
        ot = std::max(0, race.overtakes - lastOvertakes);
        lastOvertakes = race.overtakes;
    } else lastOvertakes = 0;
    int banked = skills.update(dt, player, smashCount_, lastCrash_, ot);
    smashCount_ = 0;
    lastCrash_ = 0;
    if (banked > 0) {
        int xp = std::max(1, banked / 3);
        addXP(xp, "Навыки");
        addCredits(banked / 25);
        prof.bestChain = std::max(prof.bestChain, banked);
        ui.toast("Цепочка навыков: " + std::to_string(banked), "+" + std::to_string(xp) + " опыта   +" + std::to_string(banked / 25) + " CR", pal::pink, 2.4f);
        audio.play(SFX_SKILL);
    }
    // трюки в свободной езде
    if (state == GS_DRIVE) {
        std::vector<StuntResult> res;
        stunts.update(dt, world, fest, player, skills.frameDriftPts, res);
        for (const StuntResult& r : res) {
            const StuntDef& st = fest.stunts[r.stunt];
            int old = prof.stuntStars.count(st.id) ? prof.stuntStars[st.id] : 0;
            float oldBest = prof.stuntBest.count(st.id) ? prof.stuntBest[st.id] : 0;
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.0f %s", r.value, st.unit());
            std::string starsTxt = std::string(r.stars >= 1 ? "★" : "☆") + (r.stars >= 2 ? "★" : "☆") + (r.stars >= 3 ? "★" : "☆");
            (void)starsTxt;
            if (r.value > oldBest) prof.stuntBest[st.id] = r.value;
            if (r.stars > old) {
                prof.stuntStars[st.id] = r.stars;
                int xp = (r.stars - old) * 1500;
                addXP(xp, st.name);
                addCredits((r.stars - old) * 2500);
                audio.play(SFX_REWARD);
                ui.toast(std::string(stuntTypeName(st.type)) + " «" + st.name + "»: " + buf, "Звёзд: " + std::to_string(r.stars) + " из 3   +" + std::to_string(xp) + " опыта", pal::yellow, 3.5f);
            } else {
                ui.toast(std::string(stuntTypeName(st.type)) + " «" + st.name + "»: " + buf, r.value > oldBest ? "Новый рекорд!" : "Рекорд: " + std::to_string((int)std::max(oldBest, r.value)), pal::cyan, 2.5f);
            }
        }
        // ближайшее событие
        nearEvent = -1;
        float best = 16.0f * 16.0f;
        for (size_t i = 0; i < fest.events.size(); i++) {
            float d = dist2xz(fest.events[i].marker, player.pos);
            if (d < best && std::fabs(fest.events[i].marker.y - player.pos.y) < 8) {
                best = d;
                nearEvent = (int)i;
            }
        }
        if (nearEvent >= 0 && nearEvent != nearEventPrev) audio.play(SFX_CHECKPOINT, 0.5f);
        nearEventPrev = nearEvent;
        updateTraffic(dt);
    }
    // гонка
    if (state == GS_RACE) {
        int gatesBefore = race.gatesPassed;
        float tBefore = race.t;
        race.frame(dt, world, player);
        if (race.gatesPassed > gatesBefore && !race.playerFinished) audio.play(SFX_CHECKPOINT, 0.6f);
        // обратный отсчёт
        for (int k = 3; k >= 1; k--)
            if (tBefore < -k + 0.0f && race.t >= -k + 0.0f) audio.play(SFX_BEEP);
        if (tBefore < 0 && race.t >= 0) audio.play(SFX_GO);
        if (race.playerFinished && race.overT < dt * 1.5f) {
            audio.play(SFX_REWARD);
            if (race.playerPlace == 1) fx.confetti(player.pos);
        }
        if (race.over) {
            endRace(false);
            return;
        }
    }
    updateTimeOfDay(dt);
    // камера
    CameraInput ci;
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 md = GetMouseDelta();
        ci.lookX = -md.x * 0.004f * prof.settings.camSens;
        ci.lookY = md.y * 0.003f * prof.settings.camSens;
        ci.mouseActive = true;
    }
    if (IsGamepadAvailable(0)) {
        float rx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X), ry = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        if (std::fabs(rx) > 0.2f || std::fabs(ry) > 0.2f) {
            ci.lookX = -rx * dt * 3.0f * prof.settings.camSens;
            ci.lookY = ry * dt * 1.5f * prof.settings.camSens;
            ci.mouseActive = true;
        }
        if (IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) ci.lookBack = true;
    }
    if (IsKeyDown(KEY_V)) ci.lookBack = true;
    rig.update(dt, player, world, ci);
    // эффекты
    fx.carFrame(0, player, world, dt, true);
    int slot = 1;
    for (auto& r : race.racers) fx.carFrame(slot++, r.car, world, dt, len(r.car.pos - rig.pos()) < 220);
    if (state == GS_DRIVE)
        for (auto& t : traffic) fx.carFrame(slot++, t.car, world, dt, !t.frozen && len(t.car.pos - rig.pos()) < 200);
    fx.update(dt, world);
    // грязь на кузове
    bool soft = player.surfUnder == SURF_DIRT || player.surfUnder == SURF_SAND || player.surfUnder == SURF_GRAVEL || player.surfUnder == SURF_GRASS;
    if (player.waterDepth > 0.2f) dirt = std::max(0.0f, dirt - dt * 0.25f);
    else if (soft && player.speed > 5) dirt = std::min(1.0f, dirt + dt * 0.012f * clamp01(player.speed / 25));
    player.damage = dirt;
    prof.distanceKm += player.speed * dt / 1000.0f;
    // перемотка: снимки 30 раз в секунду
    snapAcc += dt;
    if (snapAcc >= 1.0f / 30.0f) {
        snapAcc = 0;
        pushRewind();
    }
    autosaveT += dt;
    if (autosaveT > 45) {
        autosaveT = 0;
        save();
    }
}

void Game::updateTimeOfDay(float dt) {
    prof.timeOfDay += dt * prof.settings.timeSpeed / 60.0f;
    if (prof.timeOfDay >= 24.0f) prof.timeOfDay -= 24.0f;
    if (prof.timeOfDay < 0) prof.timeOfDay += 24.0f;
}

// ------------------------------------------------------------------ перемотка
void Game::pushRewind() {
    RewindFrame f;
    f.player = player.snap();
    if (state == GS_RACE) {
        for (auto& r : race.racers) {
            f.racers.push_back(r.car.snap());
            f.aiProg.push_back(r.ai.progress);
            f.aiHint.push_back(r.ai.hint);
        }
        f.raceT = race.t;
        f.trackProg = race.playerTrack.progress;
        f.trackHint = race.playerTrack.hint;
        f.gates = race.gatesPassed;
        f.lap = race.lap;
        f.lapStart = race.lapStart;
    }
    rewind.push_back(std::move(f));
    while (rewind.size() > 300) rewind.pop_front();
}

void Game::doRewind(float dt) {
    rewindAcc += dt * 1.6f;
    while (rewindAcc >= 1.0f / 30.0f && rewind.size() > 1) {
        rewindAcc -= 1.0f / 30.0f;
        rewind.pop_back();
    }
    if (rewind.empty()) return;
    const RewindFrame& f = rewind.back();
    player.restore(f.player);
    if (state == GS_RACE && f.racers.size() == race.racers.size()) {
        for (size_t i = 0; i < race.racers.size(); i++) {
            Racer& r = race.racers[i];
            r.car.restore(f.racers[i]);
            r.ai.progress = f.aiProg[i];
            r.ai.hint = f.aiHint[i];
            if (r.ai.progress - race.ev->startS < race.total) r.finished = false;
        }
        race.t = f.raceT;
        race.playerTrack.progress = f.trackProg;
        race.playerTrack.hint = f.trackHint;
        race.gatesPassed = f.gates;
        race.lap = f.lap;
        race.lapStart = f.lapStart;
        race.missedGate = false;
        race.wrongWayT = 0;
    }
    acc = 0;
}

// ------------------------------------------------------------------ события
void Game::startEvent(int idx) {
    if (idx < 0 || idx >= (int)fest.events.size()) return;
    const EventDef& e = fest.events[idx];
    raceEvent = idx;
    clearTraffic();
    race = RaceSession{};
    race.playerAutoGear = prof.settings.autoGear;
    race.autodrive = autodrive;
    race.start(e, world, player, prof.settings.difficulty, (uint32_t)(realTime * 1000) + idx * 17);
    rewind.clear();
    skills.reset();
    fx.clear();
    rig.reset(player);
    steerSmooth = 0;
    enterState(GS_RACE);
    fade = 1.0f;
    ui.toast(e.name, std::string(eventTypeName(e.type)) + (e.circuit ? "  ·  кругов: " + std::to_string(e.laps) : ""), pal::orange, 3.0f);
}

void Game::endRace(bool abandoned) {
    const EventDef& e = fest.events[raceEvent];
    if (abandoned) {
        race = RaceSession{};
        raceEvent = -1;
        rewind.clear();
        enterState(GS_DRIVE);
        fade = 1.0f;
        return;
    }
    results.clear();
    auto order = race.standings();
    for (int id : order) {
        RaceResultRow row;
        if (id == -1) {
            row.name = "Вы";
            row.car = player.spec->name;
            row.finished = race.playerFinished;
            row.time = race.playerFinishT;
            row.player = true;
        } else if (id == -2) {
            row.name = "Самолёт «Чайка»";
            row.car = "Пилотажный биплан";
            row.finished = race.planeS >= e.route.length - 1;
            row.time = race.planeTotalT;
        } else {
            const Racer& r = race.racers[id];
            row.name = r.name;
            row.car = r.car.spec->name;
            row.finished = r.finished;
            row.time = r.finishT;
        }
        results.push_back(row);
    }
    resultPlace = race.playerFinished ? race.playerPlace : (int)results.size();
    for (size_t i = 0; i < results.size(); i++)
        if (results[i].player) resultPlace = (int)i + 1;
    resultReward = PendingReward{};
    int place = resultPlace;
    resultReward.credits = place <= 3 ? e.reward[place - 1] : 4000;
    resultReward.xp = (int)(e.xp * (place == 1 ? 1.0f : place <= 3 ? 0.7f : 0.4f));
    bool firstWin = place == 1 && (!prof.eventPlace.count(e.id) || prof.eventPlace[e.id] != 1);
    int old = prof.eventPlace.count(e.id) ? prof.eventPlace[e.id] : 99;
    prof.eventPlace[e.id] = std::min(old, place);
    addCredits(resultReward.credits);
    addXP(resultReward.xp, e.name);
    if (firstWin) prof.wheelspins++;
    resultTitle = place == 1 ? "ПОБЕДА!" : place <= 3 ? "ПОДИУМ!" : "ФИНИШ";
    save();
    enterState(GS_RESULTS);
    audio.play(place == 1 ? SFX_LEVEL : SFX_REWARD);
}

// ------------------------------------------------------------------ колесо удачи
void Game::prepareSpin() {
    spinReel.clear();
    Rng rng((uint32_t)(realTime * 1000) + prof.xp);
    const auto& cat = carCatalog();
    auto prize = [&]() {
        SpinPrize p;
        float r = rng.f01();
        if (r < 0.24f) {
            // машина: дешёвые чаще
            float best = 1e9f;
            const CarSpec* pick = &cat[0];
            for (int k = 0; k < 3; k++) {
                const CarSpec& c = cat[rng.irange(0, (int)cat.size() - 1)];
                if (c.price < best) { best = (float)c.price; pick = &c; }
            }
            if (rng.chance(0.25f)) pick = &cat[rng.irange(0, (int)cat.size() - 1)];
            p.title = pick->name;
            p.sub = std::string("Машина · класс ") + pick->className() + " " + std::to_string(pick->pi);
            p.carId = pick->id;
            p.c = classColor(classIndex(pick->pi));
        } else {
            int cr = r < 0.55f ? 10000 : r < 0.78f ? 25000 : r < 0.92f ? 50000 : r < 0.985f ? 100000 : 250000;
            p.credits = cr;
            p.title = std::to_string(cr / 1000) + " 000 CR";
            p.sub = "Кредиты";
            p.c = cr >= 100000 ? pal::yellow : cr >= 50000 ? pal::pink : pal::cyan;
        }
        return p;
    };
    for (int i = 0; i < 30; i++) spinReel.push_back(prize());
    spinResult = 26;
    spinT = 0;
    spinDone = false;
}

// ------------------------------------------------------------------ свет и сцена
void Game::computeLight(FrameLight& L) const {
    float t = prof.timeOfDay;
    struct K { float t; V3 sun, sky, gnd, zen, hor; float fog, expo, night; };
    static const K ks[] = {
        {0.0f, {0.2f, 0.26f, 0.42f}, {0.032f, 0.042f, 0.075f}, {0.018f, 0.018f, 0.024f}, {0.004f, 0.007f, 0.02f}, {0.028f, 0.042f, 0.075f}, 0.0009f, 1.12f, 1},
        {4.8f, {0.2f, 0.26f, 0.42f}, {0.032f, 0.042f, 0.075f}, {0.018f, 0.018f, 0.024f}, {0.004f, 0.007f, 0.02f}, {0.028f, 0.042f, 0.075f}, 0.0009f, 1.12f, 1},
        {5.9f, {1.1f, 0.55f, 0.38f}, {0.17f, 0.15f, 0.22f}, {0.07f, 0.055f, 0.055f}, {0.07f, 0.11f, 0.28f}, {0.8f, 0.44f, 0.36f}, 0.001f, 1.12f, 0.35f},
        {7.2f, {2.3f, 1.7f, 1.15f}, {0.34f, 0.37f, 0.5f}, {0.15f, 0.13f, 0.1f}, {0.15f, 0.31f, 0.68f}, {0.84f, 0.72f, 0.64f}, 0.0008f, 0.86f, 0},
        {12.0f, {2.9f, 2.76f, 2.5f}, {0.4f, 0.5f, 0.7f}, {0.19f, 0.18f, 0.14f}, {0.12f, 0.33f, 0.8f}, {0.62f, 0.77f, 0.95f}, 0.00055f, 0.8f, 0},
        {17.0f, {2.8f, 2.45f, 2.0f}, {0.39f, 0.46f, 0.63f}, {0.19f, 0.16f, 0.12f}, {0.13f, 0.32f, 0.75f}, {0.72f, 0.78f, 0.9f}, 0.0006f, 0.82f, 0},
        {19.2f, {2.6f, 1.45f, 0.72f}, {0.33f, 0.3f, 0.36f}, {0.17f, 0.12f, 0.09f}, {0.16f, 0.22f, 0.5f}, {1.0f, 0.6f, 0.38f}, 0.0008f, 0.92f, 0},
        {20.2f, {0.9f, 0.36f, 0.22f}, {0.15f, 0.11f, 0.17f}, {0.06f, 0.04f, 0.04f}, {0.06f, 0.08f, 0.22f}, {0.6f, 0.3f, 0.28f}, 0.0009f, 1.15f, 0.45f},
        {21.4f, {0.2f, 0.26f, 0.42f}, {0.032f, 0.042f, 0.075f}, {0.018f, 0.018f, 0.024f}, {0.004f, 0.007f, 0.02f}, {0.028f, 0.042f, 0.075f}, 0.0009f, 1.12f, 1},
        {24.0f, {0.2f, 0.26f, 0.42f}, {0.032f, 0.042f, 0.075f}, {0.018f, 0.018f, 0.024f}, {0.004f, 0.007f, 0.02f}, {0.028f, 0.042f, 0.075f}, 0.0009f, 1.12f, 1},
    };
    int n = sizeof(ks) / sizeof(ks[0]);
    int i = 0;
    while (i < n - 2 && t >= ks[i + 1].t) i++;
    const K& a = ks[i];
    const K& b = ks[i + 1];
    float k = clamp01((t - a.t) / std::max(b.t - a.t, 0.001f));
    k = k * k * (3 - 2 * k);
    L.sunCol = lerp(a.sun, b.sun, k);
    L.skyCol = lerp(a.sky, b.sky, k);
    L.gndCol = lerp(a.gnd, b.gnd, k);
    L.zenith = lerp(a.zen, b.zen, k);
    L.horizon = lerp(a.hor, b.hor, k);
    L.fogDen = mixf(a.fog, b.fog, k);
    L.exposure = mixf(a.expo, b.expo, k);
    L.night = mixf(a.night, b.night, k);
    L.fogCol = L.horizon;
    L.fogSun = lerp(L.horizon, L.sunCol * 0.45f, 0.5f);
    // солнце: восход на востоке (+x) в 6:00, закат на западе в 20:00, днём на юге (+z)
    float day = (t - 6.0f) / 14.0f;
    float elev;
    if (day >= 0 && day <= 1) elev = std::sin(day * PI) * 64.0f * DEG;
    else {
        float nt = t < 6 ? t + 24 : t;
        elev = -std::sin(clamp01((nt - 20.0f) / 10.0f) * PI) * 40.0f * DEG;
    }
    float th = clampf(day, -0.3f, 1.3f) * PI;
    V3 hz = norm(V3{std::cos(th), 0, 0.5f * std::sin(th) + 0.25f});
    V3 sun = norm(hz * std::cos(elev) + V3{0, std::sin(elev), 0});
    L.realSun = sun;
    V3 moon = norm(V3{-sun.x * 0.7f + 0.2f, std::max(0.35f, -sun.y) + 0.25f, -sun.z * 0.7f + 0.3f});
    L.moonDir = moon;
    float sk = smoothstep(-0.06f, 0.08f, sun.y);
    V3 ld = norm(lerp(moon, V3{sun.x, std::max(sun.y, 0.04f), sun.z}, sk));
    L.sunDir = ld;
    L.cloud = 0.38f;
    L.shadowOn = ld.y > 0.1f;
    L.pointCount = 0;
    // фары игрока
    bool lights = lightsManual ? lightsOn : L.night > 0.3f;
    if (lights && (state == GS_DRIVE || state == GS_RACE)) {
        L.headPos = player.toWorld({0, 0.65f - player.spec->cgH, player.spec->length * 0.5f + 0.2f});
        L.headDir = norm(player.fwd() - player.up() * 0.07f);
        L.headOn = 1.0f;
    } else L.headOn = 0;
}

void Game::buildScene(Scene& sc) {
    sc.cam = rig.cam;
    if (freeCam) {
        sc.cam.position = {freeCamPos.x, freeCamPos.y, freeCamPos.z};
        sc.cam.target = {freeCamTarget.x, freeCamTarget.y, freeCamTarget.z};
        sc.cam.up = {0, 1, 0};
        sc.cam.fovy = 55;
    }
    computeLight(sc.L);
    sc.time = realTime;
    sc.fx = &fx;
    float night = sc.L.night;
    bool lightsAll = night > 0.3f;
    bool playerLights = lightsManual ? lightsOn : lightsAll;
    bool menuShow = state == GS_STARTER || ((state == GS_PAUSE || state == GS_WHEELSPIN) && (tab == TAB_GARAGE || tab == TAB_AUTOSHOW) && pauseFrom == GS_DRIVE);
    bool drawPlayer = !(rig.mode == CAM_HOOD || rig.mode == CAM_BUMPER) || !(state == GS_DRIVE || state == GS_RACE || state == GS_PAUSE);
    if (state == GS_TITLE) drawPlayer = false;
    if (drawPlayer && !menuShow) sc.cars.push_back(carDrawFrom(player, playerLights ? 1.0f : 0.0f));
    if (state == GS_RACE || (state == GS_PAUSE && pauseFrom == GS_RACE) || state == GS_RESULTS) {
        for (auto& r : race.racers) sc.cars.push_back(carDrawFrom(r.car, lightsAll ? 1.0f : 0.0f));
    } else if (state != GS_TITLE && state != GS_STARTER) {
        for (auto& t : traffic)
            if (len(t.car.pos - rig.pos()) < 1200) sc.cars.push_back(carDrawFrom(t.car, lightsAll ? 1.0f : 0.0f));
    }
    // выставочные машины у сцены
    static const char* show[4] = {"fantom", "strela", "komet", "burya"};
    for (int i = 0; i < 4; i++) {
        const Place* sp = world.place("showcar" + std::to_string(i));
        if (!sp || len(sp->p - rig.pos()) > 500) continue;
        const CarSpec* s = findCar(show[i]);
        uint32_t col = s ? s->color : 0xffffffff;
        if (state == GS_STARTER && i >= 1 && i <= 3) {
            s = findCar(starters[i - 1]);
            col = s ? s->color : col;
        }
        if (menuShow && state != GS_STARTER && i == 0) {
            // гараж и автосалон: выбранная машина на подиуме
            const CarSpec* ms = nullptr;
            if (tab == TAB_GARAGE && !prof.garage.empty()) {
                const OwnedCar& oc = prof.garage[std::clamp(tabSel, 0, (int)prof.garage.size() - 1)];
                ms = findCar(oc.id);
                col = oc.color;
            } else if (tab == TAB_AUTOSHOW) {
                const auto& cat = carCatalog();
                ms = &cat[std::clamp(tabSel, 0, (int)cat.size() - 1)];
                col = ms->color;
            }
            if (ms) s = ms;
        }
        if (s) sc.cars.push_back(carDrawStatic(s, sp->p, sp->yaw, col));
    }
    // маркеры событий
    if (state == GS_DRIVE || state == GS_TITLE || (state == GS_PAUSE && pauseFrom == GS_DRIVE)) {
        for (size_t i = 0; i < fest.events.size(); i++) {
            const EventDef& e = fest.events[i];
            MarkerDraw m;
            m.p = e.marker;
            m.c = e.type == EV_ROAD ? pal::orange : e.type == EV_DIRT ? Color{230, 170, 60, 255} : e.type == EV_CROSS ? pal::green : e.type == EV_DRAG ? pal::cyan : pal::pink;
            m.radius = (int)i == nearEvent ? 7.0f : 5.0f;
            m.height = 70;
            sc.markers.push_back(m);
        }
        if (hasWaypoint) sc.markers.push_back({waypoint, pal::yellow, 3, 140});
    }
    // гонка: чекпоинты, линия движения, самолёт
    if ((state == GS_RACE || (state == GS_PAUSE && pauseFrom == GS_RACE)) && race.ev) {
        const Route& R = race.ev->route;
        for (int g = race.gatesPassed; g < (int)race.gates.size() && g < race.gatesPassed + 3; g++) {
            float s = race.ev->startS + race.gates[g];
            RoutePt q = R.at(s);
            GateDraw gd;
            gd.p = q.p;
            gd.l = q.l;
            gd.halfW = q.halfW;
            gd.next = g == race.gatesPassed;
            gd.finish = g == (int)race.gates.size() - 1;
            sc.gates.push_back(gd);
        }
        int lineMode = prof.settings.line;
        if (lineMode > 0) {
            float s0 = race.playerTrack.localS();
            for (float d = 6; d < 150; d += 3.2f) {
                float s = s0 + d;
                if (!R.loop && s > R.length) break;
                RoutePt q = R.at(s);
                float target = race.playerTrack.targetSpeedAt(s);
                float diff = player.speed - target;
                Color c;
                if (diff > 5) c = Color{240, 50, 40, 210};
                else if (diff > 0.5f) c = Color{255, 196, 40, 200};
                else {
                    if (lineMode == 1) continue;
                    c = Color{60, 160, 255, 170};
                }
                GroundHit gh = world.ground(q.p.x, q.p.z, q.p.y + 1);
                LinePt lp{V3{q.p.x, std::max(gh.h, q.p.y - 0.3f), q.p.z}, q.t, q.l, c};
                sc.line.push_back(lp);
            }
        }
        if (race.ev->type == EV_SHOWCASE) {
            float ps = race.planeS;
            RoutePt q = R.at(std::min(ps, R.length - 0.1f));
            RoutePt q2 = R.at(std::min(ps + 30, R.length - 0.1f));
            V3 dir = norm(q2.p - q.p + V3{0, 0.001f, 0});
            float bank = clampf(q.curv * 60.0f, -0.7f, 0.7f);
            sc.plane = true;
            sc.planePos = q.p + V3{0, 26 + 4 * std::sin(realTime * 0.7f), 0};
            sc.planeRot = qmul(qlook(dir, V3{0, 1, 0}), qaxis({0, 0, 1}, -bank));
        }
    }
}

void Game::updateAudio(float dt) {
    (void)dt;
    EngineSound e;
    const CarSpec& S = *player.spec;
    e.rpm = player.rpm;
    e.idle = S.idle;
    e.redline = S.redline;
    e.load = player.engineLoad;
    e.throttle = player.in.throttle;
    e.cylinders = S.cylinders;
    e.tone = S.soundTone;
    e.electric = S.electric;
    e.turbo = S.turbo;
    e.speed = player.speed;
    e.squeal = clamp01(player.squeal);
    bool soft = player.surfUnder != SURF_ASPHALT && player.wheelsOnGround > 0;
    e.rumble = soft ? clamp01(player.speed / 30.0f) * 0.6f : 0.0f;
    e.wind = clamp01(sq(player.speed / 75.0f));
    e.water = player.waterDepth > 0.1f ? clamp01(player.speed / 15.0f) : 0.0f;
    bool active = (state == GS_DRIVE || state == GS_RACE) && !rewinding;
    e.volume = active ? 1.0f : (state == GS_EVENT_CARD || state == GS_RESULTS ? 0.3f : 0.0f);
    if (!active) {
        e.squeal = 0;
        e.wind = 0;
        e.rumble = 0;
        e.water = 0;
    }
    audio.setEngine(e);
    static int lastGear = 1;
    if (active && player.gear != lastGear && player.gear > 0 && lastGear > 0) audio.play(SFX_SHIFT, 0.8f);
    lastGear = player.gear;
    static float bfT = 0;
    bfT -= dt;
    if (active && player.boostHint > 0.5f && bfT <= 0 && !S.electric) {
        audio.play(SFX_BACKFIRE, 0.7f);
        bfT = 0.25f;
    }
}

}  // namespace cl
