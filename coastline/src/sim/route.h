// Маршруты гонок: собираются из участков дорог или троп кросс-кантри
#pragma once
#include <string>
#include <vector>

#include "../world/world.h"

namespace cl {

struct RoutePt {
    V3 p, t, l;
    float s = 0;
    float halfW = 4;   // половина доступной ширины
    Surface surf = SURF_ASPHALT;
    float curv = 0;    // кривизна, 1/м
};

struct Route {
    std::vector<RoutePt> pts;
    float length = 0;
    bool loop = false;
    RoutePt at(float s) const;
    int indexAt(float s) const;
    // проекция точки на маршрут (поиск вокруг подсказки)
    float project(V3 p, int& hint, float* lateral = nullptr) const;
};

struct Leg {
    std::string road;
    float x0, z0, x1, z1;
    int dir = 0;  // для кольцевых дорог: +1 по возрастанию, -1 против
};

bool buildRoadRoute(const World& w, const std::vector<Leg>& legs, bool loop, Route& out, std::string* err = nullptr);
void buildTrailRoute(const World& w, const Trail& t, Route& out);
void finalizeRoute(Route& r);

}  // namespace cl
