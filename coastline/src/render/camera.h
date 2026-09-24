// Камеры: сзади (ближняя/дальняя), капот, бампер, свободный облёт в меню
#pragma once
#include "../sim/vehicle.h"
#include "../world/world.h"
#include "raylib.h"

namespace cl {

enum CamMode { CAM_CHASE = 0, CAM_FAR = 1, CAM_HOOD = 2, CAM_BUMPER = 3, CAM_COUNT = 4 };
const char* camModeName(int m);

struct CameraInput {
    float lookX = 0, lookY = 0;   // смещение мышью/стиком (рад)
    bool lookBack = false;
    bool mouseActive = false;
};

class CameraRig {
public:
    Camera3D cam{};
    int mode = CAM_CHASE;
    float fov = 62;
    void reset(const Vehicle& v);
    void update(float dt, const Vehicle& v, const World& w, const CameraInput& in);
    // облёт вокруг точки (меню, гараж, финиш)
    void orbit(float dt, V3 center, float radius, float height, float speed);
    V3 pos() const { return {cam.position.x, cam.position.y, cam.position.z}; }
    V3 target() const { return {cam.target.x, cam.target.y, cam.target.z}; }
    float shake = 0;

private:
    V3 p_, t_;
    float yaw_ = 0, pitch_ = 0.12f, dist_ = 6.5f;
    float lookYaw_ = 0, lookPitch_ = 0;
    float speedFov_ = 0;
    float orbitA_ = 0;
    bool init_ = false;
    V3 velF_;
};

}  // namespace cl
