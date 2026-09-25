// Эффекты: дым из-под колёс, пыль, брызги, искры, следы шин, обломки
#pragma once
#include <vector>

#include "../sim/vehicle.h"
#include "../world/world.h"
#include "raylib.h"
#include "shaders.h"
#include "textures.h"

namespace cl {

enum PartKind : uint8_t { PT_SMOKE, PT_DUST, PT_SPLASH, PT_SPARK, PT_FLAME, PT_LEAF, PT_CONFETTI };

struct Particle {
    V3 p, v;
    float life = 0, maxLife = 1;
    float size = 1, grow = 0;
    float rot = 0, rotV = 0;
    float drag = 1, grav = 0;
    Color c{255, 255, 255, 255};
    PartKind kind = PT_SMOKE;
};

struct SkidQuad {
    V3 a0, a1, b0, b1;
    uint8_t alpha = 0;
    bool dirt = false;
};

struct Debris {
    V3 p, v, av;
    Q r;
    V3 size{0.3f, 0.3f, 0.3f};
    Color c{255, 255, 255, 255};
    float life = 0;
};

class Effects {
public:
    std::vector<Particle> parts;
    std::vector<SkidQuad> skids;   // кольцевой буфер
    int skidHead = 0;
    std::vector<Debris> debris;
    float quality = 1.0f;          // множитель плотности

    void clear();
    // эффекты от машины за кадр (dt кадра)
    void carFrame(int slot, const Vehicle& v, const World& w, float dt, bool nearCam);
    void update(float dt, const World& w);
    void smash(const Prop& p, V3 carVel);
    void impact(V3 pos, V3 n, float speed);
    void confetti(V3 pos);
    void drawSkids(const FrameLight& L) const;
    void drawParticles(const Camera3D& cam, const TexSet& tex, const FrameLight& L);

private:
    struct WheelTrack { V3 last; bool valid = false; };
    std::vector<WheelTrack> tracks_;   // по 4 на машину
    void spawn(const Particle& p);
    float acc_[64] = {};
};

}  // namespace cl
