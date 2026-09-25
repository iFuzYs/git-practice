// Модели объектов мира: деревья, камни, ограждения, здания, трамплины
#pragma once
#include "../world/world.h"
#include "meshgen.h"

namespace cl {

// Флаги в альфе цвета вершины (см. FS_OBJECT)
constexpr uint8_t A_LIT = 255, A_FOLIAGE = 220, A_NIGHT = 170, A_EMIT = 120;
constexpr uint8_t A_FACADE_HOUSE = 10, A_FACADE_SHOP = 20, A_FACADE_OFFICE = 30, A_FACADE_GLASS = 40;

constexpr int PROP_VARS = 5;

// Геометрия объекта в локальных координатах (основание в нуле, масштаб — через матрицу).
// lod: 0 — вблизи, 1 — упрощённая для дальнего плана
void propGeometry(MeshBuilder& mb, PropKind k, int var, int lod);
int propVariants(PropKind k);
// Ротор ветряка: ступица в нуле, лопасти в плоскости XY
void rotorGeometry(MeshBuilder& mb);
// Здание в мировых координатах
void buildingGeometry(MeshBuilder& mb, const Building& b);
// Трамплин в мировых координатах
void rampGeometry(MeshBuilder& mb, const Ramp& r);

}  // namespace cl
