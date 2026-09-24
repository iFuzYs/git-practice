// Профиль игрока: уровень, кредиты, гараж, результаты, настройки
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cl {

struct Settings {
    int quality = 1;          // 0 низкое, 1 среднее, 2 высокое
    bool shadows = true;
    bool abs = true, tcs = true, stm = true, autoGear = true, steerAssist = true;
    int line = 1;             // линия движения: 0 выкл, 1 торможение, 2 полная
    int difficulty = 1;       // 0 новичок, 1 средний, 2 про, 3 легенда
    float master = 0.8f, music = 0.55f, sfx = 0.85f;
    bool music_on = true;
    float camSens = 1.0f;
    int camera = 0;           // 0 сзади, 1 далеко, 2 капот, 3 бампер
    float timeSpeed = 1.0f;   // минут игрового времени в секунду
};

struct OwnedCar {
    std::string id;
    uint32_t color = 0xffffffff;
};

struct Profile {
    bool started = false;
    int level = 1;
    int xp = 0;
    int credits = 0;
    int wheelspins = 0;
    std::vector<OwnedCar> garage;
    int current = 0;
    std::map<std::string, int> eventPlace;   // лучшее место
    std::map<std::string, float> stuntBest;
    std::map<std::string, int> stuntStars;
    std::vector<int> boards;                 // разбитые доски
    int bestChain = 0;
    float distanceKm = 0;
    float timeOfDay = 10.5f;                 // часы
    Settings settings;

    static int xpForLevel(int lvl) { return 6000 + (lvl - 1) * 2500; }
    int addXP(int v);                        // сколько уровней получено
    int eventsDone() const;
    int totalStars() const;
    bool owns(const std::string& id) const;
    std::string serialize() const;
    bool parse(const std::string& s);
};

}  // namespace cl
