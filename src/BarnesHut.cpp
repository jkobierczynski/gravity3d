#include "BarnesHut.h"
#include "Parallel.h"
#include <cmath>
#include <array>
#include <functional>

namespace {

using dv3 = glm::dvec3;

struct BuildNode {
    dv3   center;
    double half;
    std::array<int, 8> child;
    int   first, count;
    bool  leaf;
    dv3   com;
    double mass;
};

struct Tree {
    std::vector<BuildNode> nodes;
    std::vector<int>       order;
    const std::vector<dv3>*    pos;
    const std::vector<double>* mass;
    int ncrit, maxDepth;

    int build(int first, int count, const dv3& c, double half, int depth) {
        int bi = (int)nodes.size();
        BuildNode nd; nd.center = c; nd.half = half; nd.first = first; nd.count = count;
        nd.leaf = true; nd.child.fill(-1); nd.com = dv3(0.0); nd.mass = 0.0;
        nodes.push_back(nd);
        if (count <= ncrit || depth >= maxDepth) return bi;

        auto oct = [&](const dv3& q) {
            int o = 0; if (q.x > c.x) o |= 1; if (q.y > c.y) o |= 2; if (q.z > c.z) o |= 4; return o;
        };
        int cnt[8] = {0};
        for (int k = first; k < first + count; ++k) cnt[oct((*pos)[order[k]])]++;
        int start[8]; int s = first; for (int o = 0; o < 8; ++o) { start[o] = s; s += cnt[o]; }
        std::vector<int> tmp(count); int put[8];
        for (int o = 0; o < 8; ++o) put[o] = start[o] - first;
        for (int k = first; k < first + count; ++k) {
            int o = oct((*pos)[order[k]]); tmp[put[o]++] = order[k];
        }
        for (int k = 0; k < count; ++k) order[first + k] = tmp[k];

        double h = half * 0.5;
        for (int o = 0; o < 8; ++o) {
            if (cnt[o] == 0) continue;
            double sx = (o & 1) ? 1 : -1, sy = (o & 2) ? 1 : -1, sz = (o & 4) ? 1 : -1;
            dv3 cc(c.x + sx * h, c.y + sy * h, c.z + sz * h);
            nodes[bi].child[o] = build(start[o], cnt[o], cc, h, depth + 1);
        }
        nodes[bi].leaf = false;
        return bi;
    }

    void computeCOM() {
        for (int bi = (int)nodes.size() - 1; bi >= 0; --bi) {
            BuildNode& nd = nodes[bi];
            dv3 c(0.0); double m = 0.0;
            if (nd.leaf) {
                for (int k = nd.first; k < nd.first + nd.count; ++k) {
                    int j = order[k]; c += (*pos)[j] * (*mass)[j]; m += (*mass)[j];
                }
            } else {
                for (int o = 0; o < 8; ++o) {
                    int ci = nd.child[o]; if (ci < 0) continue;
                    c += nodes[ci].com * nodes[ci].mass; m += nodes[ci].mass;
                }
            }
            if (m > 0) c /= m;
            nd.com = c; nd.mass = m;
        }
    }

    bh::Flat flatten() {
        bh::Flat f;
        int nn = (int)nodes.size();
        f.com.resize(nn); f.mass.resize(nn); f.size.resize(nn);
        f.next.resize(nn); f.child.resize(nn); f.first.resize(nn); f.count.resize(nn);
        f.order.assign(order.begin(), order.end());
        int counter = 0;
        std::function<void(int)> rec = [&](int bi) {
            BuildNode& nd = nodes[bi];
            int idx = counter++;
            f.com[idx] = nd.com; f.mass[idx] = nd.mass; f.size[idx] = nd.half * 2;
            f.first[idx] = nd.first; f.count[idx] = nd.count;
            if (nd.leaf) f.child[idx] = -1;
            else {
                f.child[idx] = counter;                 // first child emitted next (DFS)
                for (int o = 0; o < 8; ++o) if (nd.child[o] >= 0) rec(nd.child[o]);
            }
            f.next[idx] = counter;                      // continue here when not opened
        };
        rec(0);
        f.n = counter;
        return f;
    }
};

} // namespace

namespace bh {

Flat buildFlat(const std::vector<dv3>& pos, const std::vector<double>& mass, int ncrit) {
    Flat empty;
    int n = (int)pos.size();
    if (n == 0) return empty;
    dv3 mn = pos[0], mx = pos[0];
    for (const auto& q : pos) { mn = glm::min(mn, q); mx = glm::max(mx, q); }
    dv3 c = (mn + mx) * 0.5; double half = 0;
    for (int a = 0; a < 3; ++a) half = std::max(half, std::max(mx[a] - c[a], c[a] - mn[a]));
    half *= 1.0001; if (half <= 0) half = 1;

    Tree t; t.pos = &pos; t.mass = &mass; t.ncrit = ncrit; t.maxDepth = 24;
    t.order.resize(n); for (int i = 0; i < n; ++i) t.order[i] = i;
    t.build(0, n, c, half, 0);
    t.computeCOM();
    return t.flatten();
}

void accelerations(const std::vector<dv3>& pos, const std::vector<double>& mass,
                   double G, double softening, double theta, std::vector<dv3>& out) {
    int n = (int)pos.size();
    out.assign(n, dv3(0.0));
    if (n == 0) return;
    Flat f = buildFlat(pos, mass, 32);
    const double eps2 = softening * softening, theta2 = theta * theta;

    parallel::forRange((std::size_t)n, [&](std::size_t lo, std::size_t hi) {
        for (std::size_t ii = lo; ii < hi; ++ii) {
            int i = (int)ii;
            const dv3 pi = pos[i];
            dv3 a(0.0);
            int idx = 0;
            while (idx < f.n) {
                dv3 d = f.com[idx] - pi;
                double d2 = glm::dot(d, d);
                if (f.size[idx] * f.size[idx] < theta2 * d2) {          // far cell -> point mass
                    double inv = 1.0 / std::sqrt(d2 + eps2);
                    a += (f.mass[idx] * inv * inv * inv) * d;
                    idx = f.next[idx];
                } else if (f.child[idx] < 0) {                          // near leaf -> exact
                    int first = f.first[idx], cnt = f.count[idx];
                    for (int k = 0; k < cnt; ++k) {
                        int j = f.order[first + k];
                        if (j == i) continue;
                        dv3 dd = pos[j] - pi;
                        double r2 = glm::dot(dd, dd) + eps2;
                        double inv = 1.0 / std::sqrt(r2);
                        a += (mass[j] * inv * inv * inv) * dd;
                    }
                    idx = f.next[idx];
                } else {
                    idx = f.child[idx];                                 // open the cell
                }
            }
            out[i] = G * a;
        }
    });
}

} // namespace bh
