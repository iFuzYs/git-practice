// Мир: рельеф, вода, дороги, объекты и коллизии. Без зависимостей от рендера.
#pragma once
#include <string>
#include <vector>

#include "../core/mathx.h"
#include "../core/noise.h"

namespace cl {

enum Surface : uint8_t { SURF_ASPHALT, SURF_DIRT, SURF_GRAVEL, SURF_GRASS, SURF_SAND, SURF_ROCK, SURF_WATER, SURF_WOOD, SURF_COUNT };
const char* surfaceName(Surface s);

enum RoadType : uint8_t { ROAD_HIGHWAY, ROAD_MAIN, ROAD_STREET, ROAD_DIRT, ROAD_RUNWAY };

struct RoadPt {
    V3 p;          // центр полотна
    V3 t;          // касательная (вперёд по дороге)
    V3 l;          // нормаль влево в плоскости XZ
    float s = 0;   // расстояние от начала
    bool bridge = false;
};

struct Road {
    int id = 0;
    RoadType type = ROAD_MAIN;
    float width = 9;
    bool loop = false;
    std::string name;
    std::vector<RoadPt> pts;
    float length = 0;
    float halfW() const { return width * 0.5f; }
    Surface surface() const { return type == ROAD_DIRT ? SURF_DIRT : SURF_ASPHALT; }
    // Точка на дороге по расстоянию s (с интерполяцией)
    RoadPt at(float s) const;
    int indexAt(float s) const;
};

struct RoadHit {
    int road = -1;
    int idx = 0;       // сегмент [idx, idx+1]
    float t = 0;       // положение внутри сегмента
    float lateral = 0; // смещение влево от центра
    float s = 0;       // расстояние вдоль дороги
    float h = 0;       // высота полотна
    V3 tangent{0, 0, 1};
};

struct GroundHit {
    float h = 0;
    V3 n{0, 1, 0};
    Surface surf = SURF_GRASS;
    int road = -1;
    bool water = false;  // точка ниже уровня воды
    float waterDepth = 0;
};

enum ColShape : uint8_t { COL_CYL, COL_BOX, COL_SPHERE };
struct Collider {
    ColShape shape = COL_CYL;
    V3 c;                  // центр основания (для сферы — центр)
    float r = 0.5f;        // цилиндр/сфера
    float hx = 0, hz = 0;  // полуразмеры коробки
    float yaw = 0, cs = 1, sn = 0;
    float y0 = 0, y1 = 3;  // вертикальный диапазон
    int prop = -1;         // разрушаемый объект
    bool smash = false;    // проезжается насквозь с разрушением
};

// Объекты, которые рендер рисует инстансами
enum PropKind : uint8_t {
    PK_PINE, PK_OAK, PK_BIRCH, PK_PALM, PK_BUSH, PK_ROCK, PK_FENCE, PK_BOARD, PK_CONE, PK_HAY, PK_LAMP,
    PK_TURBINE, PK_POLE, PK_BARRIER, PK_FLAG, PK_COUNT
};
struct Prop {
    PropKind kind = PK_PINE;
    uint8_t var = 0;
    V3 pos;
    float yaw = 0, scale = 1;
    int col = -1;
    bool alive = true;
    float respawn = 0;
};

// Здания и крупные сооружения — рендер склеивает их в статические меши
enum BuildStyle : uint8_t { BS_HOUSE, BS_SHOP, BS_TOWER, BS_BARN, BS_HANGAR, BS_TENT, BS_STAGE, BS_GRANDSTAND, BS_LIGHTHOUSE, BS_OBSERVATORY, BS_TOWERMAST, BS_ARCH, BS_SCREEN };
struct Building {
    BuildStyle style = BS_HOUSE;
    V3 c;             // центр основания
    float hx = 5, hz = 5, h = 6, yaw = 0;
    uint32_t color = 0xffffffff;
    uint8_t var = 0;
};

// Трамплины и платформы, по которым можно ехать
struct Ramp {
    V3 c;
    float hx = 3, hz = 6, yaw = 0, h0 = 0, h1 = 2;  // высота растёт по локальной оси z
    float cs = 1, sn = 0;
    bool contains(float x, float z, float& h) const;
};

// Маршруты кросс-кантри: по ним не растут деревья
struct Trail {
    std::string id, name;
    std::vector<V3> pts;   // шаг 4 м, высоты по рельефу
};

struct Place {
    std::string id, name;
    V3 p;
    float yaw = 0;
};

class World {
public:
    static constexpr int N = 769;              // вершин по стороне
    static constexpr float CELL = 4.0f;        // шаг сетки, м
    static constexpr float ORIGIN = -1536.0f;  // координата первой вершины
    static constexpr float LIMIT = 1440.0f;    // граница игровой зоны
    static constexpr float WATER = 0.0f;

    uint32_t seed = 2026;
    std::vector<float> raw;     // естественный рельеф
    std::vector<float> height;  // рельеф после дорог
    std::vector<V3> normal;
    std::vector<uint8_t> tint;  // по 4 байта: обочина, песок, лесная подстилка, мощение
    std::vector<float> roadDist; // расстояние до края ближайшей дороги (0..60)

    std::vector<Road> roads;
    std::vector<Prop> props;
    std::vector<Building> buildings;
    std::vector<Ramp> ramps;
    std::vector<Collider> cols;
    std::vector<Place> places;
    std::vector<std::vector<V3>> rivers;   // полилинии рек
    std::vector<V3> fords;                 // броды
    std::vector<Trail> trails;
    std::vector<int> boards;               // индексы досок опыта в props
    int roadIndex(const std::string& name) const;
    const Trail* trail(const std::string& id) const;

    void generate(uint32_t seed = 2026);

    // Запросы
    float terrainH(float x, float z) const;
    V3 terrainN(float x, float z) const;
    Surface terrainSurface(float x, float z) const;
    bool roadAt(float x, float z, RoadHit& out, float margin = 0.0f) const;
    bool nearestRoad(float x, float z, float maxDist, RoadHit& out) const;
    GroundHit ground(float x, float z, float yRef) const;
    void queryCols(float x, float z, float r, std::vector<int>& out) const;
    const Place* place(const std::string& id) const;
    float roadDistAt(float x, float z) const;
    bool inBounds(float x, float z) const { return std::fabs(x) < LIMIT && std::fabs(z) < LIMIT; }

    int idx(int i, int j) const { return j * N + i; }
    float vx(int i) const { return ORIGIN + i * CELL; }

    // Статистика генерации
    int treeCount = 0;
    float genMs = 0;

private:
    Noise noise_{2026};
    std::vector<float> blur_;
    // сетка ссылок на сегменты дорог
    static constexpr float RG = 24.0f;
    static constexpr int RGN = 128;
    std::vector<std::vector<uint32_t>> roadGrid_;  // (road << 16) | idx
    // сетка коллайдеров
    static constexpr float CG = 32.0f;
    static constexpr int CGN = 96;
    std::vector<std::vector<int>> colGrid_;

    float naturalH(float x, float z) const;
    float naturalH2(float x, float z, float riverD, float fordK) const;
    float flattenAndCarve(float x, float z, float h) const;
    float sampleGrid(const std::vector<float>& g, float x, float z) const;
    void buildTerrain();
    void buildRoads();
    void addRoad(const std::string& name, RoadType type, const std::vector<V3>& ctrl, bool loop, float smoothM);
    void indexRoads();
    void carveRoads();
    void computeNormals();
    void computeTint();
    void carveTrails();
    std::vector<uint8_t> trackTint_;
    void placeProps();
    void defineTrails();
    bool nearTrail(float x, float z, float r) const;
    void addCol(const Collider& c);
    int addProp(PropKind k, V3 p, float yaw, float scale, uint8_t var = 0);
    void addBuilding(BuildStyle st, V3 c, float hx, float hz, float h, float yaw, uint32_t color, uint8_t var = 0, bool collide = true);
    void addRamp(V3 c, float yaw, float w, float len, float h);
    float riverDist(float x, float z, float& fordK) const;
};

// Катмулл–Ром: гладкая кривая через контрольные точки с шагом step (м)
std::vector<V3> sampleSpline(const std::vector<V3>& ctrl, bool loop, float step);

}  // namespace cl
