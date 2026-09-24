#include "route.h"

namespace cl {

int Route::indexAt(float s) const {
    int n = (int)pts.size();
    if (n == 0) return 0;
    if (loop) { s = std::fmod(s, length); if (s < 0) s += length; }
    else s = clampf(s, 0, length);
    // точки примерно через 2 м
    int i = clampf(s / 2.0f, 0, (float)(n - 1));
    while (i > 0 && pts[i].s > s) i--;
    while (i + 1 < n && pts[i + 1].s <= s) i++;
    return i;
}

RoutePt Route::at(float s) const {
    int n = (int)pts.size();
    if (loop) { s = std::fmod(s, length); if (s < 0) s += length; }
    else s = clampf(s, 0, length);
    int i = indexAt(s);
    int j = i + 1;
    float s1;
    if (j >= n) {
        if (!loop) return pts[n - 1];
        j = 0;
        s1 = length;
    } else s1 = pts[j].s;
    float t = s1 > pts[i].s ? (s - pts[i].s) / (s1 - pts[i].s) : 0;
    RoutePt r = pts[i];
    r.p = lerp(pts[i].p, pts[j].p, t);
    r.t = norm(lerp(pts[i].t, pts[j].t, t));
    r.l = norm(lerp(pts[i].l, pts[j].l, t));
    r.s = s;
    r.curv = mixf(pts[i].curv, pts[j].curv, t);
    r.halfW = mixf(pts[i].halfW, pts[j].halfW, t);
    return r;
}

float Route::project(V3 p, int& hint, float* lateral) const {
    int n = (int)pts.size();
    if (n < 2) return 0;
    int best = hint;
    float bd = 1e18f;
    auto scan = [&](int from, int to) {
        for (int k = from; k <= to; k++) {
            int i = loop ? ((k % n) + n) % n : clampf(k, 0, n - 1);
            float d = dist2xz(pts[i].p, p);
            if (d < bd) { bd = d; best = i; }
        }
    };
    scan(hint - 40, hint + 80);
    if (bd > 60 * 60) scan(0, n - 1);  // потерялись — полный поиск
    hint = best;
    // уточнение на соседних сегментах
    float bestS = pts[best].s, bestLat = 0, bestD = 1e18f;
    for (int d = -1; d <= 0; d++) {
        int a = best + d, b = a + 1;
        if (loop) { a = (a + n) % n; b = (b + n) % n; }
        else if (a < 0 || b >= n) continue;
        V3 A = pts[a].p, B = pts[b].p;
        float dx = B.x - A.x, dz = B.z - A.z, l2 = dx * dx + dz * dz;
        float t = l2 > 0 ? clamp01(((p.x - A.x) * dx + (p.z - A.z) * dz) / l2) : 0;
        V3 C = lerp(A, B, t);
        float dd = dist2xz(C, p);
        if (dd < bestD) {
            bestD = dd;
            float sa = pts[a].s, sb = (b == 0 && loop) ? length : pts[b].s;
            bestS = mixf(sa, sb, t);
            V3 lft = norm(lerp(pts[a].l, pts[b].l, t));
            bestLat = (p.x - C.x) * lft.x + (p.z - C.z) * lft.z;
        }
    }
    if (lateral) *lateral = bestLat;
    return bestS;
}

void finalizeRoute(Route& r) {
    int n = (int)r.pts.size();
    if (n < 3) return;
    // лёгкое сглаживание стыков между дорогами
    std::vector<V3> sm(n);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            int a = i - 1, b = i + 1;
            if (r.loop) { a = (a + n) % n; b = b % n; }
            else { a = std::max(a, 0); b = std::min(b, n - 1); }
            V3 p = (r.pts[a].p + r.pts[i].p * 2.0f + r.pts[b].p) * 0.25f;
            sm[i] = p;
        }
        for (int i = 0; i < n; i++) {
            if (!r.loop && (i == 0 || i == n - 1)) continue;
            r.pts[i].p = sm[i];
        }
    }
    float s = 0;
    for (int i = 0; i < n; i++) {
        if (i > 0) s += distxz(r.pts[i].p, r.pts[i - 1].p);
        r.pts[i].s = s;
    }
    r.length = s + (r.loop ? distxz(r.pts[0].p, r.pts[n - 1].p) : 0);
    for (int i = 0; i < n; i++) {
        int a = i - 1, b = i + 1;
        if (r.loop) { a = (a + n) % n; b = b % n; }
        else { a = std::max(a, 0); b = std::min(b, n - 1); }
        V3 t = r.pts[b].p - r.pts[a].p;
        t.y = 0;
        t = norm(t);
        r.pts[i].t = t;
        r.pts[i].l = norm(cross(V3{0, 1, 0}, t));
    }
    // кривизна по изменению курса на базе ±8 м
    for (int i = 0; i < n; i++) {
        int a = i - 4, b = i + 4;
        if (r.loop) { a = (a + n) % n; b = b % n; }
        else { a = std::max(a, 0); b = std::min(b, n - 1); }
        float ha = std::atan2(r.pts[a].t.x, r.pts[a].t.z), hb = std::atan2(r.pts[b].t.x, r.pts[b].t.z);
        float ds = std::max(distxz(r.pts[a].p, r.pts[b].p), 1.0f);
        r.pts[i].curv = std::fabs(wrapAngle(hb - ha)) / ds;
    }
}

bool buildRoadRoute(const World& w, const std::vector<Leg>& legs, bool loop, Route& out, std::string* err) {
    out = Route{};
    out.loop = loop;
    for (const Leg& lg : legs) {
        int ri = w.roadIndex(lg.road);
        if (ri < 0) {
            if (err) *err = "нет дороги " + lg.road;
            return false;
        }
        const Road& r = w.roads[ri];
        int n = (int)r.pts.size();
        auto nearest = [&](float x, float z) {
            int best = 0;
            float bd = 1e18f;
            for (int i = 0; i < n; i++) {
                float d = sq(r.pts[i].p.x - x) + sq(r.pts[i].p.z - z);
                if (d < bd) { bd = d; best = i; }
            }
            return best;
        };
        int i0 = nearest(lg.x0, lg.z0), i1 = nearest(lg.x1, lg.z1);
        int dir = lg.dir;
        if (!r.loop || dir == 0) dir = i1 >= i0 ? 1 : -1;
        int i = i0;
        int guard = 0;
        bool full = r.loop && i0 == i1 && lg.dir != 0;  // полный круг по кольцевой дороге
        while (guard++ < n + 2) {
            const RoadPt& p = r.pts[i];
            RoutePt q;
            q.p = p.p;
            q.halfW = std::max(1.5f, r.halfW() - 1.2f);
            q.surf = r.surface();
            if (!out.pts.empty()) {
                // разрыв между дорогами (например, через площадь) — заполняем прямой
                V3 a = out.pts.back().p;
                float gap = distxz(a, q.p);
                if (gap > 3.0f) {
                    int k = (int)(gap / 2.0f);
                    for (int m = 1; m < k; m++) {
                        RoutePt g = q;
                        g.p = lerp(a, q.p, (float)m / k);
                        g.p.y = w.ground(g.p.x, g.p.z, g.p.y + 3).h;
                        g.halfW = std::min(out.pts.back().halfW, q.halfW);
                        out.pts.push_back(g);
                    }
                }
            }
            if (out.pts.empty() || distxz(out.pts.back().p, q.p) > 1.2f) out.pts.push_back(q);
            if (i == i1 && !(full && guard == 1)) break;
            i += dir;
            if (r.loop) i = (i + n) % n;
            else if (i < 0 || i >= n) break;
        }
    }
    if (loop && out.pts.size() > 2 && distxz(out.pts.front().p, out.pts.back().p) < 1.5f) out.pts.pop_back();
    finalizeRoute(out);
    return out.pts.size() > 10;
}

void buildTrailRoute(const World& w, const Trail& t, Route& out) {
    out = Route{};
    out.loop = false;
    for (auto& p : t.pts) {
        RoutePt q;
        q.p = p;
        q.p.y = w.ground(p.x, p.z, p.y + 2).h;
        q.halfW = 5.0f;
        GroundHit g = w.ground(p.x, p.z, p.y + 2);
        q.surf = g.road >= 0 ? w.roads[g.road].surface() : w.terrainSurface(p.x, p.z);
        out.pts.push_back(q);
    }
    finalizeRoute(out);
}

}  // namespace cl
