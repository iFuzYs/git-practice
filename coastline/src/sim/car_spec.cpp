#include "car_spec.h"

#include <cmath>

#include "../core/mathx.h"

namespace cl {

const char* piClass(int pi) {
    if (pi <= 500) return "D";
    if (pi <= 600) return "C";
    if (pi <= 700) return "B";
    if (pi <= 800) return "A";
    if (pi <= 900) return "S1";
    if (pi <= 998) return "S2";
    return "X";
}
int classIndex(int pi) {
    if (pi <= 500) return 0;
    if (pi <= 600) return 1;
    if (pi <= 700) return 2;
    if (pi <= 800) return 3;
    if (pi <= 900) return 4;
    if (pi <= 998) return 5;
    return 6;
}
const char* CarSpec::className() const { return piClass(pi); }

float CarSpec::torqueAt(float rpm) const {
    if (electric) {
        // электромотор: полный момент с нуля, затем ограничение мощностью
        float w = std::max(rpm, 1.0f) * TAU / 60.0f;
        return std::min(torqueNm, powerKW * 1000.0f / w) * (1.0f - 0.5f * smoothstep(redline * 0.9f, redline * 1.05f, rpm));
    }
    float t = torqueNm * (rpm < torqueRpm ? mixf(0.6f, 1.0f, smoothstep(idle, torqueRpm, rpm)) : 1.0f);
    float w = std::max(rpm, 300.0f) * TAU / 60.0f;
    float pl = powerKW * 1000.0f / w;
    if (rpm > powerRpm) pl *= 1.0f - 0.55f * smoothstep(powerRpm, redline * 1.06f, rpm);
    return std::max(0.0f, std::min(t, pl));
}

void CarSpec::finalize() {
    // передачи: геометрическая прогрессия, высшая даёт максималку чуть выше отсечки
    float vTop = topSpeedKmh / 3.6f * 1.04f;
    float wheelRpm = vTop / wheelR * 60.0f / TAU;
    float rTop = 0.78f;
    if (electric) gears = 1;
    finalDrive = redline / (wheelRpm * rTop);
    if (gears == 1) {
        gearRatio[1] = rTop;
    } else {
        float r1 = rTop * (gears >= 6 ? 4.4f : gears == 5 ? 3.9f : 3.3f);
        for (int g = 1; g <= gears; g++) {
            float t = (float)(g - 1) / (gears - 1);
            // чуть плотнее высшие передачи
            t = std::pow(t, 0.85f);
            gearRatio[g] = r1 * std::pow(rTop / r1, t);
        }
    }
    gearRatio[0] = -gearRatio[1] * 1.05f;  // задняя
}

static std::vector<CarSpec> buildCatalog() {
    std::vector<CarSpec> v;
    auto add = [&](CarSpec c) { c.finalize(); v.push_back(c); };
    CarSpec c;

    c = CarSpec{};
    c.id = "lastochka"; c.name = "Ласточка RS"; c.maker = "Ласточка"; c.year = 2019; c.pi = 548; c.price = 32000;
    c.body = BODY_HATCH; c.drive = DRIVE_FWD; c.mass = 1180; c.powerKW = 165; c.torqueNm = 300; c.torqueRpm = 2600; c.powerRpm = 6000; c.redline = 6800;
    c.gears = 6; c.topSpeedKmh = 238; c.length = 4.1f; c.width = 1.79f; c.height = 1.42f; c.wheelbase = 2.58f; c.track = 1.54f; c.wheelR = 0.31f;
    c.cgH = 0.5f; c.frontWeight = 0.62f; c.grip = 1.02f; c.looseGrip = 0.86f; c.cd = 0.32f; c.clA = 0.12f; c.brakeTorque = 2200; c.springHz = 2.0f;
    c.cylinders = 4; c.turbo = true; c.soundTone = 1.15f; c.color = 0xffe06a2e; add(c);

    c = CarSpec{};
    c.id = "bison"; c.name = "Бизон 70 SS"; c.maker = "Бизон"; c.year = 1970; c.pi = 612; c.price = 58000;
    c.body = BODY_MUSCLE; c.drive = DRIVE_RWD; c.mass = 1620; c.powerKW = 300; c.torqueNm = 640; c.torqueRpm = 3200; c.powerRpm = 5600; c.redline = 6200;
    c.gears = 4; c.topSpeedKmh = 245; c.length = 4.9f; c.width = 1.92f; c.height = 1.33f; c.wheelbase = 2.85f; c.track = 1.6f; c.wheelR = 0.34f;
    c.cgH = 0.52f; c.frontWeight = 0.55f; c.grip = 0.95f; c.looseGrip = 0.82f; c.cd = 0.42f; c.clA = 0.0f; c.brakeTorque = 2400; c.springHz = 1.5f;
    c.damping = 0.36f; c.maxSteerDeg = 34; c.cylinders = 8; c.soundTone = 0.62f; c.color = 0xff1818d8; add(c);

    c = CarSpec{};
    c.id = "sirocco"; c.name = "Сирокко Ралли"; c.maker = "Сирокко"; c.year = 2021; c.pi = 694; c.price = 76000;
    c.body = BODY_RALLY; c.role = ROLE_RALLY; c.drive = DRIVE_AWD; c.awdRear = 0.55f; c.mass = 1260; c.powerKW = 240; c.torqueNm = 440; c.torqueRpm = 3200;
    c.powerRpm = 6200; c.redline = 7000; c.gears = 6; c.topSpeedKmh = 222; c.length = 4.05f; c.width = 1.82f; c.height = 1.45f; c.wheelbase = 2.55f;
    c.track = 1.58f; c.wheelR = 0.33f; c.cgH = 0.52f; c.frontWeight = 0.58f; c.grip = 1.0f; c.looseGrip = 1.3f; c.cd = 0.36f; c.clA = 0.35f;
    c.brakeTorque = 2300; c.springHz = 1.6f; c.travel = 0.3f; c.cylinders = 4; c.turbo = true; c.soundTone = 1.1f; c.color = 0xff20c8f0; add(c);

    c = CarSpec{};
    c.id = "breeze"; c.name = "Бриз Родстер"; c.maker = "Бриз"; c.year = 2016; c.pi = 571; c.price = 41000;
    c.body = BODY_ROADSTER; c.drive = DRIVE_RWD; c.mass = 1010; c.powerKW = 140; c.torqueNm = 230; c.torqueRpm = 4500; c.powerRpm = 7000; c.redline = 7500;
    c.gears = 6; c.topSpeedKmh = 220; c.length = 3.95f; c.width = 1.73f; c.height = 1.23f; c.wheelbase = 2.31f; c.track = 1.5f; c.wheelR = 0.3f;
    c.cgH = 0.44f; c.frontWeight = 0.51f; c.grip = 1.02f; c.looseGrip = 0.82f; c.cd = 0.34f; c.clA = 0.05f; c.brakeTorque = 1900; c.springHz = 2.1f;
    c.cylinders = 4; c.soundTone = 1.25f; c.color = 0xffc8b820; add(c);

    c = CarSpec{};
    c.id = "turist"; c.name = "Турист Универсал"; c.maker = "Турист"; c.year = 2008; c.pi = 412; c.price = 16000;
    c.body = BODY_WAGON; c.drive = DRIVE_FWD; c.mass = 1480; c.powerKW = 110; c.torqueNm = 220; c.torqueRpm = 3800; c.powerRpm = 5800; c.redline = 6200;
    c.gears = 5; c.topSpeedKmh = 195; c.length = 4.7f; c.width = 1.8f; c.height = 1.5f; c.wheelbase = 2.72f; c.track = 1.54f; c.wheelR = 0.32f;
    c.cgH = 0.56f; c.frontWeight = 0.6f; c.grip = 0.9f; c.looseGrip = 0.88f; c.cd = 0.34f; c.clA = 0.0f; c.brakeTorque = 2000; c.springHz = 1.5f;
    c.damping = 0.34f; c.cylinders = 4; c.soundTone = 0.95f; c.color = 0xff50a0d8; add(c);

    c = CarSpec{};
    c.id = "zarya"; c.name = "Заря 1300"; c.maker = "Заря"; c.year = 1978; c.pi = 305; c.price = 9000;
    c.body = BODY_CLASSIC; c.drive = DRIVE_RWD; c.mass = 960; c.powerKW = 51; c.torqueNm = 92; c.torqueRpm = 3400; c.powerRpm = 5600; c.redline = 6000;
    c.gears = 4; c.topSpeedKmh = 148; c.length = 4.05f; c.width = 1.61f; c.height = 1.44f; c.wheelbase = 2.42f; c.track = 1.36f; c.wheelR = 0.3f;
    c.cgH = 0.55f; c.frontWeight = 0.53f; c.grip = 0.84f; c.looseGrip = 0.95f; c.cd = 0.44f; c.clA = 0.0f; c.brakeTorque = 1300; c.springHz = 1.3f;
    c.damping = 0.3f; c.travel = 0.26f; c.cylinders = 4; c.soundTone = 0.9f; c.color = 0xff84b48c; add(c);

    c = CarSpec{};
    c.id = "vector"; c.name = "Вектор GT"; c.maker = "Вектор"; c.year = 2022; c.pi = 736; c.price = 128000;
    c.body = BODY_COUPE; c.drive = DRIVE_RWD; c.mass = 1470; c.powerKW = 340; c.torqueNm = 530; c.torqueRpm = 4200; c.powerRpm = 7200; c.redline = 7800;
    c.gears = 7; c.topSpeedKmh = 300; c.length = 4.5f; c.width = 1.9f; c.height = 1.3f; c.wheelbase = 2.64f; c.track = 1.6f; c.wheelR = 0.34f;
    c.cgH = 0.47f; c.frontWeight = 0.5f; c.grip = 1.1f; c.looseGrip = 0.8f; c.cd = 0.31f; c.clA = 0.45f; c.brakeTorque = 3000; c.springHz = 2.2f;
    c.cylinders = 6; c.soundTone = 1.0f; c.color = 0xff1a1a1a; add(c);

    c = CarSpec{};
    c.id = "kaizen"; c.name = "Кайдзэн GT-R"; c.maker = "Кайдзэн"; c.year = 2002; c.pi = 781; c.price = 185000;
    c.body = BODY_JDM; c.drive = DRIVE_AWD; c.awdRear = 0.62f; c.mass = 1540; c.powerKW = 410; c.torqueNm = 620; c.torqueRpm = 4400; c.powerRpm = 7000; c.redline = 7800;
    c.gears = 6; c.topSpeedKmh = 305; c.length = 4.6f; c.width = 1.79f; c.height = 1.36f; c.wheelbase = 2.66f; c.track = 1.58f; c.wheelR = 0.34f;
    c.cgH = 0.49f; c.frontWeight = 0.55f; c.grip = 1.12f; c.looseGrip = 0.95f; c.cd = 0.34f; c.clA = 0.55f; c.brakeTorque = 3100; c.springHz = 2.2f;
    c.cylinders = 6; c.turbo = true; c.soundTone = 1.05f; c.color = 0xffd8d8d8; add(c);

    c = CarSpec{};
    c.id = "medved"; c.name = "Медведь 4x4"; c.maker = "Медведь"; c.year = 2020; c.pi = 628; c.price = 92000;
    c.body = BODY_TRUCK; c.role = ROLE_OFFROAD; c.drive = DRIVE_AWD; c.awdRear = 0.55f; c.mass = 2300; c.powerKW = 320; c.torqueNm = 720; c.torqueRpm = 3000;
    c.powerRpm = 5400; c.redline = 6000; c.gears = 6; c.topSpeedKmh = 190; c.length = 5.4f; c.width = 2.08f; c.height = 1.95f; c.wheelbase = 3.3f;
    c.track = 1.75f; c.wheelR = 0.43f; c.wheelW = 0.3f; c.cgH = 0.82f; c.frontWeight = 0.56f; c.grip = 0.92f; c.looseGrip = 1.3f; c.cd = 0.52f;
    c.clA = 0.0f; c.brakeTorque = 3600; c.springHz = 1.35f; c.damping = 0.38f; c.travel = 0.4f; c.cylinders = 8; c.soundTone = 0.7f; c.color = 0xff2c6c3c; add(c);

    c = CarSpec{};
    c.id = "dune"; c.name = "Дюна Багги"; c.maker = "Дюна"; c.year = 2018; c.pi = 664; c.price = 71000;
    c.body = BODY_BUGGY; c.role = ROLE_OFFROAD; c.drive = DRIVE_RWD; c.mass = 820; c.powerKW = 175; c.torqueNm = 260; c.torqueRpm = 4800; c.powerRpm = 7200; c.redline = 7800;
    c.gears = 5; c.topSpeedKmh = 180; c.length = 3.8f; c.width = 1.95f; c.height = 1.5f; c.wheelbase = 2.6f; c.track = 1.7f; c.wheelR = 0.38f; c.wheelW = 0.3f;
    c.cgH = 0.6f; c.frontWeight = 0.42f; c.grip = 0.92f; c.looseGrip = 1.4f; c.cd = 0.5f; c.clA = 0.0f; c.brakeTorque = 1700; c.springHz = 1.3f;
    c.damping = 0.4f; c.travel = 0.42f; c.cylinders = 4; c.soundTone = 1.3f; c.color = 0xff20d8a0; add(c);

    c = CarSpec{};
    c.id = "strela"; c.name = "Стрела V12"; c.maker = "Стрела"; c.year = 2023; c.pi = 868; c.price = 460000;
    c.body = BODY_SUPER; c.drive = DRIVE_RWD; c.mass = 1560; c.powerKW = 570; c.torqueNm = 720; c.torqueRpm = 5500; c.powerRpm = 8200; c.redline = 8800;
    c.gears = 7; c.topSpeedKmh = 340; c.length = 4.7f; c.width = 2.0f; c.height = 1.18f; c.wheelbase = 2.7f; c.track = 1.68f; c.wheelR = 0.35f; c.wheelW = 0.3f;
    c.cgH = 0.43f; c.frontWeight = 0.45f; c.grip = 1.18f; c.looseGrip = 0.75f; c.cd = 0.33f; c.clA = 1.0f; c.brakeTorque = 3800; c.springHz = 2.5f;
    c.cylinders = 12; c.soundTone = 1.2f; c.color = 0xff1030ff; add(c);

    c = CarSpec{};
    c.id = "burya"; c.name = "Буря Трек"; c.maker = "Буря"; c.year = 2024; c.pi = 893; c.price = 620000;
    c.body = BODY_TRACK; c.role = ROLE_TRACK; c.drive = DRIVE_RWD; c.mass = 1120; c.powerKW = 430; c.torqueNm = 560; c.torqueRpm = 5800; c.powerRpm = 8400; c.redline = 9000;
    c.gears = 7; c.topSpeedKmh = 310; c.length = 4.35f; c.width = 1.98f; c.height = 1.14f; c.wheelbase = 2.6f; c.track = 1.66f; c.wheelR = 0.33f; c.wheelW = 0.31f;
    c.cgH = 0.4f; c.frontWeight = 0.43f; c.grip = 1.3f; c.looseGrip = 0.68f; c.cd = 0.42f; c.clA = 2.2f; c.brakeTorque = 3900; c.springHz = 2.9f;
    c.cylinders = 6; c.turbo = true; c.soundTone = 1.35f; c.color = 0xff00d0ff; add(c);

    c = CarSpec{};
    c.id = "komet"; c.name = "Комета ЭВ"; c.maker = "Комета"; c.year = 2025; c.pi = 824; c.price = 310000;
    c.body = BODY_EV; c.drive = DRIVE_AWD; c.awdRear = 0.58f; c.mass = 2150; c.powerKW = 620; c.torqueNm = 1100; c.redline = 16000; c.powerRpm = 12000;
    c.topSpeedKmh = 260; c.length = 5.0f; c.width = 1.97f; c.height = 1.44f; c.wheelbase = 2.97f; c.track = 1.66f; c.wheelR = 0.35f; c.cgH = 0.45f;
    c.frontWeight = 0.5f; c.grip = 1.08f; c.looseGrip = 0.85f; c.cd = 0.24f; c.clA = 0.3f; c.brakeTorque = 3600; c.springHz = 1.9f; c.electric = true;
    c.cylinders = 0; c.soundTone = 1.0f; c.color = 0xfff0f0f0; add(c);

    c = CarSpec{};
    c.id = "fantom"; c.name = "Фантом Гибрид"; c.maker = "Фантом"; c.year = 2026; c.pi = 958; c.price = 2200000;
    c.body = BODY_HYPER; c.drive = DRIVE_AWD; c.awdRear = 0.68f; c.mass = 1590; c.powerKW = 880; c.torqueNm = 1150; c.torqueRpm = 4500; c.powerRpm = 8200; c.redline = 9000;
    c.gears = 7; c.topSpeedKmh = 390; c.length = 4.75f; c.width = 2.05f; c.height = 1.14f; c.wheelbase = 2.72f; c.track = 1.7f; c.wheelR = 0.36f; c.wheelW = 0.32f;
    c.cgH = 0.42f; c.frontWeight = 0.44f; c.grip = 1.25f; c.looseGrip = 0.82f; c.cd = 0.34f; c.clA = 1.6f; c.brakeTorque = 4300; c.springHz = 2.6f;
    c.cylinders = 8; c.turbo = true; c.soundTone = 1.1f; c.color = 0xff202020; add(c);

    return v;
}

const std::vector<CarSpec>& carCatalog() {
    static std::vector<CarSpec> cat = buildCatalog();
    return cat;
}

const CarSpec* findCar(const std::string& id) {
    for (auto& c : carCatalog())
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace cl
