// Рендер мира: рельеф, дороги, вода, небо, объекты, машины, тени и эффекты
#pragma once
#include <map>
#include <string>
#include <vector>

#include "../sim/vehicle.h"
#include "../world/world.h"
#include "carmodel.h"
#include "effects.h"
#include "propmodels.h"
#include "shaders.h"
#include "textures.h"

namespace cl {

struct CarDraw {
    const CarSpec* spec = nullptr;
    V3 pos;
    Q rot;
    V3 wheelC[4];
    float steer[4] = {0, 0, 0, 0}, spin[4] = {0, 0, 0, 0};
    uint32_t color = 0xffffffff;
    float lights = 0, brake = 0, reverse = 0, dirt = 0;
    bool shadow = true;
};
CarDraw carDrawFrom(const Vehicle& v, float lights);
CarDraw carDrawStatic(const CarSpec* s, V3 ground, float yaw, uint32_t color);

struct MarkerDraw {
    V3 p;
    Color c{255, 160, 40, 255};
    float radius = 5, height = 60;
};
struct GateDraw {
    V3 p, l;
    float halfW = 6;
    bool next = false, finish = false;
};
struct LinePt {
    V3 p, t, l;
    Color c;
};

struct Scene {
    Camera3D cam{};
    FrameLight L;
    std::vector<CarDraw> cars;
    std::vector<MarkerDraw> markers;
    std::vector<GateDraw> gates;
    std::vector<LinePt> line;
    Effects* fx = nullptr;
    bool plane = false;
    V3 planePos;
    Q planeRot;
    float time = 0;
    float aspect = 16.0f / 9.0f;
};

class Renderer {
public:
    TexSet tex;
    Shaders sh;
    Texture2D waterDepth{};
    int quality = 1;
    bool shadows = true;
    int drawCalls = 0, instanceCount = 0;

    void init(const World& w, Font display, int quality, bool shadows);
    void unload();
    void setQuality(int q, bool shadows);
    void render(Scene& sc);
    const CarModel& carModel(const CarSpec* s);
    Matrix view() const { return view_; }
    Matrix proj() const { return proj_; }

private:
    struct SMesh {
        Mesh m{};
        V3 mn, mx;
    };
    struct Glow {
        V3 p;
        Color c;
        float size;
        uint8_t mode;  // 0 ночью, 1 всегда, 2 мигает
        int prop = -1;
        float maxD = 600;
    };
    const World* w_ = nullptr;
    // рельеф: 12×12 участков по 64 клетки, два уровня детализации
    std::vector<SMesh> terr_[2];
    std::vector<SMesh> outer_;
    std::vector<SMesh> roads_[5];
    std::vector<SMesh> statics_;
    // деревья дальнего плана: по ячейкам 128 м
    static constexpr int PC = 24;
    static constexpr float PCS = 128.0f;
    std::vector<std::vector<int>> cellProps_;
    std::vector<V3> cellMin_, cellMax_;
    std::vector<std::vector<SMesh>> far_;
    Mesh prop_[PK_COUNT][PROP_VARS]{};
    bool propOk_[PK_COUNT][PROP_VARS]{};
    Mesh rotor_{}, cube_{}, sky_{}, water_{}, plane_{}, balloon_{};
    std::vector<int> turbines_, lamps_, poles_;
    std::vector<Glow> glows_;
    Material mTerrain_{}, mRoad_[5]{}, mObj_{}, mInst_{}, mCar_{}, mWater_{}, mSky_{}, mDepth_{}, mDepthInst_{};
    RenderTexture2D shadowRT_{};
    int shadowSize_ = 0;
    float shadowHalf_ = 90;
    std::map<std::string, CarModel> cars_;
    Matrix view_{}, proj_{}, vp_{};
    float planes_[6][4] = {};
    std::vector<Matrix> inst_[PK_COUNT][PROP_VARS];

    SMesh uploadMB(const MeshBuilder& mb);
    void buildTerrain();
    void buildOuter();
    void buildRoads();
    void buildStatics();
    void buildProps();
    void buildShadow();
    void freeShadow();
    void setFrustum(const Matrix& vp);
    bool visible(V3 mn, V3 mx) const;
    void gatherProps(V3 cam, bool shadowPass, V3 focus, float radius);
    void drawProps(const Material& m);
    void drawCars(const Scene& sc, bool depth);
    void drawGlows(const Scene& sc);
    void drawBeams(const Scene& sc);
    void drawMarkers(const Scene& sc);
    void drawLine(const Scene& sc);
    void drawDebris(const Scene& sc);
    void drawDecor(const Scene& sc, bool depth);
    void choosePointLights(Scene& sc);
    int cellOf(float x, float z) const;
};

// Матрица из позиции и поворота
Matrix matFrom(V3 p, Q q, float s = 1.0f);

}  // namespace cl
