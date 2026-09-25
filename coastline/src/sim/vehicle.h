// Физика машины: твёрдое тело, лучевая подвеска, шины с комбинированным трением,
// двигатель, коробка, привод, аэродинамика, столкновения
#pragma once
#include <vector>

#include "../core/mathx.h"
#include "../world/world.h"
#include "car_spec.h"

namespace cl {

struct Controls {
    float throttle = 0, brake = 0, steer = 0;  // steer > 0 — влево
    bool handbrake = false;
    bool shiftUp = false, shiftDown = false;
};

struct Assists {
    bool abs = true, tcs = true, stm = true, autoGear = true, steerAssist = true;
};

struct WheelState {
    V3 local;             // точка крепления подвески в системе машины
    bool front = false, left = false, driven = false;
    float radius = 0.32f;
    float rest = 0.32f;   // длина подвески от крепления до центра колеса без нагрузки
    float comp = 0, prevComp = 0;
    float omega = 0, spin = 0, steer = 0;
    bool contact = false;
    V3 contactP, contactN{0, 1, 0};
    Surface surf = SURF_GRASS;
    float load = 0, slipRatio = 0, slipAngle = 0, slipF = 0;
    float fx = 0, fy = 0;
    float skid = 0;       // 0..1: интенсивность юза для следов и звука
    bool water = false;
    float dbgDrive = 0, dbgRoll = 0, dbgIw = 0;
};

struct SmashEvent { int prop; float speed; V3 pos; };
struct ImpactEvent { float speed; V3 pos; bool car; };

class Vehicle {
public:
    const CarSpec* spec = nullptr;
    V3 pos;
    Q rot;
    V3 vel, angVel;
    WheelState w[4];
    float rpm = 900;
    int gear = 1;           // -1 задняя, 0 нейтраль, 1..n
    float shiftT = 0;
    float clutch = 1;
    float tcsCut = 1;
    Controls in;
    Assists as;
    float mass = 1300, invMass = 1 / 1300.0f;
    V3 invI;
    float gripScale = 1.0f;   // для ИИ и помощников
    float powerScale = 1.0f;
    uint32_t color = 0xffffffff;
    int id = 0;
    bool ai = false;

    // производные величины (обновляются каждый шаг)
    float speed = 0, fwdSpeed = 0, latSpeed = 0, driftAngle = 0, yawRate = 0;
    float airTime = 0;
    int wheelsOnGround = 0;
    bool upsideDown = false;
    float waterDepth = 0;
    float squeal = 0;
    Surface surfUnder = SURF_ASPHALT;
    bool onRoad = true;
    int roadId = -1;
    float brakeLights = 0;
    float boostHint = 0;     // «хлопки» при сбросе газа (для звука)
    float engineLoad = 0;
    float damage = 0;        // визуальная грязь/царапины
    float wheelSpeedMs = 0;

    std::vector<SmashEvent> smashes;
    std::vector<ImpactEvent> impacts;

    void init(const CarSpec* s, V3 p, float yaw);
    void teleport(V3 p, float yaw);
    void step(float dt, const World& world);

    V3 fwd() const { return qrot(rot, {0, 0, 1}); }
    V3 up() const { return qrot(rot, {0, 1, 0}); }
    V3 left() const { return qrot(rot, {1, 0, 0}); }
    V3 toWorld(V3 local) const { return pos + qrot(rot, local); }
    V3 pointVel(V3 wp) const { return vel + cross(angVel, wp - pos); }
    float yaw() const { V3 f = fwd(); return std::atan2(f.x, f.z); }
    V3 invInertiaApply(V3 t) const;
    void applyImpulse(V3 p, V3 j);
    float gearRatio(int g) const;
    int maxGear() const { return spec->gears; }
    float kmh() const { return speed * 3.6f; }

    // Снимок для перемотки
    struct Snap {
        V3 pos, vel, angVel;
        Q rot;
        float rpm;
        int gear;
        float omega[4], comp[4];
    };
    Snap snap() const;
    void restore(const Snap& s);

    // Сферы кузова для столкновений (в мировых координатах)
    int bodySpheres(V3* out, float& r) const;

private:
    std::vector<int> colScratch_;
    float kS_[4] = {}, cD_[4] = {}, inertia_[4] = {1, 1, 1, 1};
    float steerAngle_ = 0;
    float revTimer_ = 0;
    void drivetrain(float dt, float* driveT);
    void collideStatic(float dt, const World& world);
    void collideGround(float dt, const World& world);
};

// Столкновение двух машин (импульсы)
bool collideCars(Vehicle& a, Vehicle& b);

}  // namespace cl
