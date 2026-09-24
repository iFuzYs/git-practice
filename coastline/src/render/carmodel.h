// Процедурные модели машин
#pragma once
#include "../core/mathx.h"
#include "../sim/car_spec.h"
#include "raylib.h"

namespace cl {

// Материалы кузова кодируются в texcoord.x: 0 краска, 1 стекло, 2 пластик, 3 хром, 4 фары, 5 задние фонари, 6 резина, 7 задний ход, 8 днище/салон
struct CarModel {
    Mesh body{};
    Mesh wheel{};
    bool ready = false;
    // точки фар и фонарей в системе машины (для свечения)
    V3 head[2], tail[2];
    float zFront = 2, zRear = -2;
};

CarModel buildCarModel(const CarSpec& s);

}  // namespace cl
