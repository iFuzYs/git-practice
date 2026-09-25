// Игра: состояния, ввод, цикл симуляции, прогресс, меню
#pragma once
#include <deque>
#include <string>
#include <vector>

#include "../audio/audio.h"
#include "../render/camera.h"
#include "../render/effects.h"
#include "../render/renderer.h"
#include "../sim/events.h"
#include "../sim/profile.h"
#include "../sim/skills.h"
#include "../ui/ui.h"
#include "../world/world.h"

namespace cl {

enum GameState { GS_BOOT, GS_TITLE, GS_STARTER, GS_DRIVE, GS_EVENT_CARD, GS_RACE, GS_RESULTS, GS_PAUSE, GS_WHEELSPIN };
enum PauseTab { TAB_MAP, TAB_GARAGE, TAB_AUTOSHOW, TAB_WHEELSPIN, TAB_PROGRESS, TAB_SETTINGS, TAB_CONTROLS, TAB_COUNT };

struct TrafficCar {
    Vehicle car;
    AIDriver ai;
    int route = 0;
    std::string name;
    bool frozen = false;
};

struct RewindFrame {
    Vehicle::Snap player;
    std::vector<Vehicle::Snap> racers;
    std::vector<float> aiProg;
    std::vector<int> aiHint;
    float raceT = 0, trackProg = 0;
    int trackHint = 0, gates = 0, lap = 1;
    float lapStart = 0;
};

struct RaceResultRow {
    std::string name, car;
    float time = 0;
    bool finished = false, player = false;
};

struct PendingReward {
    int credits = 0, xp = 0;
    std::string carId;
};

class Game {
public:
    // --- мир и системы
    World world;
    Festival fest;
    Profile prof;
    Renderer rend;
    Effects fx;
    CameraRig rig;
    Ui ui;
    Audio audio;
    Texture2D mapTex{};
    Vehicle player;
    RaceSession race;
    int raceEvent = -1;
    StuntTracker stunts;
    SkillChain skills;
    std::vector<TrafficCar> traffic;
    std::vector<Route> trafficRoutes;

    GameState state = GS_BOOT;
    GameState pauseFrom = GS_DRIVE;
    bool quit = false;
    float simTime = 0;          // время симуляции, с
    float realTime = 0;
    double acc = 0;
    static constexpr float DT = 1.0f / 120.0f;

    // --- запуск
    std::string savePath = "coastline_save.txt";
    std::vector<std::string> script;  // команды из --script
    size_t scriptPos = 0;
    int scriptWait = 0;
    bool fixedStep = false;             // детерминированный шаг кадра для тестов
    bool noSave = false;
    std::string startCmd;

    void parseArgs(int argc, char** argv);
    void init();
    void frame();
    void shutdown();
    std::string command(const std::string& line);

    // --- состояние интерфейса
    int sel = 0, tab = 0, tabSel = 0, tabSel2 = 0;
    float stateT = 0;
    float fade = 0;                     // затемнение при переходах
    std::string resultTitle;
    std::vector<RaceResultRow> results;
    int resultPlace = 0;
    PendingReward resultReward;
    // колесо удачи
    struct SpinPrize { std::string title, sub, carId; int credits = 0; Color c; };
    std::vector<SpinPrize> spinReel;
    float spinT = 0, spinDur = 4.0f;
    int spinResult = 0;
    bool spinDone = false;
    // карта
    float mapZoom = 1, mapX = 0, mapZ = 0;
    int mapSel = 0;
    V3 waypoint;
    bool hasWaypoint = false;
    // события рядом
    int nearEvent = -1;
    int nearEventPrev = -1;
    // перемотка
    std::deque<RewindFrame> rewind;
    bool rewinding = false;
    float rewindAcc = 0, snapAcc = 0;
    // разное
    bool lightsOn = false, lightsManual = false;
    float lookX = 0, lookY = 0;
    float steerSmooth = 0;
    float autosaveT = 0;
    float boardMsgT = 0;
    std::string lastStuntMsg;
    float dirt = 0;
    bool debugHud = false;
    bool autodrive = false;
    int frameNo = 0;
    float fpsAvg = 60;
    int starterSel = 0;
    std::vector<std::string> starters = {"lastochka", "breeze", "sirocco"};
    int levelUps = 0;
    bool showHelp = false;
    bool confirmReset = false;
    // принудительное управление для тестов (команда input)
    int forcedFrames = 0;
    Controls forced;
    std::string pendingShot;
    bool quitAfterScript = false;
    float orbitSpeed = 0.3f;
    int winW = 1280, winH = 720;
    bool freeCam = false;  // отладочная камера для снимков
    V3 freeCamPos, freeCamTarget;

    const CarSpec* currentSpec() const;
    void setPlayerCar(const std::string& id, uint32_t color, bool keepPos);
    void teleportPlayer(V3 p, float yaw);
    void resetToRoad();
    void startEvent(int idx);
    void endRace(bool abandoned);
    void addXP(int xp, const std::string& why);
    void addCredits(int cr);
    void save();
    void load();
    void newGame(const std::string& starter);
    void enterState(GameState s);
    void spawnTraffic();
    void clearTraffic();
    void prepareSpin();
    void applySettings();

private:
    // обновление
    void updateBoot();
    void updateDrive(float dt);
    void physicsStep(float dt, bool firstStep);
    void postPhysics(float dt);
    void readControls(float dt, Controls& c);
    void handleSmashes();
    void updateTraffic(float dt);
    void updateTimeOfDay(float dt);
    void pushRewind();
    void doRewind(float dt);
    void computeLight(FrameLight& L) const;
    void buildScene(Scene& sc);
    void updateAudio(float dt);
    // экраны (game_ui.cpp)
    void drawBoot();
    void drawTitle(float dt);
    void drawStarter(float dt);
    void drawHud(float dt);
    void drawRaceHud(float dt);
    void drawMinimap(Rectangle r, float radiusM);
    void drawSpeedo();
    void drawSkills();
    void drawEventCard(float dt);
    void drawResults(float dt);
    void drawPause(float dt);
    void drawMapTab(Rectangle area, bool fullscreen);
    void drawGarageTab(Rectangle area);
    void drawAutoshowTab(Rectangle area);
    void drawSpinTab(Rectangle area);
    void drawProgressTab(Rectangle area);
    void drawSettingsTab(Rectangle area);
    void drawControlsTab(Rectangle area);
    void drawWheelspin(float dt);
    void drawCarStats(const CarSpec& s, Rectangle r);
    void drawRewindOverlay();
    void drawFade(float dt);
    void drawDebug();
    void drawHelp();

    bool booted_ = false;
    int bootStep_ = 0;
    float bootProgress_ = 0;
    std::string bootMsg_;
    Controls ctl_;
    bool pendingShiftUp_ = false, pendingShiftDown_ = false;
    float lastCrash_ = 0;
    int smashCount_ = 0;
    float camShowA_ = 0;
    int showcaseCar_ = 0;
    std::vector<int> spawnedBoards_;
    std::vector<V3> placesCache_;
};

// точка входа для команд из JS (web) и тестов
extern Game* g_game;

}  // namespace cl

extern "C" const char* cl_cmd(const char* line);
