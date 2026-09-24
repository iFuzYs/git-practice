// Шейдеры и общие параметры освещения
#pragma once
#include "../core/mathx.h"
#include "raylib.h"

namespace cl {

struct FrameLight {
    V3 sunDir{0.3f, 0.8f, 0.2f};   // направление на солнце (или луну ночью)
    V3 sunCol{1, 1, 1};
    V3 skyCol{0.4f, 0.5f, 0.7f};
    V3 gndCol{0.2f, 0.2f, 0.15f};
    V3 fogCol{0.7f, 0.8f, 0.9f};
    V3 fogSun{1, 0.9f, 0.7f};
    V3 zenith{0.2f, 0.4f, 0.8f}, horizon{0.7f, 0.8f, 0.9f};
    float fogDen = 0.0006f;
    V3 camPos;
    float night = 0;
    float time = 0;
    float exposure = 1;
    float cloud = 0.4f;
    Matrix shadowVP{};
    bool shadowOn = false;
    unsigned int shadowTex = 0;
    V3 headPos, headDir;
    float headOn = 0;
    V3 pointPos[8];
    V3 pointCol[8];
    int pointCount = 0;
    V3 realSun{0, 1, 0};           // настоящее солнце (для неба)
    V3 moonDir{0, 1, 0};
};

struct Shaders {
    Shader terrain{}, road{}, object{}, objectInst{}, car{}, water{}, sky{}, particle{}, depth{}, depthInst{}, minimap{}, gate{};
    void load();
    void unload();
    void setLight(const FrameLight& L);
    void setSkyMatrix(const Matrix& invVP);
    // привязка текстуры к слоту для всех последующих рисований
    static void bind(int slot, unsigned int texId);
};

extern const int SLOT_SHADOW, SLOT_DEPTHMAP;
int shaderLoc(Shader s, const char* name);  // с кэшем

}  // namespace cl
