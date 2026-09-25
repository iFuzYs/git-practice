// Команды для автотестов и отладки: вызываются из --script, --cmd и из JS (cl_cmd)
#include <cstdio>
#include <sstream>

#include "game.h"

namespace cl {

static const char* stateName(GameState s) {
    switch (s) {
        case GS_BOOT: return "boot";
        case GS_TITLE: return "title";
        case GS_STARTER: return "starter";
        case GS_DRIVE: return "drive";
        case GS_EVENT_CARD: return "eventcard";
        case GS_RACE: return "race";
        case GS_RESULTS: return "results";
        case GS_PAUSE: return "pause";
        case GS_WHEELSPIN: return "wheelspin";
    }
    return "?";
}

std::string Game::command(const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    char buf[512];
    auto ready = [&]() { return booted_; };
    if (cmd == "wait") {
        int n = 1;
        in >> n;
        scriptWait = n;
        return "ok";
    }
    if (cmd == "state") return stateName(state);
    if (cmd == "frame") return std::to_string(frameNo);
    if (cmd == "keys") return std::string("enter=") + (IsKeyDown(KEY_ENTER) ? "1" : "0") + " w=" + (IsKeyDown(KEY_W) ? "1" : "0") + " sel=" + std::to_string(sel) + " ok=" + std::to_string(ui.okCount) + " state=" + stateName(state);
    if (cmd == "waitstate") {
        // waitstate имя [кадров] — держит сценарий, пока не наступит состояние
        static int started = -1;
        std::string st;
        int maxF = 20000;
        in >> st >> maxF;
        if (started < 0) started = frameNo;
        if (st == stateName(state) || frameNo - started > maxF) {
            int waited = frameNo - started;
            started = -1;
            return st == stateName(state) ? "ok after " + std::to_string(waited) : "timeout";
        }
        return "not ready";
    }
    if (cmd == "quit") { quit = true; return "ok"; }
    if (cmd == "autoquit") { quitAfterScript = true; return "ok"; }
    if (cmd == "fixed") { int v = 1; in >> v; fixedStep = v != 0; return "ok"; }
    if (!ready()) {
        if (cmd == "ready") return "0";
        return "not ready";
    }
    if (cmd == "ready") return "1";
    if (cmd == "newgame") {
        std::string car = "lastochka";
        in >> car;
        newGame(car);
        enterState(GS_DRIVE);
        return "ok";
    }
    if (cmd == "title") { enterState(GS_TITLE); return "ok"; }
    if (cmd == "starter") { int i = 0; in >> i; starterSel = std::clamp(i, 0, 2); enterState(GS_STARTER); return "ok"; }
    if (cmd == "garage") {
        // показать машину каталога на подиуме (вкладка «Автосалон»)
        std::string id;
        in >> id;
        const auto& cat = carCatalog();
        for (size_t i = 0; i < cat.size(); i++)
            if (cat[i].id == id) tabSel = (int)i;
        pauseFrom = GS_DRIVE;
        tab = TAB_AUTOSHOW;
        state = GS_PAUSE;
        return "ok";
    }
    if (cmd == "drive") {
        if (!prof.started) newGame("lastochka");
        race = RaceSession{};
        raceEvent = -1;
        enterState(GS_DRIVE);
        return "ok";
    }
    if (cmd == "teleport") {
        float x = 0, z = 0, yaw = 0;
        in >> x >> z >> yaw;
        teleportPlayer(V3{x, 0, z}, yaw);
        return "ok";
    }
    if (cmd == "place") {
        std::string id;
        in >> id;
        const Place* p = world.place(id);
        if (!p) return "no place";
        teleportPlayer(p->p, p->yaw);
        return "ok";
    }
    if (cmd == "event") {
        std::string id;
        in >> id;
        int i = fest.eventIndex(id);
        if (i < 0) return "no event";
        if (!prof.started) newGame("lastochka");
        startEvent(i);
        return "ok";
    }
    if (cmd == "autodrive") {
        int v = 1;
        in >> v;
        autodrive = v != 0;
        race.autodrive = autodrive;
        return "ok";
    }
    if (cmd == "time") {
        float h = 12;
        in >> h;
        prof.timeOfDay = h;
        return "ok";
    }
    if (cmd == "timespeed") { in >> prof.settings.timeSpeed; return "ok"; }
    if (cmd == "car") {
        std::string id;
        in >> id;
        const CarSpec* s = findCar(id);
        if (!s) return "no car";
        setPlayerCar(id, s->color, true);
        return "ok";
    }
    if (cmd == "cam") { in >> rig.mode; rig.mode = std::clamp(rig.mode, 0, CAM_COUNT - 1); return "ok"; }
    if (cmd == "shot") {
        std::string path = "shot.png";
        in >> path;
        pendingShot = path;
        scriptWait = 1;  // следующая команда — после того, как кадр будет снят
        return "ok";
    }
    if (cmd == "quality") {
        int q = 1, sh = 1;
        in >> q >> sh;
        prof.settings.quality = q;
        prof.settings.shadows = sh != 0;
        rend.setQuality(q, sh != 0);
        applySettings();
        return "ok";
    }
    if (cmd == "input") {
        // input газ тормоз руль ручник кадров
        float t = 0, b = 0, s = 0;
        int hb = 0, n = 60;
        in >> t >> b >> s >> hb >> n;
        forced = Controls{};
        forced.throttle = t;
        forced.brake = b;
        forced.steer = s;
        forced.handbrake = hb != 0;
        forcedFrames = n;
        return "ok";
    }
    if (cmd == "pause") {
        int t = 0;
        in >> t;
        tab = t;
        enterState(GS_PAUSE);
        return "ok";
    }
    if (cmd == "resume") {
        state = pauseFrom;
        return "ok";
    }
    if (cmd == "spin") {
        prof.wheelspins = std::max(prof.wheelspins, 1);
        prepareSpin();
        pauseFrom = GS_DRIVE;
        state = GS_WHEELSPIN;
        stateT = 0;
        return "ok";
    }
    if (cmd == "results") {
        // досрочно завершить гонку (для проверки экрана итогов)
        if (state == GS_RACE) endRace(false);
        return "ok";
    }
    if (cmd == "card") {
        std::string id;
        in >> id;
        int i = fest.eventIndex(id);
        if (i < 0) return "no event";
        teleportPlayer(fest.events[i].marker, fest.events[i].markerYaw);
        nearEvent = i;
        enterState(GS_EVENT_CARD);
        return "ok";
    }
    if (cmd == "view") {
        // view x z высота_над_землёй tx tz — свободная камера (view off — выключить)
        std::string a;
        in >> a;
        if (a == "off") { freeCam = false; return "ok"; }
        float x = std::stof(a), z = 0, h = 5, tx = 0, tz = 0;
        in >> z >> h >> tx >> tz;
        freeCam = true;
        freeCamPos = {x, world.terrainH(x, z) + h, z};
        freeCamTarget = {tx, world.terrainH(tx, tz) + 2, tz};
        return "ok";
    }
    if (cmd == "board") {
        int i = 0;
        in >> i;
        if (i < 0 || i >= (int)world.boards.size()) return "no board";
        const Prop& p = world.props[world.boards[i]];
        freeCam = true;
        V3 f{std::sin(p.yaw), 0, std::cos(p.yaw)};
        freeCamPos = p.pos + f * 9.0f + V3{0, 3, 0};
        freeCamTarget = p.pos + V3{0, 2, 0};
        return "ok";
    }
    if (cmd == "orbit") { in >> orbitSpeed; return "ok"; }
    if (cmd == "credits") { int v = 0; in >> v; prof.credits = v; return "ok"; }
    if (cmd == "debug") { debugHud = !debugHud; return "ok"; }
    if (cmd == "save") { save(); return "ok"; }
    if (cmd == "info") {
        int pos = state == GS_RACE ? race.position() : 0;
        std::snprintf(buf, sizeof buf,
                      "{\"state\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"kmh\":%.1f,\"gear\":%d,\"fps\":%.1f,\"race_t\":%.1f,\"pos\":%d,\"lap\":%d,\"gates\":%d,\"gatesTotal\":%d,\"finished\":%d,\"calls\":%d,\"inst\":%d,\"level\":%d,\"credits\":%d,\"tod\":%.2f}",
                      stateName(state), player.pos.x, player.pos.y, player.pos.z, player.kmh(), player.gear, fpsAvg, race.t, pos, race.lap, race.gatesPassed, (int)race.gates.size(),
                      race.playerFinished ? 1 : 0, rend.drawCalls, rend.instanceCount, prof.level, prof.credits, prof.timeOfDay);
        return buf;
    }
    return "unknown command";
}

}  // namespace cl

extern "C" const char* cl_cmd(const char* line) {
    static std::string out;
    if (!cl::g_game) return "no game";
    out = cl::g_game->command(line ? line : "");
    return out.c_str();
}
