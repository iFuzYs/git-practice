#include "ai.h"

namespace cl {

static const float AI_MU[SURF_COUNT] = {1.0f, 0.74f, 0.7f, 0.62f, 0.52f, 0.82f, 0.45f, 0.93f};
static bool aiLoose(Surface s) { return s == SURF_DIRT || s == SURF_GRAVEL || s == SURF_GRASS || s == SURF_SAND || s == SURF_WATER; }

void AIDriver::reset(const Route* r, const Vehicle& v, float sStart) {
    route = r;
    hint = r->indexAt(sStart);
    lastS_ = sStart;
    progress = sStart;
    stuckT_ = reverseT_ = offT_ = flipT_ = 0;
    stuckCount_ = 0;
    wantsRespawn = false;
    prepare(v);
}

// Целевые скорости: предел по кривизне и прижимной силе, затем «обратный проход» по торможению
void AIDriver::prepare(const Vehicle& v) {
    const CarSpec& S = *v.spec;
    int n = (int)route->pts.size();
    vt_.assign(n, 0);
    float vcap = S.topSpeedKmh / 3.6f;
    float c = S.clA * 0.6125f / S.mass;
    for (int i = 0; i < n; i++) {
        const RoutePt& p = route->pts[i];
        float mu = S.grip * AI_MU[p.surf] * (aiLoose(p.surf) ? S.looseGrip : 1.0f) * 0.86f;
        float k = std::max(p.curv, 1e-4f);
        float den = k - mu * c;
        float vc = den > 1e-5f ? std::sqrt(mu * G / den) : vcap;
        vt_[i] = std::min(vc, vcap);
    }
    for (int pass = 0; pass < (route->loop ? 2 : 1); pass++)
        for (int k = n - 2 + (route->loop ? 1 : 0); k >= 0; k--) {
            int i = k % n, j = (k + 1) % n;
            const RoutePt& p = route->pts[i];
            float mu = S.grip * AI_MU[p.surf] * (aiLoose(p.surf) ? S.looseGrip : 1.0f);
            float ab = mu * G * 0.72f;
            float ds = std::max(0.5f, std::fabs(route->pts[j].s - p.s));
            if (j == 0 && route->loop) ds = std::max(0.5f, route->length - p.s);
            vt_[i] = std::min(vt_[i], std::sqrt(vt_[j] * vt_[j] + 2 * ab * ds));
        }
}

float AIDriver::targetSpeedAt(float s) const {
    int i = route->indexAt(s);
    return vt_.empty() ? 20 : vt_[i];
}

Controls AIDriver::drive(const Vehicle& v, float dt, const std::vector<const Vehicle*>& others) {
    Controls c;
    const CarSpec& S = *v.spec;
    const Route& R = *route;
    float spd = v.speed;
    float lat = 0;
    float s = R.project(v.pos, hint, &lat);
    lateral = lat;
    // прогресс с учётом кругов
    float ds = s - lastS_;
    if (R.loop) {
        if (ds < -R.length * 0.5f) ds += R.length;
        if (ds > R.length * 0.5f) ds -= R.length;
    }
    if (std::fabs(ds) < 60) progress += ds;
    lastS_ = s;

    V3 f = v.fwd(), l = v.left();
    // объезд машин впереди
    float wantOff = offset;
    float brakeFor = 1e9f;
    for (const Vehicle* o : others) {
        if (o == &v) continue;
        V3 d = o->pos - v.pos;
        float ahead = dot(d, f), side = dot(d, l);
        if (ahead > 0 && ahead < 22 && std::fabs(side) < 2.6f) {
            float rel = spd - dot(o->vel, f);
            if (rel > -1.0f) {
                // уходим в сторону, где больше места
                float room = R.at(s).halfW;
                float dir = side > 0 ? -1.0f : 1.0f;
                if (std::fabs(offset + dir * 3.0f) > room) dir = -dir;
                wantOff = clampf(offset + dir * 3.0f, -room, room);
                if (ahead < 9 && rel > 2) brakeFor = std::min(brakeFor, dot(o->vel, f) + 1.0f);
            }
        }
    }
    avoid_ = damp(avoid_, wantOff, 1.6f, dt);

    // точка прицеливания
    float L = clampf(7.0f + spd * 0.55f, 9.0f, 42.0f);
    RoutePt tp = R.at(s + L);
    float room = tp.halfW;
    V3 target = tp.p + tp.l * clampf(avoid_, -room, room);
    V3 d = target - v.pos;
    float alpha = std::atan2(dot(d, l), dot(d, f));
    float delta = std::atan(2.0f * S.wheelbase * std::sin(alpha) / L);
    float maxSt = S.maxSteerDeg * DEG * mixf(1.0f, 0.28f, clamp01((spd - 5.0f) / 45.0f));
    c.steer = clampf(delta / maxSt, -1.0f, 1.0f);

    // скорость: минимум целевой на ближайшем отрезке
    float look = spd * 0.5f + 4.0f;
    float tgt = 1e9f;
    for (float q = 0; q <= look; q += 4.0f) tgt = std::min(tgt, targetSpeedAt(s + q));
    tgt *= skill;
    tgt = std::min(tgt, brakeFor);
    if (std::fabs(v.driftAngle) > 0.3f) tgt = std::min(tgt, spd * 0.95f);
    if (spd < tgt - 0.8f) {
        c.throttle = clampf((tgt - spd) * 0.35f + 0.35f, 0.25f, 1.0f);
    } else if (spd > tgt + 1.2f) {
        c.throttle = 0;
        c.brake = clampf((spd - tgt) * 0.14f, 0.1f, 1.0f);
    } else {
        c.throttle = 0.4f;
    }
    // сильный занос — отпускаем газ
    if (std::fabs(v.driftAngle) > 0.45f) c.throttle *= 0.4f;

    // застряли: сдаём назад
    if (reverseT_ > 0) {
        reverseT_ -= dt;
        c.throttle = 0;
        c.brake = 1;
        c.steer = -c.steer;
        if (reverseT_ <= 0) stuckT_ = 0;
    } else if (spd < 1.5f && c.throttle > 0.3f) {
        stuckT_ += dt;
        if (stuckT_ > 2.5f) {
            reverseT_ = 1.4f;
            stuckCount_++;
        }
    } else {
        stuckT_ = std::max(0.0f, stuckT_ - dt);
        if (spd > 8) stuckCount_ = 0;
    }
    // слетели с маршрута или перевернулись — просим респаун
    offT_ = std::fabs(lat) > R.at(s).halfW + 14.0f ? offT_ + dt : 0;
    flipT_ = v.upsideDown ? flipT_ + dt : 0;
    if (offT_ > 4 || flipT_ > 2.0f || stuckCount_ >= 3 || v.waterDepth > 1.1f) {
        wantsRespawn = true;
        stuckCount_ = 0;
        offT_ = flipT_ = 0;
    }
    return c;
}

}  // namespace cl
