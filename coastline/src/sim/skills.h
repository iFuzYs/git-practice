// Цепочки навыков: дрифт, полёт, разрушение, скорость, обгоны
#pragma once
#include <string>
#include <vector>

#include "vehicle.h"

namespace cl {

struct SkillPop {
    std::string name;
    int pts = 0;
    float t = 0;   // время жизни на экране
};

class SkillChain {
public:
    float chainPts = 0;   // очки цепочки без множителя
    int mult = 1;
    float timer = 0;      // до «фиксации» цепочки
    bool active = false;
    std::string current;  // навык в процессе
    float currentPts = 0;
    std::vector<SkillPop> pops;
    std::string lostMsg;  // «цепочка сорвана»
    float lostT = 0;
    float frameDriftPts = 0;
    int bestChain = 0;
    int driftsDone = 0, jumpsDone = 0, smashTotal = 0;

    // возвращает очки зафиксированной цепочки (0, если не было)
    int update(float dt, const Vehicle& v, int smashes, float crashSpeed, int overtakes);
    void reset();

private:
    bool drifting_ = false;
    float driftPts_ = 0, driftEnd_ = 0;
    float airT_ = 0;
    bool fast_ = false;
    float speedPts_ = 0;
    int smashInChain_ = 0;
    void complete(const std::string& name, float pts);
    void touch();
};

}  // namespace cl
