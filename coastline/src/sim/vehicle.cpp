#include "vehicle.h"

namespace cl {

// Сцепление и сопротивление качению по покрытиям
static const float SURF_MU[SURF_COUNT] = {1.0f, 0.74f, 0.7f, 0.62f, 0.52f, 0.82f, 0.45f, 0.93f};
static const bool SURF_LOOSE[SURF_COUNT] = {false, true, true, true, true, false, true, false};
static const float SURF_ROLL[SURF_COUNT] = {0.012f, 0.03f, 0.032f, 0.05f, 0.085f, 0.02f, 0.12f, 0.015f};

void Vehicle::init(const CarSpec* s, V3 p, float yaw) {
    spec = s;
    mass = s->mass;
    invMass = 1.0f / mass;
    float L = s->length, W = s->width, Hh = s->height;
    float Ip = mass / 12.0f * (Hh * Hh + L * L) * 0.85f;
    float Iy = mass / 12.0f * (W * W + L * L) * 0.9f;
    float Ir = mass / 12.0f * (W * W + Hh * Hh) * 0.85f;
    invI = {1.0f / Ip, 1.0f / Iy, 1.0f / Ir};
    float zF = s->wheelbase * (1.0f - s->frontWeight), zR = -s->wheelbase * s->frontWeight;
    for (int i = 0; i < 4; i++) {
        WheelState& wh = w[i];
        wh = WheelState{};
        wh.front = i < 2;
        wh.left = (i % 2) == 0;
        wh.radius = s->wheelR;
        wh.driven = s->drive == DRIVE_AWD || (s->drive == DRIVE_FWD && wh.front) || (s->drive == DRIVE_RWD && !wh.front);
        float mc = mass * (wh.front ? s->frontWeight : 1.0f - s->frontWeight) * 0.5f;
        float k = mc * sq(TAU * s->springHz);
        float c = 2.0f * s->damping * std::sqrt(k * mc);
        kS_[i] = k;
        cD_[i] = c;
        float x0 = mc * G / k;
        wh.rest = s->travel + 0.05f;
        float x = wh.left ? s->track * 0.5f : -s->track * 0.5f;
        float y = -(s->cgH - s->wheelR) + (wh.rest - x0);
        wh.local = {x, y, wh.front ? zF : zR};
        wh.comp = x0;
        wh.prevComp = x0;
    }
    color = s->color;
    teleport(p, yaw);
}

void Vehicle::teleport(V3 p, float yaw) {
    pos = p + V3{0, spec->cgH + 0.06f, 0};
    rot = qyaw(yaw);
    vel = {};
    angVel = {};
    for (auto& wh : w) {
        wh.omega = 0;
        wh.slipF = 0;
        wh.skid = 0;
    }
    steerAngle_ = 0;
    gear = 1;
    rpm = spec->idle;
    shiftT = 0;
    airTime = 0;
    tcsCut = 1;
}

float Vehicle::gearRatio(int g) const {
    if (g < 0) return spec->gearRatio[0];
    if (g == 0) return 0;
    return spec->gearRatio[std::min(g, spec->gears)];
}

V3 Vehicle::invInertiaApply(V3 t) const {
    V3 b = qrot(qconj(rot), t);
    b = mulv(b, invI);
    return qrot(rot, b);
}

void Vehicle::applyImpulse(V3 p, V3 j) {
    vel += j * invMass;
    angVel += invInertiaApply(cross(p - pos, j));
}

Vehicle::Snap Vehicle::snap() const {
    Snap s;
    s.pos = pos; s.vel = vel; s.angVel = angVel; s.rot = rot; s.rpm = rpm; s.gear = gear;
    for (int i = 0; i < 4; i++) { s.omega[i] = w[i].omega; s.comp[i] = w[i].comp; }
    return s;
}

void Vehicle::restore(const Snap& s) {
    pos = s.pos; vel = s.vel; angVel = s.angVel; rot = s.rot; rpm = s.rpm; gear = s.gear;
    for (int i = 0; i < 4; i++) { w[i].omega = s.omega[i]; w[i].comp = w[i].prevComp = s.comp[i]; w[i].slipF = 0; }
}

int Vehicle::bodySpheres(V3* out, float& r) const {
    const CarSpec& S = *spec;
    r = S.width * 0.5f;
    float yb = -S.cgH + S.wheelR * 0.6f, yt = S.height - S.cgH;
    float yc = (yb + yt) * 0.5f;
    float dz = S.length * 0.5f - r * 0.95f;
    out[0] = toWorld({0, yc, dz});
    out[1] = toWorld({0, yc, 0});
    out[2] = toWorld({0, yc, -dz});
    return 3;
}

// Контактный импульс с трением
static void contactImpulse(Vehicle& v, V3 P, V3 n, float e, float mu, float& vnOut) {
    V3 r = P - v.pos;
    V3 vP = v.pointVel(P);
    float vn = dot(vP, n);
    vnOut = vn;
    if (vn >= 0) return;
    V3 rn = cross(r, n);
    float kN = v.invMass + dot(n, cross(v.invInertiaApply(rn), r));
    float j = -(1.0f + e) * vn / kN;
    v.applyImpulse(P, n * j);
    vP = v.pointVel(P);
    V3 vt = vP - n * dot(vP, n);
    float vtl = len(vt);
    if (vtl > 1e-4f) {
        V3 td = vt / vtl;
        V3 rt = cross(r, td);
        float kT = v.invMass + dot(td, cross(v.invInertiaApply(rt), r));
        float jt = std::min(vtl / kT, mu * j);
        v.applyImpulse(P, td * -jt);
    }
}

void Vehicle::drivetrain(float dt, float* driveT) {
    const CarSpec& S = *spec;
    float split[4] = {0, 0, 0, 0};
    if (S.drive == DRIVE_FWD) split[0] = split[1] = 0.5f;
    else if (S.drive == DRIVE_RWD) split[2] = split[3] = 0.5f;
    else { split[0] = split[1] = (1.0f - S.awdRear) * 0.5f; split[2] = split[3] = S.awdRear * 0.5f; }
    float wAvg = 0;
    for (int i = 0; i < 4; i++) wAvg += split[i] * w[i].omega;

    // автоматическая коробка и задний ход
    bool reverse = gear < 0;
    float thrIn = reverse && as.autoGear ? in.brake : in.throttle;
    if (as.autoGear) {
        if (gear >= 1) {
            if (!S.electric && gear < S.gears && rpm > S.redline * 0.935f && shiftT <= 0 && wheelsOnGround >= 2 && thrIn > 0.2f && clutch >= 0.99f) {
                gear++;
                shiftT = 0.16f;
            } else if (!S.electric && gear > 1 && shiftT <= 0) {
                float rpmDown = rpm * gearRatio(gear - 1) / gearRatio(gear);
                if (rpmDown < S.redline * 0.8f && rpm < S.redline * (thrIn > 0.6f ? 0.6f : 0.45f)) {
                    gear--;
                    shiftT = 0.1f;
                }
            }
            if (fwdSpeed < 0.9f && in.brake > 0.5f && in.throttle < 0.1f) {
                revTimer_ += dt;
                if (revTimer_ > 0.25f) { gear = -1; revTimer_ = 0; }
            } else revTimer_ = 0;
        } else {
            if (fwdSpeed > 2.0f) gear = 1;  // едем вперёд на задней — например, после перемотки
            if (fwdSpeed > -0.9f && in.throttle > 0.5f && in.brake < 0.1f) {
                revTimer_ += dt;
                if (revTimer_ > 0.1f) { gear = 1; revTimer_ = 0; }
            } else revTimer_ = 0;
        }
    } else {
        if (in.shiftUp && gear < S.gears) { gear = gear < 0 ? 1 : gear + 1; shiftT = 0.14f; }
        if (in.shiftDown && gear > -1) { gear = gear == 1 ? -1 : gear - 1; shiftT = 0.12f; }
        in.shiftUp = in.shiftDown = false;
    }

    float ratio = gearRatio(gear) * S.finalDrive;
    float rpmW = std::fabs(wAvg * ratio) * 60.0f / TAU;
    float thr = thrIn * tcsCut * powerScale;
    if (shiftT > 0) { shiftT -= dt; thr *= 0.1f; }
    float launch = std::min(S.redline * 0.55f, S.torqueRpm * 1.15f);
    if (gear == 0) {
        rpm = damp(rpm, S.idle + thrIn * (S.redline - S.idle), 7, dt);
        clutch = 0;
    } else {
        float slipRpm = S.idle + (launch - S.idle) * thrIn;
        if (!S.electric && rpmW < slipRpm) {
            rpm = damp(rpm, slipRpm, 9, dt);
            clutch = clamp01(rpmW / slipRpm);
        } else {
            rpm = damp(rpm, rpmW, 30, dt);
            clutch = 1;
        }
        if (wheelsOnGround == 0) rpm = damp(rpm, S.idle + thrIn * (S.redline * 0.95f - S.idle), 3, dt);
    }
    rpm = clampf(rpm, S.idle * 0.9f, S.redline * 1.04f);
    if (rpm >= S.redline && !S.electric) thr = 0;  // отсечка
    float Te = S.torqueAt(std::max(rpm, S.idle)) * thr;
    if (thrIn < 0.05f) Te -= S.torqueNm * 0.15f * clamp01((rpm - S.idle) / (S.redline - S.idle));
    engineLoad = thr;
    float Tw = gear == 0 ? 0.0f : Te * ratio * 0.88f;
    for (int i = 0; i < 4; i++) {
        driveT[i] = Tw * split[i];
        // приведённая инерция: маховик двигателя через передачу (на низких передачах — десятки кг·м²)
        inertia_[i] = 1.0f + (split[i] > 0 ? 0.4f + 0.22f * ratio * ratio * split[i] * clutch : 0.0f);
    }
}

void Vehicle::step(float dt, const World& world) {
    const CarSpec& S = *spec;
    V3 f = fwd(), u = up(), l = left();
    fwdSpeed = dot(vel, f);
    latSpeed = dot(vel, l);
    speed = len(vel);

    // --- руль
    float spd = std::fabs(fwdSpeed);
    float maxSt = S.maxSteerDeg * DEG * mixf(1.0f, 0.28f, clamp01((spd - 5.0f) / 45.0f));
    float target = in.steer * maxSt;
    if (as.steerAssist && spd > 4 && wheelsOnGround >= 2 && fwdSpeed > 0) {
        float slip = std::atan2(latSpeed, spd);
        target += clampf(slip, -0.6f, 0.6f) * (in.handbrake ? 0.15f : 0.55f);
    }
    steerAngle_ = moveTowards(steerAngle_, clampf(target, -S.maxSteerDeg * DEG, S.maxSteerDeg * DEG), 3.4f * dt);
    for (int i = 0; i < 2; i++) w[i].steer = steerAngle_;

    // --- трансмиссия
    float driveT[4];
    drivetrain(dt, driveT);
    bool reverse = gear < 0;
    float brakeIn = (reverse && as.autoGear) ? in.throttle : in.brake;
    if (gear >= 1 && fwdSpeed < -0.5f && in.throttle > 0.1f) brakeIn = std::max(brakeIn, in.throttle);  // катимся назад на передаче
    brakeLights = std::max(brakeIn, in.handbrake ? 0.6f : 0.0f);

    // --- подвеска
    float Lmax[4], tt[4];
    GroundHit gh[4];
    V3 A[4];
    for (int i = 0; i < 4; i++) {
        WheelState& wh = w[i];
        A[i] = toWorld(wh.local);
        Lmax[i] = wh.rest + wh.radius;
        wh.contact = false;
        wh.water = false;
        GroundHit g = world.ground(A[i].x, A[i].z, A[i].y);
        float den = dot(u, g.n);
        if (den > 0.3f) {
            float t0 = ((A[i].y - g.h) * g.n.y) / den;
            V3 Hp = A[i] - u * t0;
            GroundHit g2 = world.ground(Hp.x, Hp.z, Hp.y + 0.5f);
            float den2 = dot(u, g2.n);
            if (den2 > 0.3f) {
                t0 = dot(A[i] - V3{Hp.x, g2.h, Hp.z}, g2.n) / den2;
                g = g2;
            }
            if (t0 < Lmax[i] && t0 > -0.8f) {
                wh.contact = true;
                tt[i] = t0;
                gh[i] = g;
            }
        }
        wh.prevComp = wh.comp;
        wh.comp = wh.contact ? clampf(Lmax[i] - tt[i], 0.0f, S.travel + 0.12f) : 0.0f;
    }

    V3 F{0, -G * mass, 0}, T{0, 0, 0};
    int onGround = 0;
    float maxSlipDriven = 0;
    float squealAcc = 0;
    surfUnder = SURF_GRASS;
    onRoad = false;
    roadId = -1;
    for (int i = 0; i < 4; i++) {
        WheelState& wh = w[i];
        float Iw = inertia_[i];
        float R = wh.radius;
        if (!wh.contact) {
            // колесо в воздухе: крутится от двигателя и тормозов
            wh.omega += driveT[i] / Iw * dt * 0.3f;
            if (brakeIn > 0.05f || (in.handbrake && !wh.front)) wh.omega = damp(wh.omega, 0, 8, dt);
            wh.omega *= std::exp(-0.3f * dt);
            wh.load = 0;
            wh.fx = wh.fy = 0;
            wh.skid = 0;
            wh.spin += wh.omega * dt;
            continue;
        }
        onGround++;
        const GroundHit& g = gh[i];
        // пружина + демпфер + стабилизатор
        int mate = i ^ 1;
        float arbF = S.arb * kS_[i] * (wh.comp - w[mate].comp);
        float cv = clampf((wh.comp - wh.prevComp) / dt, -6.0f, 6.0f);
        float spring = kS_[i] * wh.comp;
        if (wh.comp > S.travel) spring += kS_[i] * 14.0f * (wh.comp - S.travel);
        float Fs = std::max(0.0f, spring + cD_[i] * cv + arbF);
        wh.load = Fs;
        F += u * Fs;
        T += cross(A[i] - pos, u * Fs);

        // шина
        V3 C = A[i] - u * tt[i];
        wh.contactP = C;
        wh.contactN = g.n;
        wh.surf = g.surf;
        if (g.road >= 0) { onRoad = true; roadId = g.road; }
        if (i == 0) surfUnder = g.surf;
        V3 vC = pointVel(C);
        float cs = std::cos(wh.steer), sn = std::sin(wh.steer);
        V3 wf = f * cs + l * sn;
        V3 wfg = norm(projPlane(wf, g.n));
        V3 wlg = cross(g.n, wfg);
        float vx = dot(vC, wfg), vy = dot(vC, wlg);
        Surface sf = g.surf;
        bool loose = SURF_LOOSE[sf];
        float mu = S.grip * SURF_MU[sf] * (loose ? S.looseGrip : 1.0f) * gripScale;
        float Fz = Fs;
        float Fz0 = mass * G * 0.25f;
        mu *= clampf(1.08f - 0.08f * Fz / Fz0, 0.8f, 1.1f);
        if (g.water) {
            wh.water = true;
            mu *= g.waterDepth > 0.35f ? 0.55f : 0.85f;
        }
        float kPeak = loose ? 0.2f : 0.11f, aPeak = loose ? 0.19f : 0.13f;

        // привод
        float wD = wh.omega + driveT[i] / Iw * dt;
        // тормоз (с ABS на скорости)
        float Tb = brakeIn * S.brakeTorque * 2.0f * (wh.front ? S.brakeBias : 1.0f - S.brakeBias);
        bool hb = in.handbrake && !wh.front;
        if (hb) Tb = std::max(Tb, mass * 3.2f);
        if (as.abs && !hb && Tb > 0 && std::fabs(vx) > 3.0f) {
            float kPred = (wD * R - vx) / std::max(std::fabs(vx), 2.5f);
            if (kPred * signf(vx) < -kPeak * 1.15f) Tb *= 0.25f;
        }
        Tb += SURF_ROLL[sf] * Fz * R;  // сопротивление качению — как слабый тормоз
        float dWb = Tb / Iw * dt;
        float wB = std::fabs(wD) <= dWb ? 0.0f : wD - signf(wD) * dWb;
        bool locked = wB == 0.0f && Tb > 60.0f;

        // проскальзывание и силы шины
        float kappa = (wB * R - vx) / std::max(std::fabs(vx), 2.5f);
        float alpha = std::atan2(vy, std::max(std::fabs(vx), 1.0f));
        wh.slipF += (alpha - wh.slipF) * clamp01((std::fabs(vx) + 2.0f) * dt / 0.3f);
        alpha = wh.slipF;
        wh.slipRatio = kappa;
        wh.slipAngle = alpha;
        float sx = kappa / kPeak, sy = alpha / aPeak;
        float rho = std::sqrt(sx * sx + sy * sy);
        float Fmax = mu * Fz;
        float fmag = Fmax * std::sin(1.35f * std::atan(2.34f * rho));
        float Fx = rho > 1e-5f ? fmag * sx / rho : 0.0f;
        float Fy = rho > 1e-5f ? -fmag * sy / rho : 0.0f;
        float omegaRoll = vx / R;
        if (locked && std::fabs(Fx * R) <= Tb + std::fabs(driveT[i])) {
            // тормоз удерживает колесо: шина скользит с полной силой
            wh.omega = 0;
            if (std::fabs(vx) < 0.4f) {
                // почти на месте — статическое трение держит машину на уклоне
                float Fs2 = clampf(-vx * mass * 0.25f / dt * 0.5f, -Fmax, Fmax);
                Fx = mixf(Fx, Fs2, 1.0f - std::fabs(vx) / 0.4f);
            }
        } else {
            // реакция шины; неявная защита от «перекрута» через свободное качение
            float wT = wB - Fx * R / Iw * dt;
            if ((wB - omegaRoll) * (wT - omegaRoll) < 0) {
                Fx = (wB - omegaRoll) * Iw / (R * dt);
                wT = omegaRoll;
            }
            wh.omega = wT;
        }
        // малая скорость: гасим боковое сползание напрямую
        if (std::fabs(vx) < 2.5f) {
            float Fyl = clampf(-vy * mass * 0.25f / dt * 0.35f, -Fmax, Fmax);
            Fy = mixf(Fy, Fyl, 1.0f - std::fabs(vx) / 2.5f);
        }
        wh.fx = Fx;
        wh.fy = Fy;
        wh.dbgDrive = driveT[i];
        wh.dbgRoll = omegaRoll;
        wh.dbgIw = Iw;
        V3 Ft = wfg * Fx + wlg * Fy;
        F += Ft;
        T += cross(C - pos, Ft);
        wh.skid = rho > 1.05f ? clamp01((rho - 1.05f) * 0.8f) * clamp01(std::max(std::fabs(vx), std::fabs(vy)) / 4.0f) : 0.0f;
        if (!loose) squealAcc = std::max(squealAcc, wh.skid);
        if (wh.driven) maxSlipDriven = std::max(maxSlipDriven, kappa * signf(vx + 0.01f) / kPeak);
        wh.spin += wh.omega * dt;
    }
    // блокировка дифференциала: выравниваем скорости ведущих колёс на оси
    for (int a = 0; a < 2; a++) {
        WheelState& L = w[a * 2];
        WheelState& Rr = w[a * 2 + 1];
        if (L.driven && Rr.driven) {
            float m = (L.omega + Rr.omega) * 0.5f;
            L.omega = mixf(L.omega, m, 0.25f);
            Rr.omega = mixf(Rr.omega, m, 0.25f);
        }
    }
    wheelsOnGround = onGround;
    squeal = squealAcc;
    // трекшн-контроль
    if (as.tcs && brakeIn < 0.1f && maxSlipDriven > 1.15f) tcsCut = moveTowards(tcsCut, clampf(1.0f - (maxSlipDriven - 1.15f) * 0.9f, 0.12f, 1.0f), 14.0f * dt);
    else tcsCut = moveTowards(tcsCut, 1.0f, 3.0f * dt);

    // --- аэродинамика
    float area = S.width * S.height * 0.84f;
    F += vel * (-0.5f * 1.225f * S.cd * area * speed);
    float down = 0.5f * 1.225f * S.clA * fwdSpeed * fwdSpeed;
    V3 downP = toWorld({0, 0, -0.05f * S.wheelbase});
    F -= u * down;
    T += cross(downP - pos, u * -down);

    // --- помощник устойчивости: гасит сильный занос (не мешает ручнику)
    yawRate = dot(angVel, u);
    if (as.stm && !in.handbrake && onGround >= 3 && spd > 8) {
        float expected = fwdSpeed * std::tan(steerAngle_) / S.wheelbase;
        float err = yawRate - expected;
        if (std::fabs(err) > 0.35f) {
            float corr = -(err - signf(err) * 0.35f) * 2.2f / invI.y;
            T += u * corr;
        }
    }

    // --- вода: сопротивление и плавучесть
    float bottom = pos.y - S.cgH;
    waterDepth = std::max(0.0f, World::WATER - bottom);
    if (waterDepth > 0.05f) {
        float k = clamp01(waterDepth / 1.2f);
        F += vel * (-mass * 0.9f * k);
        F.y += mass * G * 0.35f * k;
        angVel *= std::exp(-1.5f * k * dt);
    }

    // --- интегрирование скоростей
    vel += F * (invMass * dt);
    angVel += invInertiaApply(T) * dt;
    if (onGround == 0) {
        airTime += dt;
        // в воздухе гасим тангаж и крен, чтобы приземления были ровнее
        V3 wb = qrot(qconj(rot), angVel);
        wb.x *= std::exp(-1.4f * dt);
        wb.z *= std::exp(-1.4f * dt);
        angVel = qrot(rot, wb);
    } else {
        airTime = 0;
    }
    angVel *= std::exp(-0.05f * dt);

    collideGround(dt, world);
    collideStatic(dt, world);

    // --- интегрирование положения
    pos += vel * dt;
    rot = qintegrate(rot, angVel, dt);

    // граница мира
    for (float* c : {&pos.x, &pos.z}) {
        float* vc = (c == &pos.x) ? &vel.x : &vel.z;
        if (std::fabs(*c) > World::LIMIT) {
            float sgn = signf(*c);
            *c = sgn * World::LIMIT;
            if (*vc * sgn > 0) *vc *= -0.3f;
        }
    }
    if (!finite(pos) || !finite(vel) || !finite(angVel)) {
        vel = {}; angVel = {}; rot = qyaw(0);
        pos = {0, world.terrainH(0, 40) + 2, 40};
    }

    // --- производные величины
    f = fwd();
    u = up();
    l = left();
    fwdSpeed = dot(vel, f);
    latSpeed = dot(vel, l);
    speed = len(vel);
    upsideDown = u.y < 0.15f;
    driftAngle = (speed > 4 && fwdSpeed > 0) ? std::atan2(dot(vel, l), dot(vel, f)) : 0.0f;
    float ws = 0; int dn = 0;
    for (auto& wh : w) if (wh.driven) { ws += wh.omega * wh.radius; dn++; }
    wheelSpeedMs = dn ? ws / dn : 0;
}

void Vehicle::collideGround(float, const World& world) {
    const CarSpec& S = *spec;
    float hw = S.width * 0.46f, hl = S.length * 0.47f;
    float yb = -S.cgH + S.wheelR * 0.55f;
    float yt = S.height - S.cgH;
    V3 pts[11] = {{hw, yb, hl}, {-hw, yb, hl}, {hw, yb, -hl}, {-hw, yb, -hl},
                  {hw * 0.8f, yt, hl * 0.45f}, {-hw * 0.8f, yt, hl * 0.45f}, {hw * 0.8f, yt, -hl * 0.45f}, {-hw * 0.8f, yt, -hl * 0.45f},
                  {0, yt, 0}, {hw, (yb + yt) * 0.5f, 0}, {-hw, (yb + yt) * 0.5f, 0}};
    float maxPen = 0;
    V3 penN{0, 1, 0};
    for (int k = 0; k < 11; k++) {
        V3 P = toWorld(pts[k]);
        float th = world.terrainH(P.x, P.z);
        if (P.y > th + 3.2f) {
            // быстрый выход: высоко над рельефом и не над мостом или трамплином
            RoadHit rh;
            bool nearRamp = false;
            for (auto& rp : world.ramps) if (dist2xz(rp.c, P) < 16 * 16) nearRamp = true;
            if (!nearRamp && !world.roadAt(P.x, P.z, rh)) continue;
        }
        GroundHit g = world.ground(P.x, P.z, P.y + 0.4f);
        if (P.y >= g.h) continue;
        float pen = (g.h - P.y) * g.n.y;
        float vn;
        contactImpulse(*this, P, g.n, k < 4 ? 0.05f : 0.12f, k < 4 ? 0.35f : 0.5f, vn);
        if (-vn > 5.0f) impacts.push_back({-vn, P, false});
        if (pen > maxPen) { maxPen = pen; penN = g.n; }
    }
    if (maxPen > 0) pos += penN * (maxPen * 0.6f);
}

void Vehicle::collideStatic(float, const World& world) {
    V3 sp[3];
    float r;
    int n = bodySpheres(sp, r);
    for (int s = 0; s < n; s++) {
        V3 c = sp[s];
        world.queryCols(c.x, c.z, r + 3.0f, colScratch_);
        for (int id : colScratch_) {
            const Collider& k = world.cols[id];
            if (k.prop >= 0 && !world.props[k.prop].alive) continue;
            if (c.y + r < k.y0 || c.y - r > k.y1) continue;
            V3 nrm;
            float pen;
            if (k.shape == COL_CYL) {
                float d = distxz(c, k.c);
                pen = r + k.r - d;
                if (pen <= 0) continue;
                nrm = d > 1e-4f ? V3{(c.x - k.c.x) / d, 0, (c.z - k.c.z) / d} : -fwd();
            } else if (k.shape == COL_BOX) {
                float dx = c.x - k.c.x, dz = c.z - k.c.z;
                float lx = dx * k.cs - dz * k.sn, lz = dx * k.sn + dz * k.cs;
                float qx = clampf(lx, -k.hx, k.hx), qz = clampf(lz, -k.hz, k.hz);
                float ex = lx - qx, ez = lz - qz, d2 = ex * ex + ez * ez;
                if (d2 > r * r) continue;
                float nlx, nlz;
                if (d2 > 1e-8f) {
                    float d = std::sqrt(d2);
                    nlx = ex / d; nlz = ez / d;
                    pen = r - d;
                } else {
                    float px = k.hx - std::fabs(lx), pz = k.hz - std::fabs(lz);
                    if (px < pz) { nlx = signf(lx) + (lx == 0); nlz = 0; pen = r + px; }
                    else { nlx = 0; nlz = signf(lz) + (lz == 0); pen = r + pz; }
                }
                nrm = {nlx * k.cs + nlz * k.sn, 0, -nlx * k.sn + nlz * k.cs};
            } else {
                V3 dv = c - k.c;
                float d = len(dv);
                pen = r + k.r - d;
                if (pen <= 0) continue;
                nrm = d > 1e-4f ? dv / d : V3{0, 1, 0};
            }
            if (k.smash) {
                bool dup = false;
                for (auto& e : smashes) if (e.prop == k.prop) dup = true;
                if (!dup) {
                    smashes.push_back({k.prop, speed, c - nrm * r});
                    vel *= 0.975f;
                }
                continue;
            }
            V3 P = c - nrm * r;
            float vn;
            contactImpulse(*this, P, nrm, 0.2f, 0.25f, vn);
            pos += nrm * (pen * 0.85f);
            if (-vn > 3.0f) impacts.push_back({-vn, P, false});
        }
    }
}

bool collideCars(Vehicle& a, Vehicle& b) {
    if (distxz(a.pos, b.pos) > (a.spec->length + b.spec->length) * 0.6f + 1.0f) return false;
    if (std::fabs(a.pos.y - b.pos.y) > 4.0f) return false;
    V3 sa[3], sb[3];
    float ra, rb;
    int na = a.bodySpheres(sa, ra), nb = b.bodySpheres(sb, rb);
    bool hit = false;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++) {
            V3 d = sa[i] - sb[j];
            float dl = len(d);
            float pen = ra + rb - dl;
            if (pen <= 0 || dl < 1e-4f) continue;
            V3 n = d / dl;
            n.y *= 0.3f;
            n = norm(n);
            V3 P = sb[j] + n * rb;
            V3 vrel = a.pointVel(P) - b.pointVel(P);
            float vn = dot(vrel, n);
            if (vn < 0) {
                V3 r1 = P - a.pos, r2 = P - b.pos;
                float kN = a.invMass + b.invMass + dot(n, cross(a.invInertiaApply(cross(r1, n)), r1)) + dot(n, cross(b.invInertiaApply(cross(r2, n)), r2));
                float jn = -(1.0f + 0.25f) * vn / kN;
                a.applyImpulse(P, n * jn);
                b.applyImpulse(P, n * -jn);
                // трение скольжения кузовов
                V3 vr2 = a.pointVel(P) - b.pointVel(P);
                V3 vt = vr2 - n * dot(vr2, n);
                float vtl = len(vt);
                if (vtl > 1e-3f) {
                    float jt = std::min(vtl / (a.invMass + b.invMass) * 0.5f, 0.2f * jn);
                    V3 td = vt / vtl;
                    a.applyImpulse(P, td * -jt);
                    b.applyImpulse(P, td * jt);
                }
                if (-vn > 2.5f) {
                    a.impacts.push_back({-vn, P, true});
                    b.impacts.push_back({-vn, P, true});
                }
                hit = true;
            }
            float wa = b.mass / (a.mass + b.mass);
            a.pos += n * (pen * wa * 0.8f);
            b.pos -= n * (pen * (1 - wa) * 0.8f);
        }
    return hit;
}

}  // namespace cl
