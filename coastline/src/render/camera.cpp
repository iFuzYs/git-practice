#include "camera.h"

namespace cl {

const char* camModeName(int m) {
    switch (m) {
        case CAM_CHASE: return "Сзади";
        case CAM_FAR: return "Сзади, далеко";
        case CAM_HOOD: return "Капот";
        case CAM_BUMPER: return "Бампер";
    }
    return "";
}

void CameraRig::reset(const Vehicle& v) {
    init_ = false;
    lookYaw_ = lookPitch_ = 0;
    velF_ = v.vel;
    yaw_ = v.yaw();
}

void CameraRig::update(float dt, const Vehicle& v, const World& w, const CameraInput& in) {
    dt = clampf(dt, 0.0f, 0.1f);
    const CarSpec& S = *v.spec;
    V3 f = v.fwd();
    float carYaw = std::atan2(f.x, f.z);
    velF_ = lerp(velF_, v.vel, 1.0f - std::exp(-dt * 6.0f));
    // направление взгляда: по скорости при заносе (как в «Горизонте»), иначе по кузову
    float spd = len(V3{velF_.x, 0, velF_.z});
    float velYaw = spd > 3.0f ? std::atan2(velF_.x, velF_.z) : carYaw;
    float diff = wrapAngle(velYaw - carYaw);
    if (v.fwdSpeed < -1.0f) diff = 0;  // задний ход: остаёмся за кузовом
    float targetYaw = carYaw + clampf(diff, -0.9f, 0.9f) * 0.55f;
    if (in.lookBack) targetYaw += PI;
    if (!init_) {
        yaw_ = targetYaw;
        init_ = true;
    }
    float yawRate = mode == CAM_HOOD || mode == CAM_BUMPER ? 30.0f : 5.2f;
    yaw_ += wrapAngle(targetYaw - yaw_) * (1.0f - std::exp(-dt * yawRate));
    // свободный взгляд мышью с возвратом
    if (in.mouseActive) {
        lookYaw_ = clampf(lookYaw_ + in.lookX, -PI, PI);
        lookPitch_ = clampf(lookPitch_ + in.lookY, -0.35f, 0.7f);
    } else {
        lookYaw_ *= std::exp(-dt * 2.5f);
        lookPitch_ *= std::exp(-dt * 2.5f);
    }
    float kmh = v.kmh();
    speedFov_ = mixf(speedFov_, clampf((kmh - 60) / 220.0f, 0.0f, 1.0f) * 12.0f, 1.0f - std::exp(-dt * 2.0f));
    cam.fovy = fov + speedFov_;
    cam.up = {0, 1, 0};
    cam.projection = CAMERA_PERSPECTIVE;

    float yawT = yaw_ + lookYaw_;
    V3 dir{std::sin(yawT), 0, std::cos(yawT)};
    V3 focus = v.pos + V3{0, S.height * 0.55f - S.cgH + 0.2f, 0};
    if (mode == CAM_CHASE || mode == CAM_FAR) {
        float d = (mode == CAM_CHASE ? 4.6f : 7.2f) + S.length * 0.55f;
        float h = (mode == CAM_CHASE ? 1.25f : 2.1f) + S.height * 0.3f;
        d += clampf(kmh / 300.0f, 0.0f, 1.0f) * 0.9f;
        float pitch = pitch_ + lookPitch_;
        V3 want = focus - dir * (d * std::cos(pitch)) + V3{0, h + d * std::sin(pitch) * 0.6f, 0};
        // плавное следование с учётом скорости машины (без отставания на прямой)
        if (len2(p_ - want) > 400.0f) p_ = want;
        V3 rel = p_ - v.pos;
        V3 relW = want - v.pos;
        rel = lerp(rel, relW, 1.0f - std::exp(-dt * 9.0f));
        p_ = v.pos + rel;
        // не опускаться под землю и не залезать в склон
        GroundHit g = w.ground(p_.x, p_.z, p_.y + 2.0f);
        float minY = std::max(g.h, World::WATER - 0.2f) + 0.6f;
        if (p_.y < minY) p_.y = minY;
        t_ = focus + dir * 2.0f + V3{0, 0.25f, 0};
    } else if (mode == CAM_HOOD) {
        V3 up = v.up();
        p_ = v.toWorld({0, S.height - S.cgH - 0.12f, S.length * 0.12f});
        V3 look = norm(v.fwd() * std::cos(lookYaw_) + v.left() * std::sin(lookYaw_) + V3{0, lookPitch_ - 0.04f, 0});
        if (in.lookBack) look = -look;
        t_ = p_ + look * 10.0f;
        cam.up = {up.x, up.y, up.z};
    } else {
        V3 up = v.up();
        p_ = v.toWorld({0, 0.55f - S.cgH, S.length * 0.5f + 0.05f});
        V3 look = norm(v.fwd() * std::cos(lookYaw_) + v.left() * std::sin(lookYaw_) + V3{0, lookPitch_, 0});
        if (in.lookBack) {
            look = -look;
            p_ = v.toWorld({0, 0.7f - S.cgH, -S.length * 0.5f - 0.1f});
        }
        t_ = p_ + look * 10.0f;
        cam.up = {up.x, up.y, up.z};
    }
    // тряска при ударах и на высокой скорости
    shake = std::max(0.0f, shake - dt * 2.5f);
    float sh = shake * 0.15f + clampf((kmh - 200) / 200.0f, 0.0f, 1.0f) * 0.012f;
    V3 jitter{0, 0, 0};
    if (sh > 0) {
        float t = (float)GetTime();
        jitter = V3{std::sin(t * 47.0f), std::sin(t * 53.0f + 1.3f), std::sin(t * 41.0f + 2.1f)} * sh;
    }
    cam.position = {p_.x + jitter.x, p_.y + jitter.y, p_.z + jitter.z};
    cam.target = {t_.x, t_.y, t_.z};
}

void CameraRig::orbit(float dt, V3 center, float radius, float height, float speed) {
    orbitA_ += dt * speed;
    V3 p = center + V3{std::sin(orbitA_) * radius, height, std::cos(orbitA_) * radius};
    cam.position = {p.x, p.y, p.z};
    cam.target = {center.x, center.y + 0.6f, center.z};
    cam.up = {0, 1, 0};
    cam.fovy = 45;
    cam.projection = CAMERA_PERSPECTIVE;
    init_ = false;
}

}  // namespace cl
