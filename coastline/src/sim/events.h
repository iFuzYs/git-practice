// События фестиваля: гонки, трюки и их логика
#pragma once
#include <string>
#include <vector>

#include "ai.h"
#include "route.h"
#include "vehicle.h"

namespace cl {

enum EventType : uint8_t { EV_ROAD, EV_DIRT, EV_CROSS, EV_DRAG, EV_SHOWCASE };
const char* eventTypeName(EventType t);

struct EventDef {
    std::string id, name, desc;
    EventType type = EV_ROAD;
    bool circuit = false;
    int laps = 1;
    std::vector<Leg> legs;
    std::string trail;
    Route route;
    int opponents = 7;
    int reward[3] = {30000, 18000, 12000};
    int xp = 3000;
    int unlockAfter = 0;   // сколько событий нужно пройти, чтобы открыть
    float startS = 0;      // где стоит стартовая линия
    V3 marker;             // точка входа на карте
    float markerYaw = 0;
};

enum StuntType : uint8_t { ST_TRAP, ST_ZONE, ST_DRIFT, ST_JUMP };
const char* stuntTypeName(StuntType t);

struct StuntDef {
    std::string id, name;
    StuntType type = ST_TRAP;
    int road = -1;
    float s0 = 0, s1 = 0;       // участок дороги (для радара s0 = s1)
    int ramp = -1;              // для прыжков
    float stars[3] = {0, 0, 0}; // пороги: км/ч, км/ч, очки или метры
    V3 pos;                     // значок на карте
    float yaw = 0;
    const char* unit() const;
};

struct Festival {
    std::vector<EventDef> events;
    std::vector<StuntDef> stunts;
    void build(const World& w);
    int eventIndex(const std::string& id) const;
};

// Соперник в гонке
struct Racer {
    Vehicle car;
    AIDriver ai;
    std::string name;
    bool finished = false;
    float finishT = 0;
    float baseSkill = 0.9f;
};

class RaceSession {
public:
    const EventDef* ev = nullptr;
    std::vector<Racer> racers;
    AIDriver playerTrack;      // отслеживание игрока (и автопилот для тестов)
    float t = 0;               // время гонки (отрицательное — обратный отсчёт)
    float total = 0;           // дистанция
    std::vector<float> gates;  // положения чекпоинтов (по прогрессу)
    int gatesPassed = 0;
    int lap = 1;
    float lapStart = 0, lastLap = 0, bestLap = 0;
    bool playerFinished = false;
    float playerFinishT = 0;
    int playerPlace = 0;
    bool over = false;
    float overT = 0;
    float wrongWayT = 0;
    bool missedGate = false;
    int overtakes = 0;
    float planeS = 0, planeTotalT = 0;  // шоу с самолётом
    bool autodrive = false;
    bool playerAutoGear = true;  // выбор игрока в настройках (на старте коробка удерживается)
    int difficulty = 1;

    void start(const EventDef& e, const World& w, Vehicle& player, int difficulty, uint32_t seed);
    // шаг физики соперников (вызывается с частотой симуляции)
    void substep(float dt, const World& w, Vehicle& player);
    // логика кадра: чекпоинты, финиш, позиции
    void frame(float dt, const World& w, Vehicle& player);
    int position() const;          // место игрока сейчас
    float playerProgress() const { return playerTrack.progress - ev->startS; }
    std::vector<int> standings() const;  // индексы: -1 — игрок
    void respawnRacer(Racer& r, const World& w);
    void respawnPlayer(Vehicle& player, const World& w);
    V3 nextGatePos() const;
    int lastPosition_ = 0;
};

// Трюки в свободной езде
struct StuntResult {
    int stunt = -1;
    float value = 0;
    int stars = 0;
    bool record = false;
};

class StuntTracker {
public:
    void update(float dt, const World& w, const Festival& f, const Vehicle& v, float driftPts, std::vector<StuntResult>& out);
    int activeZone = -1;       // текущая зона скорости или дрифта
    float zoneT = 0, zoneScore = 0, zoneDist = 0;
    int jumpRamp = -1;
    V3 takeoff;
    bool airborne = false;
    float lastRoadS = -1;
    int lastRoad = -1;
    std::string status;        // строка для HUD
    float statusValue = 0;
};

}  // namespace cl
