// Процедурные текстуры
#pragma once
#include "raylib.h"

namespace cl {

struct TexSet {
    Texture2D grass{}, rock{}, sand{}, gravel{}, paved{};
    Texture2D road[5]{};   // по RoadType
    Texture2D facade{};
    Texture2D particle{}, spark{}, glow{};
    Texture2D logo{};      // баннер фестиваля
    Texture2D flag[5]{};
    void build(Font display, int quality);
    void unload();
};

}  // namespace cl
