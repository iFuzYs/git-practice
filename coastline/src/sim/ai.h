// Водитель под управлением компьютера: соперники в гонках и автопилот
#pragma once
#include <vector>

#include "route.h"
#include "vehicle.h"

namespace cl {

class AIDriver {
public:
    const Route* route = nullptr;
    float skill = 1.0f;        // множитель целевой скорости
    float offset = 0;          // желаемое смещение от оси маршрута
    float progress = 0;        // пройденное расстояние с учётом кругов
    int hint = 0;
    bool wantsRespawn = false;
    float lateral = 0;

    void reset(const Route* r, const Vehicle& v, float sStart);
    void prepare(const Vehicle& v);
    Controls drive(const Vehicle& v, float dt, const std::vector<const Vehicle*>& others);
    float localS() const { return lastS_; }
    float targetSpeedAt(float s) const;

private:
    std::vector<float> vt_;
    float lastS_ = 0;
    float stuckT_ = 0, reverseT_ = 0, offT_ = 0, flipT_ = 0;
    int stuckCount_ = 0;
    float avoid_ = 0;
};

}  // namespace cl
