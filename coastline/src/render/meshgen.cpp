#include "meshgen.h"

#include <cstdlib>
#include <cstring>
#include <map>

namespace cl {

int MeshBuilder::vert(V3 p, V3 n, float u, float v, Col8 c) {
    pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
    nrm.push_back(n.x); nrm.push_back(n.y); nrm.push_back(n.z);
    uv.push_back(u); uv.push_back(v);
    col.push_back(c.r); col.push_back(c.g); col.push_back(c.b); col.push_back(c.a);
    return count() - 1;
}

void MeshBuilder::quad(V3 a, V3 b, V3 c, V3 d, Col8 cl, float u0, float v0, float u1, float v1) {
    V3 n = norm(cross(b - a, c - a));
    if (len2(n) < 1e-12f) n = norm(cross(c - a, d - a));
    int i0 = vert(a, n, u0, v0, cl), i1 = vert(b, n, u1, v0, cl), i2 = vert(c, n, u1, v1, cl), i3 = vert(d, n, u0, v1, cl);
    tri(i0, i1, i2);
    tri(i0, i2, i3);
}

void MeshBuilder::triangle(V3 a, V3 b, V3 c, Col8 cl) {
    V3 n = norm(cross(b - a, c - a));
    tri(vert(a, n, 0, 0, cl), vert(b, n, 1, 0, cl), vert(c, n, 1, 1, cl));
}

void MeshBuilder::box(const Xf& x, V3 mn, V3 mx, Col8 c, bool bottom, float uvm) {
    V3 p[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
               {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
    for (auto& q : p) q = x.apply(q);
    float w = (mx.x - mn.x) * x.s, d = (mx.z - mn.z) * x.s, h = (mx.y - mn.y) * x.s;
    float ux = uvm > 0 ? w / uvm : 1, uz = uvm > 0 ? d / uvm : 1, uy = uvm > 0 ? h / uvm : 1;
    quad(p[3], p[2], p[6], p[7], c, 0, 0, ux, uy);  // +z
    quad(p[1], p[0], p[4], p[5], c, 0, 0, ux, uy);  // -z
    quad(p[2], p[1], p[5], p[6], c, 0, 0, uz, uy);  // +x
    quad(p[0], p[3], p[7], p[4], c, 0, 0, uz, uy);  // -x
    quad(p[7], p[6], p[5], p[4], shade(c, 1.0f), 0, 0, ux, uz);  // верх
    if (bottom) quad(p[0], p[1], p[2], p[3], c, 0, 0, ux, uz);
}

void MeshBuilder::cylinder(const Xf& x, V3 base, float r0, float r1, float h, int seg, Col8 c, bool capTop, bool smooth, float u) {
    (void)u;
    std::vector<V3> ring0(seg + 1), ring1(seg + 1), nr(seg + 1);
    float slope = (r0 - r1) / std::max(h, 1e-4f);
    for (int i = 0; i <= seg; i++) {
        float a = i * TAU / seg;
        float cs = std::cos(a), sn = std::sin(a);
        ring0[i] = x.apply(base + V3{cs * r0, 0, sn * r0});
        ring1[i] = x.apply(base + V3{cs * r1, h, sn * r1});
        nr[i] = x.dir(norm(V3{cs, slope, sn}));
    }
    if (smooth) {
        int start = count();
        for (int i = 0; i <= seg; i++) {
            vert(ring0[i], nr[i], (float)i / seg, 0, c);
            vert(ring1[i], nr[i], (float)i / seg, 1, c);
        }
        for (int i = 0; i < seg; i++) {
            int a = start + i * 2, b = a + 1, cc = a + 2, d = a + 3;
            tri(a, b, d);
            tri(a, d, cc);
        }
    } else {
        for (int i = 0; i < seg; i++) quad(ring0[i], ring1[i], ring1[i + 1], ring0[i + 1], c);
    }
    if (capTop && r1 > 0.001f) {
        V3 top = x.apply(base + V3{0, h, 0});
        V3 n = x.dir(V3{0, 1, 0});
        int ci = vert(top, n, 0.5f, 0.5f, c);
        int st = count();
        for (int i = 0; i <= seg; i++) vert(ring1[i], n, 0, 0, c);
        for (int i = 0; i < seg; i++) tri(ci, st + i + 1, st + i);
    }
}

void MeshBuilder::cylinderAxis(V3 a, V3 b, float r0, float r1, int seg, Col8 c, bool caps) {
    V3 ax = norm(b - a);
    V3 t = std::fabs(ax.y) < 0.9f ? norm(cross(ax, V3{0, 1, 0})) : norm(cross(ax, V3{1, 0, 0}));
    V3 bt = cross(ax, t);
    int start = count();
    for (int i = 0; i <= seg; i++) {
        float ang = i * TAU / seg;
        V3 d = t * std::cos(ang) + bt * std::sin(ang);
        vert(a + d * r0, d, 0, 0, c);
        vert(b + d * r1, d, 0, 1, c);
    }
    for (int i = 0; i < seg; i++) {
        int p0 = start + i * 2, p1 = p0 + 1, p2 = p0 + 2, p3 = p0 + 3;
        tri(p0, p2, p3);
        tri(p0, p3, p1);
    }
    if (caps) {
        for (int e = 0; e < 2; e++) {
            V3 cc = e ? b : a;
            float r = e ? r1 : r0;
            V3 n = e ? ax : -ax;
            int ci = vert(cc, n, 0, 0, c);
            int st = count();
            for (int i = 0; i <= seg; i++) {
                float ang = i * TAU / seg;
                vert(cc + (t * std::cos(ang) + bt * std::sin(ang)) * r, n, 0, 0, c);
            }
            for (int i = 0; i < seg; i++) {
                if (e) tri(ci, st + i, st + i + 1);
                else tri(ci, st + i + 1, st + i);
            }
        }
    }
}

void MeshBuilder::icosphere(const Xf& x, V3 c, V3 rad, int subdiv, Col8 cl, uint32_t seed, float jitter) {
    const float t = 1.618034f;
    std::vector<V3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (auto& p : v) p = norm(p);
    std::vector<int> f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                          3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (int s = 0; s < subdiv; s++) {
        std::map<uint64_t, int> mid;
        auto midp = [&](int a, int b) {
            uint64_t k = a < b ? ((uint64_t)a << 32) | (uint32_t)b : ((uint64_t)b << 32) | (uint32_t)a;
            auto it = mid.find(k);
            if (it != mid.end()) return it->second;
            v.push_back(norm((v[a] + v[b]) * 0.5f));
            mid[k] = (int)v.size() - 1;
            return (int)v.size() - 1;
        };
        std::vector<int> nf;
        for (size_t i = 0; i < f.size(); i += 3) {
            int a = f[i], b = f[i + 1], cc = f[i + 2];
            int ab = midp(a, b), bc = midp(b, cc), ca = midp(cc, a);
            int tris[12] = {a, ab, ca, b, bc, ab, cc, ca, bc, ab, bc, ca};
            nf.insert(nf.end(), tris, tris + 12);
        }
        f = nf;
    }
    // шум формы (для камней и крон) — детерминированный по индексу вершины
    std::vector<float> jr(v.size(), 1.0f);
    if (jitter > 0)
        for (size_t i = 0; i < v.size(); i++) jr[i] = 1.0f + (hash2f((int)i * 7 + (int)seed, (int)seed * 13 + 5) - 0.5f) * 2.0f * jitter;
    // плоское затенение: собственные вершины на грань
    for (size_t i = 0; i < f.size(); i += 3) {
        V3 p[3];
        for (int k = 0; k < 3; k++) {
            V3 q = v[f[i + k]] * jr[f[i + k]];
            p[k] = x.apply(c + mulv(q, rad));
        }
        V3 n = norm(cross(p[1] - p[0], p[2] - p[0]));
        int a = vert(p[0], n, 0, 0, cl), b = vert(p[1], n, 0, 0, cl), d = vert(p[2], n, 0, 0, cl);
        tri(a, b, d);
    }
}

void MeshBuilder::append(const MeshBuilder& o, const Xf& x) {
    int base = count();
    for (int i = 0; i < o.count(); i++) {
        V3 p{o.pos[i * 3], o.pos[i * 3 + 1], o.pos[i * 3 + 2]};
        V3 n{o.nrm[i * 3], o.nrm[i * 3 + 1], o.nrm[i * 3 + 2]};
        vert(x.apply(p), x.dir(n), o.uv[i * 2], o.uv[i * 2 + 1], {o.col[i * 4], o.col[i * 4 + 1], o.col[i * 4 + 2], o.col[i * 4 + 3]});
    }
    for (uint16_t k : o.idx) idx.push_back((uint16_t)(k + base));
}

Mesh MeshBuilder::upload() const {
    Mesh m = {};
    m.vertexCount = count();
    m.triangleCount = (int)idx.size() / 3;
    m.vertices = (float*)RL_MALLOC(pos.size() * sizeof(float));
    m.normals = (float*)RL_MALLOC(nrm.size() * sizeof(float));
    m.texcoords = (float*)RL_MALLOC(uv.size() * sizeof(float));
    m.colors = (unsigned char*)RL_MALLOC(col.size());
    m.indices = (unsigned short*)RL_MALLOC(idx.size() * sizeof(unsigned short));
    std::memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
    std::memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
    std::memcpy(m.texcoords, uv.data(), uv.size() * sizeof(float));
    std::memcpy(m.colors, col.data(), col.size());
    std::memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
    UploadMesh(&m, false);
    return m;
}

MeshBuilder& MeshSet::get(int needVerts) {
    if (parts.empty() || parts.back().count() + needVerts > 64000) parts.emplace_back();
    return parts.back();
}

std::vector<Mesh> MeshSet::upload() const {
    std::vector<Mesh> out;
    for (auto& p : parts)
        if (!p.empty()) out.push_back(p.upload());
    return out;
}

BoundingBox meshBounds(const Mesh& m) {
    BoundingBox b = {{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}};
    for (int i = 0; i < m.vertexCount; i++) {
        Vector3 p = {m.vertices[i * 3], m.vertices[i * 3 + 1], m.vertices[i * 3 + 2]};
        b.min.x = std::min(b.min.x, p.x); b.min.y = std::min(b.min.y, p.y); b.min.z = std::min(b.min.z, p.z);
        b.max.x = std::max(b.max.x, p.x); b.max.y = std::max(b.max.y, p.y); b.max.z = std::max(b.max.z, p.z);
    }
    return b;
}

}  // namespace cl
