#include "skills.h"

namespace cl {

void SkillChain::reset() {
    chainPts = 0;
    mult = 1;
    timer = 0;
    active = false;
    current.clear();
    currentPts = 0;
    drifting_ = false;
    driftPts_ = 0;
    airT_ = 0;
    fast_ = false;
    speedPts_ = 0;
    smashInChain_ = 0;
}

void SkillChain::touch() {
    active = true;
    timer = 3.2f;
}

void SkillChain::complete(const std::string& name, float pts) {
    touch();
    chainPts += pts;
    mult = std::min(mult + 1, 9);
    pops.push_back({name, (int)pts, 2.2f});
    if (pops.size() > 4) pops.erase(pops.begin());
}

int SkillChain::update(float dt, const Vehicle& v, int smashes, float crashSpeed, int overtakes) {
    for (auto& p : pops) p.t -= dt;
    while (!pops.empty() && pops.front().t <= 0) pops.erase(pops.begin());
    if (lostT > 0) lostT -= dt;
    frameDriftPts = 0;
    float kmh = v.kmh();
    float angle = std::fabs(v.driftAngle) / DEG;
    bool grounded = v.wheelsOnGround >= 3;

    // дрифт
    if (grounded && angle > 14 && kmh > 32) {
        float p = dt * (angle - 10) * kmh * 0.32f;
        driftPts_ += p;
        frameDriftPts = p;
        drifting_ = true;
        driftEnd_ = 0;
        touch();
    } else if (drifting_) {
        driftEnd_ += dt;
        if (driftEnd_ > 0.35f || !grounded) {
            drifting_ = false;
            if (driftPts_ > 150) {
                complete(driftPts_ > 5000 ? "Великий дрифт" : driftPts_ > 1500 ? "Большой дрифт" : "Дрифт", driftPts_);
                driftsDone++;
            }
            driftPts_ = 0;
        }
    }
    // полёт
    if (v.wheelsOnGround == 0 && !v.upsideDown) {
        airT_ += dt;
        if (airT_ > 0.5f) touch();
    } else if (airT_ > 0) {
        if (airT_ > 0.6f && !v.upsideDown) {
            float pts = airT_ * 650.0f;
            complete(airT_ > 2.2f ? "Затяжной полёт" : airT_ > 1.3f ? "Большой полёт" : "Полёт", pts);
            jumpsDone++;
        }
        airT_ = 0;
    }
    // скорость
    if (kmh > 230) {
        fast_ = true;
        speedPts_ += dt * (kmh - 200) * 3.0f;
        touch();
    } else if (fast_ && kmh < 205) {
        fast_ = false;
        if (speedPts_ > 200) complete("Скорость", speedPts_);
        speedPts_ = 0;
    }
    // разрушение
    for (int i = 0; i < smashes; i++) {
        chainPts += 120;
        smashInChain_++;
        smashTotal++;
        touch();
        if (smashInChain_ % 3 == 0) complete("Разрушение", 200);
    }
    for (int i = 0; i < overtakes; i++) complete("Обгон", 350);

    // текущий навык для подписи
    if (drifting_) { current = "Дрифт"; currentPts = driftPts_; }
    else if (airT_ > 0.5f) { current = "Полёт"; currentPts = airT_ * 650; }
    else if (fast_) { current = "Скорость"; currentPts = speedPts_; }
    else { current.clear(); currentPts = 0; }

    // авария срывает цепочку
    if (crashSpeed > 9.0f && active) {
        lostMsg = "Цепочка сорвана";
        lostT = 2.0f;
        reset();
        return 0;
    }
    if (active) {
        bool inProgress = drifting_ || airT_ > 0.3f || fast_;
        if (!inProgress) timer -= dt;
        if (timer <= 0) {
            int total = (int)((chainPts + 0.5f) * mult);
            bestChain = std::max(bestChain, total);
            reset();
            return total;
        }
    }
    return 0;
}

}  // namespace cl
