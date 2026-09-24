#include "effects.h"

#include <algorithm>

#include "rlgl.h"

namespace cl {

static Rng fxRng(777);

static const size_t MAX_SKIDS = 3600;

void Effects::clear() {
    parts.clear();
    skids.clear();
    skidHead = 0;
    debris.clear();
    tracks_.clear();
    for (float& a : acc_) a = 0;
}

void Effects::spawn(const Particle& p) {
    size_t cap = (size_t)(2600 * std::max(quality, 0.3f));
    if (parts.size() >= cap) return;
    parts.push_back(p);
}

void Effects::carFrame(int slot, const Vehicle& v, const World& w, float dt, bool nearCam) {
    (void)w;
    if ((int)tracks_.size() < (slot + 1) * 4) tracks_.resize((slot + 1) * 4);
    float spd = v.speed;
    V3 up = v.up();
    for (int i = 0; i < 4; i++) {
        const WheelState& wh = v.w[i];
        WheelTrack& tr = tracks_[slot * 4 + i];
        int ai = std::min(slot * 4 + i, 63);
        bool soft = wh.surf == SURF_DIRT || wh.surf == SURF_SAND || wh.surf == SURF_GRAVEL || wh.surf == SURF_GRASS;
        // --- следы шин
        bool mark = wh.contact && !wh.water && ((wh.skid > 0.22f && spd > 2) || (soft && spd > 3));
        V3 cp = wh.contactP + wh.contactN * 0.035f;
        if (mark && nearCam) {
            if (tr.valid) {
                V3 d = cp - tr.last;
                float L = len(d);
                if (L > 4.0f) tr.last = cp;
                else if (L > 0.35f) {
                    V3 dir = d / L;
                    V3 side = norm(cross(wh.contactN, dir)) * (v.spec->wheelW * 0.5f);
                    SkidQuad q;
                    q.a0 = tr.last + side;
                    q.a1 = tr.last - side;
                    q.b0 = cp + side;
                    q.b1 = cp - side;
                    float a = soft ? 70 + wh.skid * 90 : wh.skid * 230;
                    q.alpha = (uint8_t)clampf(a, 0, 210);
                    q.dirt = soft;
                    if (skids.size() < MAX_SKIDS) skids.push_back(q);
                    else skids[skidHead] = q;
                    skidHead = (int)((skidHead + 1) % MAX_SKIDS);
                    tr.last = cp;
                }
            } else {
                tr.last = cp;
                tr.valid = true;
            }
        } else tr.valid = false;
        if (!nearCam || !wh.contact) continue;

        // --- частицы
        V3 base = wh.contactP + up * 0.15f;
        if (wh.water && spd > 2.0f) {
            acc_[ai] += dt * spd * 3.0f * quality;
            while (acc_[ai] > 1) {
                acc_[ai] -= 1;
                Particle p;
                p.kind = PT_SPLASH;
                p.p = base;
                p.v = v.vel * 0.4f + V3{fxRng.range(-2, 2), fxRng.range(2.5f, 5.5f) + spd * 0.12f, fxRng.range(-2, 2)};
                p.maxLife = fxRng.range(0.6f, 1.1f);
                p.size = fxRng.range(0.4f, 0.8f);
                p.grow = 1.4f;
                p.grav = 9.8f;
                p.drag = 0.4f;
                p.c = {225, 240, 245, 170};
                spawn(p);
            }
            continue;
        }
        if (!soft && wh.skid > 0.4f && spd > 2.5f) {
            acc_[ai] += dt * (wh.skid - 0.3f) * 34.0f * quality;
            while (acc_[ai] > 1) {
                acc_[ai] -= 1;
                Particle p;
                p.kind = PT_SMOKE;
                p.p = base + V3{fxRng.range(-0.2f, 0.2f), 0.1f, fxRng.range(-0.2f, 0.2f)};
                p.v = v.vel * 0.25f + V3{fxRng.range(-0.6f, 0.6f), fxRng.range(0.3f, 0.9f), fxRng.range(-0.6f, 0.6f)};
                p.maxLife = fxRng.range(1.8f, 2.8f);
                p.size = fxRng.range(0.7f, 1.1f);
                p.grow = fxRng.range(1.6f, 2.4f);
                p.drag = 1.4f;
                p.rot = fxRng.range(0, TAU);
                p.rotV = fxRng.range(-0.8f, 0.8f);
                p.c = {236, 236, 238, 120};
                spawn(p);
            }
        } else if (soft && spd > 4.0f && (!wh.front || wh.skid > 0.3f)) {
            float rate = (spd * 0.55f + wh.skid * 22.0f) * quality;
            if (wh.surf == SURF_GRASS) rate *= 0.35f;
            acc_[ai] += dt * rate;
            while (acc_[ai] > 1) {
                acc_[ai] -= 1;
                Particle p;
                p.kind = PT_DUST;
                p.p = base + V3{fxRng.range(-0.3f, 0.3f), 0.2f, fxRng.range(-0.3f, 0.3f)};
                p.v = v.vel * 0.18f + V3{fxRng.range(-1, 1), fxRng.range(0.4f, 1.4f), fxRng.range(-1, 1)};
                p.maxLife = fxRng.range(1.8f, 3.2f);
                p.size = fxRng.range(0.9f, 1.5f);
                p.grow = fxRng.range(1.8f, 2.8f);
                p.drag = 1.1f;
                p.rot = fxRng.range(0, TAU);
                p.rotV = fxRng.range(-0.5f, 0.5f);
                if (wh.surf == SURF_SAND) p.c = {226, 206, 164, 120};
                else if (wh.surf == SURF_GRASS) { p.c = {150, 150, 104, 80}; p.maxLife *= 0.6f; }
                else if (wh.surf == SURF_GRAVEL) p.c = {170, 160, 140, 120};
                else p.c = {176, 146, 108, 130};
                spawn(p);
            }
        }
    }
    // хлопки из выхлопа при сбросе газа
    if (nearCam && v.boostHint > 0.5f && !v.spec->electric) {
        V3 ex = v.toWorld({v.spec->width * 0.25f, -v.spec->cgH + 0.3f, -v.spec->length * 0.5f});
        for (int k = 0; k < 3; k++) {
            Particle p;
            p.kind = PT_FLAME;
            p.p = ex;
            p.v = v.vel - v.fwd() * fxRng.range(2, 5) + V3{fxRng.range(-0.5f, 0.5f), fxRng.range(0, 0.5f), fxRng.range(-0.5f, 0.5f)};
            p.maxLife = fxRng.range(0.06f, 0.14f);
            p.size = fxRng.range(0.25f, 0.4f);
            p.grow = 1.0f;
            p.c = {255, 150, 60, 255};
            spawn(p);
        }
    }
}

void Effects::impact(V3 pos, V3 n, float speed) {
    int count = (int)std::min(speed * 2.5f, 40.0f);
    for (int i = 0; i < count; i++) {
        Particle p;
        p.kind = PT_SPARK;
        p.p = pos;
        V3 r{fxRng.range(-1, 1), fxRng.range(0, 1), fxRng.range(-1, 1)};
        p.v = (n * 0.6f + r) * (speed * fxRng.range(0.2f, 0.45f));
        p.maxLife = fxRng.range(0.35f, 0.8f);
        p.size = fxRng.range(0.08f, 0.16f);
        p.grav = 9.8f;
        p.drag = 0.6f;
        p.c = {255, 196, 120, 255};
        spawn(p);
    }
}

void Effects::confetti(V3 pos) {
    Color cols[5] = {{255, 90, 40, 255}, {255, 210, 40, 255}, {40, 190, 255, 255}, {250, 250, 250, 255}, {180, 80, 255, 255}};
    for (int i = 0; i < 160; i++) {
        Particle p;
        p.kind = PT_CONFETTI;
        p.p = pos + V3{fxRng.range(-6, 6), fxRng.range(4, 9), fxRng.range(-6, 6)};
        p.v = V3{fxRng.range(-3, 3), fxRng.range(1, 6), fxRng.range(-3, 3)};
        p.maxLife = fxRng.range(3, 5);
        p.size = 0.14f;
        p.grav = 2.0f;
        p.drag = 1.8f;
        p.rot = fxRng.range(0, TAU);
        p.rotV = fxRng.range(-8, 8);
        p.c = cols[i % 5];
        spawn(p);
    }
}

void Effects::smash(const Prop& pr, V3 carVel) {
    auto chunk = [&](V3 size, Color c, int n, float up) {
        for (int i = 0; i < n; i++) {
            Debris d;
            d.p = pr.pos + V3{fxRng.range(-0.5f, 0.5f), fxRng.range(0.3f, 1.2f) * up, fxRng.range(-0.5f, 0.5f)};
            d.v = carVel * fxRng.range(0.5f, 0.85f) + V3{fxRng.range(-2, 2), fxRng.range(2.5f, 6.0f), fxRng.range(-2, 2)};
            d.av = V3{fxRng.range(-8, 8), fxRng.range(-8, 8), fxRng.range(-8, 8)};
            d.r = qaxis(norm(V3{fxRng.range(-1, 1), 1, fxRng.range(-1, 1)}), fxRng.range(0, TAU));
            d.size = size * fxRng.range(0.7f, 1.2f);
            d.c = c;
            debris.push_back(d);
        }
    };
    auto leaves = [&](Color c, int n, float h) {
        for (int i = 0; i < n; i++) {
            Particle p;
            p.kind = PT_LEAF;
            p.p = pr.pos + V3{fxRng.range(-1, 1), fxRng.range(0.3f, h), fxRng.range(-1, 1)};
            p.v = carVel * 0.4f + V3{fxRng.range(-3, 3), fxRng.range(1, 5), fxRng.range(-3, 3)};
            p.maxLife = fxRng.range(1.5f, 3.0f);
            p.size = fxRng.range(0.12f, 0.22f);
            p.grav = 3.0f;
            p.drag = 1.6f;
            p.rot = fxRng.range(0, TAU);
            p.rotV = fxRng.range(-6, 6);
            p.c = c;
            spawn(p);
        }
    };
    switch (pr.kind) {
        case PK_FENCE: chunk({1.4f, 0.12f, 0.06f}, {150, 116, 80, 255}, 4, 1); chunk({0.12f, 1.1f, 0.12f}, {132, 100, 70, 255}, 2, 1); break;
        case PK_BOARD: chunk({1.1f, 0.9f, 0.2f}, {255, 120, 60, 255}, 4, 2); chunk({1.0f, 0.8f, 0.2f}, {236, 50, 130, 255}, 3, 2); chunk({0.15f, 1.3f, 0.15f}, {60, 60, 66, 255}, 2, 1); break;
        case PK_CONE: chunk({0.35f, 0.4f, 0.35f}, {240, 92, 24, 255}, 2, 0.5f); break;
        case PK_HAY: chunk({0.6f, 0.5f, 0.6f}, {214, 178, 92, 255}, 4, 1); leaves({230, 200, 110, 255}, 40, 1.4f); break;
        case PK_LAMP: chunk({0.2f, 3.4f, 0.2f}, {74, 78, 84, 255}, 2, 2); chunk({0.5f, 0.25f, 0.7f}, {74, 78, 84, 255}, 1, 5); break;
        case PK_POLE: chunk({0.3f, 4.0f, 0.3f}, {108, 84, 60, 255}, 2, 2); break;
        case PK_FLAG: chunk({0.1f, 4.0f, 0.1f}, {220, 222, 226, 255}, 2, 2); leaves({255, 120, 60, 255}, 12, 6); break;
        case PK_BUSH: leaves({70, 110, 44, 255}, 45, 1.2f); chunk({0.1f, 0.6f, 0.1f}, {90, 70, 50, 255}, 3, 0.8f); break;
        case PK_PINE: case PK_OAK: case PK_BIRCH:
            leaves({62, 104, 46, 255}, 60, 4.0f * pr.scale);
            chunk({0.3f * pr.scale, 2.6f * pr.scale, 0.3f * pr.scale}, pr.kind == PK_BIRCH ? Color{226, 222, 212, 255} : Color{94, 72, 56, 255}, 2, 2);
            break;
        default: chunk({0.4f, 0.4f, 0.4f}, {150, 150, 150, 255}, 3, 1); break;
    }
    while (debris.size() > 120) debris.erase(debris.begin());
}

void Effects::update(float dt, const World& w) {
    for (size_t i = 0; i < parts.size();) {
        Particle& p = parts[i];
        p.life += dt;
        if (p.life >= p.maxLife) {
            parts[i] = parts.back();
            parts.pop_back();
            continue;
        }
        p.v = p.v * std::exp(-p.drag * dt);
        p.v.y -= p.grav * dt;
        if (p.kind == PT_SMOKE || p.kind == PT_DUST) p.v.y += 0.35f * dt;
        p.p += p.v * dt;
        p.size += p.grow * dt;
        p.rot += p.rotV * dt;
        if ((p.kind == PT_SPARK || p.kind == PT_SPLASH || p.kind == PT_LEAF || p.kind == PT_CONFETTI) && p.grav > 0) {
            float h = std::max(w.terrainH(p.p.x, p.p.z), World::WATER);
            if (p.p.y < h) {
                if (p.kind == PT_LEAF || p.kind == PT_CONFETTI) {
                    p.p.y = h + 0.02f;
                    p.v = {0, 0, 0};
                    p.grav = 0;
                    p.rotV = 0;
                } else p.life = p.maxLife;
            }
        }
        i++;
    }
    for (size_t i = 0; i < debris.size();) {
        Debris& d = debris[i];
        d.life += dt;
        if (d.life > 5.0f) {
            debris.erase(debris.begin() + i);
            continue;
        }
        d.v.y -= G * dt;
        d.p += d.v * dt;
        d.r = qintegrate(d.r, d.av, dt);
        GroundHit g = w.ground(d.p.x, d.p.z, d.p.y + 1.0f);
        float r = std::min(std::min(d.size.x, d.size.y), d.size.z) * 0.5f;
        if (d.p.y - r < g.h) {
            d.p.y = g.h + r;
            if (d.v.y < 0) d.v.y = -d.v.y * 0.3f;
            d.v.x *= 0.7f;
            d.v.z *= 0.7f;
            d.av = d.av * 0.6f;
        }
        i++;
    }
}

void Effects::drawSkids(const FrameLight& L) const {
    if (skids.empty()) return;
    (void)L;
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (const SkidQuad& q : skids) {
        if (q.dirt) rlColor4ub(62, 46, 30, q.alpha);
        else rlColor4ub(14, 14, 16, q.alpha);
        rlTexCoord2f(0, 0);
        rlVertex3f(q.a0.x, q.a0.y, q.a0.z);
        rlVertex3f(q.a1.x, q.a1.y, q.a1.z);
        rlVertex3f(q.b1.x, q.b1.y, q.b1.z);
        rlVertex3f(q.b0.x, q.b0.y, q.b0.z);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Effects::drawParticles(const Camera3D& cam, const TexSet& tex, const FrameLight& L) {
    if (parts.empty()) return;
    V3 cp{cam.position.x, cam.position.y, cam.position.z};
    V3 fwd = norm(V3{cam.target.x, cam.target.y, cam.target.z} - cp);
    V3 right = norm(cross(fwd, V3{0, 1, 0}));
    V3 up = cross(right, fwd);
    // освещённость частиц
    float sunK = clamp01(L.sunDir.y * 2.0f) * (1.0f - L.night);
    V3 tint = L.skyCol * 0.9f + L.sunCol * (0.55f * sunK);
    float mx = std::max(tint.x, std::max(tint.y, tint.z));
    if (mx > 1.0f) tint = tint / mx;
    tint = lerp(tint, V3{0.16f, 0.18f, 0.24f}, L.night * 0.85f);
    std::sort(parts.begin(), parts.end(), [&](const Particle& a, const Particle& b) { return len2(a.p - cp) > len2(b.p - cp); });

    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    auto quadAt = [&](const Particle& p, Color c) {
        float s = p.size * 0.5f;
        float cs = std::cos(p.rot), sn = std::sin(p.rot);
        V3 r = (right * cs + up * sn) * s, u = (up * cs - right * sn) * s;
        V3 a = p.p - r - u, b = p.p + r - u, cc = p.p + r + u, d = p.p - r + u;
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(0, 1); rlVertex3f(a.x, a.y, a.z);
        rlTexCoord2f(1, 1); rlVertex3f(b.x, b.y, b.z);
        rlTexCoord2f(1, 0); rlVertex3f(cc.x, cc.y, cc.z);
        rlTexCoord2f(0, 0); rlVertex3f(d.x, d.y, d.z);
    };
    // 1) полупрозрачные: дым, пыль, брызги
    BeginBlendMode(BLEND_ALPHA);
    rlSetTexture(tex.particle.id);
    rlBegin(RL_QUADS);
    for (const Particle& p : parts) {
        if (p.kind != PT_SMOKE && p.kind != PT_DUST && p.kind != PT_SPLASH) continue;
        float t = p.life / p.maxLife;
        float fade = (1.0f - t) * std::min(1.0f, p.life * 8.0f);
        Color c = p.c;
        V3 lit = p.kind == PT_SPLASH ? lerp(tint, V3{1, 1, 1}, 0.3f) : tint;
        c.r = (unsigned char)clampf(c.r * lit.x, 0, 255);
        c.g = (unsigned char)clampf(c.g * lit.y, 0, 255);
        c.b = (unsigned char)clampf(c.b * lit.z, 0, 255);
        c.a = (unsigned char)(c.a * fade);
        quadAt(p, c);
    }
    rlEnd();
    rlSetTexture(0);
    // 2) листья и конфетти — непрозрачные маленькие квадраты
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    for (const Particle& p : parts) {
        if (p.kind != PT_LEAF && p.kind != PT_CONFETTI) continue;
        float t = p.life / p.maxLife;
        Color c = p.c;
        c.r = (unsigned char)clampf(c.r * tint.x, 0, 255);
        c.g = (unsigned char)clampf(c.g * tint.y, 0, 255);
        c.b = (unsigned char)clampf(c.b * tint.z, 0, 255);
        c.a = (unsigned char)(255 * clamp01((1.0f - t) * 4.0f));
        quadAt(p, c);
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
    // 3) аддитивные: искры, пламя
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(tex.spark.id);
    rlBegin(RL_QUADS);
    for (const Particle& p : parts) {
        if (p.kind != PT_SPARK && p.kind != PT_FLAME) continue;
        float t = p.life / p.maxLife;
        Color c = p.c;
        c.a = (unsigned char)(255 * (1.0f - t));
        if (p.kind == PT_SPARK) {
            // искра вытянута вдоль скорости
            V3 d = p.v * 0.03f;
            Particle q = p;
            quadAt(q, c);
            q.p = p.p - d;
            q.size *= 0.7f;
            quadAt(q, c);
        } else quadAt(p, c);
    }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

}  // namespace cl
