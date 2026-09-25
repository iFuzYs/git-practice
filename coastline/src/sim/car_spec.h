// Характеристики машин и классы производительности (PI)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cl {

enum Drive : uint8_t { DRIVE_FWD, DRIVE_RWD, DRIVE_AWD };
enum BodyStyle : uint8_t { BODY_HATCH, BODY_MUSCLE, BODY_RALLY, BODY_ROADSTER, BODY_WAGON, BODY_COUPE, BODY_JDM, BODY_TRUCK, BODY_BUGGY, BODY_SUPER, BODY_TRACK, BODY_HYPER, BODY_CLASSIC, BODY_EV };
enum CarRole : uint8_t { ROLE_ROAD, ROLE_RALLY, ROLE_OFFROAD, ROLE_TRACK };

struct CarSpec {
    std::string id, name, maker;
    int year = 2020;
    int pi = 500;
    int price = 30000;
    BodyStyle body = BODY_HATCH;
    CarRole role = ROLE_ROAD;
    Drive drive = DRIVE_RWD;
    float awdRear = 0.6f;       // доля момента на заднюю ось для полного привода

    float mass = 1300;           // кг
    float powerKW = 150;
    float torqueNm = 280;
    float torqueRpm = 3500, powerRpm = 6000, redline = 7000, idle = 900;
    int gears = 6;
    float gearRatio[9] = {};     // заполняется автоматически
    float finalDrive = 3.5f;
    float topSpeedKmh = 220;     // для подбора передач

    float length = 4.2f, width = 1.8f, height = 1.4f, wheelbase = 2.6f, track = 1.55f;
    float wheelR = 0.32f, wheelW = 0.23f, cgH = 0.5f, frontWeight = 0.55f;

    float grip = 1.0f;           // шины на асфальте
    float looseGrip = 0.85f;     // множитель на грунте, траве, песке
    float cd = 0.33f, clA = 0.1f;  // лобовое сопротивление и прижимная сила
    float brakeTorque = 2400;    // на колесо, Н·м
    float brakeBias = 0.62f;
    float springHz = 1.8f, damping = 0.42f, arb = 0.6f, travel = 0.22f;
    float maxSteerDeg = 36;
    int cylinders = 4;
    float soundTone = 1.0f;      // тембр мотора
    bool electric = false, turbo = false;
    uint32_t color = 0xff3050e0;

    const char* className() const;
    float torqueAt(float rpm) const;
    void finalize();             // передаточные числа, производные величины
};

const std::vector<CarSpec>& carCatalog();
const CarSpec* findCar(const std::string& id);
const char* piClass(int pi);
int classIndex(int pi);         // 0=D ... 6=X

}  // namespace cl
