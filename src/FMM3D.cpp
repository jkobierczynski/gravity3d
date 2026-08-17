#include "FMM3D.h"
#include "Parallel.h"
#include <complex>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <algorithm>

// ============================================================================
//  3D Laplace Fast Multipole Method.
//
//  Convention (Greengard-Rokhlin): spherical harmonics without the
//  Condon-Shortley phase,
//      Y_n^m(θ,φ) = sqrt((n-|m|)!/(n+|m|)!) · P_n^{|m|}(cosθ) · e^{imφ},
//      Y_n^{-m}   = conj(Y_n^m),
//      A(n,m)     = (-1)^n / sqrt((n-m)!(n+m)!).
//  Multipole:  M_n^m += q·r^n·conj(Y_n^m(d)),   Φ = Σ M_n^m/r^{n+1}·Y_n^m.
//  Local:      Φ = Σ L_n^m·r^n·Y_n^m.
//  The M2M/M2L/L2L translation operators and their sign/phase factors were
//  pinned down by numerical validation against direct summation.
// ============================================================================

namespace {

using cd  = std::complex<double>;
using dv3 = glm::dvec3;

// ---- shared tables (single-threaded use) ----
std::vector<double> g_fact;
std::vector<double> g_A;      // A(n,m) indexed by idx(n,m)
int    g_factDeg = -1;
int    g_Adeg    = -1;
cd     g_ip[4]   = { cd(1,0), cd(0,1), cd(-1,0), cd(0,-1) };

inline int   idx(int n,int m){ return n*n + n + m; }
inline double fac(int k){ return (k < 0) ? 0.0 : g_fact[k]; }
inline cd    IPOW(int e){ int r = e & 3; if (r < 0) r += 4; return g_ip[r]; }
inline double Atab(int n,int m){ if (m < -n || m > n) return 0.0; return g_A[idx(n,m)]; }

void ensureTables(int order){
    int needFact = 4*order + 8;
    if (g_factDeg < needFact){
        g_fact.assign(needFact+1, 1.0);
        for (int i = 1; i <= needFact; ++i) g_fact[i] = g_fact[i-1]*i;
        g_factDeg = needFact;
    }
    int needA = 2*order + 2;
    if (g_Adeg < needA){
        g_A.assign((needA+1)*(needA+1), 0.0);
        for (int n = 0; n <= needA; ++n)
            for (int m = -n; m <= n; ++m){
                double s = (n & 1) ? -1.0 : 1.0;
                g_A[idx(n,m)] = s / std::sqrt(fac(n-m)*fac(n+m));
            }
        g_Adeg = needA;
    }
}

inline void sph(const dv3& v, double& r, double& th, double& ph){
    r = glm::length(v);
    if (r < 1e-300){ th = 0; ph = 0; return; }
    th = std::acos(std::max(-1.0, std::min(1.0, v.z / r)));
    ph = std::atan2(v.y, v.x);
}
inline void powfill(double r, int D, std::vector<double>& rp){
    rp.resize(D+1); rp[0] = 1.0;
    for (int i = 1; i <= D; ++i) rp[i] = rp[i-1]*r;
}

// One-pass table of Y_n^m for all n<=D, m in [-n,n].
void buildY(double th, double ph, int D, std::vector<cd>& Y){
    Y.assign((D+1)*(D+1), cd(0,0));
    double x = std::cos(th);
    double somx2 = std::sqrt(std::max(0.0, (1.0 - x)*(1.0 + x)));
    std::vector<double> Pcs((D+1)*(D+1), 0.0);       // associated Legendre (CS phase)
    double pmm = 1.0;
    for (int m = 0; m <= D; ++m){
        if (m > 0) pmm *= -(2*m - 1)*somx2;
        Pcs[idx(m,m)] = pmm;
        if (m+1 <= D) Pcs[idx(m+1,m)] = x*(2*m + 1)*pmm;
        for (int n = m+2; n <= D; ++n)
            Pcs[idx(n,m)] = (x*(2*n - 1)*Pcs[idx(n-1,m)] - (n + m - 1)*Pcs[idx(n-2,m)]) / (n - m);
    }
    std::vector<cd> em(D+1); em[0] = cd(1,0);
    cd e1(std::cos(ph), std::sin(ph));
    for (int m = 1; m <= D; ++m) em[m] = em[m-1]*e1;
    for (int n = 0; n <= D; ++n)
        for (int m = 0; m <= n; ++m){
            double v = Pcs[idx(n,m)]; if (m & 1) v = -v;   // strip CS phase
            double pref = std::sqrt(fac(n-m)/fac(n+m));
            cd base = pref*v*em[m];
            Y[idx(n,m)] = base;
            if (m > 0) Y[idx(n,-m)] = std::conj(base);
        }
}

struct Exp {
    int p; std::vector<cd> c;
    Exp(int P=0): p(P), c((P+1)*(P+1), cd(0,0)) {}
    void zero(){ std::fill(c.begin(), c.end(), cd(0,0)); }
};

void P2M(Exp& M, double q, const dv3& d){
    double r,th,ph; sph(d,r,th,ph);
    thread_local std::vector<cd> Y; buildY(th,ph,M.p,Y);
    thread_local std::vector<double> rp; powfill(r,M.p,rp);
    for (int n = 0; n <= M.p; ++n)
        for (int m = -n; m <= n; ++m)
            M.c[idx(n,m)] += q*rp[n]*std::conj(Y[idx(n,m)]);
}
double L2P(const Exp& L, const dv3& D){
    double r,th,ph; sph(D,r,th,ph);
    thread_local std::vector<cd> Y; buildY(th,ph,L.p,Y);
    thread_local std::vector<double> rp; powfill(r,L.p,rp);
    cd s(0,0);
    for (int n = 0; n <= L.p; ++n)
        for (int m = -n; m <= n; ++m)
            s += L.c[idx(n,m)]*rp[n]*Y[idx(n,m)];
    return s.real();
}
// child multipole O -> parent, using precomputed shift table Ys (deg p) + rho powers
void M2M(const Exp& O, const std::vector<cd>& Ys, const std::vector<double>& rp, Exp& M){
    int p = M.p;
    for (int j = 0; j <= p; ++j) for (int k = -j; k <= j; ++k){
        cd sum(0,0);
        for (int n = 0; n <= j; ++n){ int jn = j - n;
            for (int m = -n; m <= n; ++m){ int km = k - m; if (km < -jn || km > jn) continue;
                sum += O.c[idx(jn,km)]*IPOW(std::abs(k)-std::abs(m)-std::abs(km))
                     * Atab(n,m)*Atab(jn,km)*rp[n]*std::conj(Ys[idx(n,m)]);
            } }
        double aj = Atab(j,k); if (aj != 0.0) M.c[idx(j,k)] += sum/aj;
    }
}
// source multipole O -> local, shift table Ys (deg 2p) + rho powers (deg 2p+1)
void M2L(const Exp& O, const std::vector<cd>& Ys, const std::vector<double>& rp, Exp& L){
    int p = L.p;
    for (int j = 0; j <= p; ++j) for (int k = -j; k <= j; ++k){
        cd sum(0,0); double aj = Atab(j,k); double sgn = (j & 1) ? -1.0 : 1.0;
        for (int n = 0; n <= p; ++n) for (int m = -n; m <= n; ++m){
            int mk = m - k; double a2 = Atab(j+n, mk); if (a2 == 0.0) continue;
            sum += O.c[idx(n,m)]*IPOW(std::abs(k-m)-std::abs(k)-std::abs(m))
                 * Atab(n,m)*aj*Ys[idx(j+n,mk)] / (sgn*a2*rp[j+n+1]);
        }
        L.c[idx(j,k)] += sum;
    }
}
// parent local O -> child, shift table Ys (deg p) + rho powers
void L2L(const Exp& O, const std::vector<cd>& Ys, const std::vector<double>& rp, Exp& L){
    int p = L.p;
    for (int j = 0; j <= p; ++j) for (int k = -j; k <= j; ++k){
        cd sum(0,0);
        for (int n = j; n <= p; ++n) for (int m = -n; m <= n; ++m){
            int mk = m - k, nj = n - j; if (mk < -nj || mk > nj) continue;
            double anm = Atab(n,m); if (anm == 0.0) continue;
            sum += O.c[idx(n,m)]*IPOW(std::abs(m)-std::abs(mk)-std::abs(k))
                 * Atab(nj,mk)*Atab(j,k)*Ys[idx(nj,mk)]*rp[nj] / anm;
        }
        L.c[idx(j,k)] += sum;
    }
}

struct Box { int ix,iy,iz; dv3 center; Exp M,L; std::vector<int> bodies; };
inline uint64_t keyOf(int x,int y,int z){ return (uint64_t(x)<<42)|(uint64_t(y)<<21)|uint64_t(z); }

struct Tree {
    int p, L; double G, eps2;
    dv3 lo; double S, cell;
    std::vector<std::unordered_map<uint64_t,int>> lvlMap;
    std::vector<std::vector<Box>> lvl;
    std::unordered_map<uint64_t,std::vector<cd>> m2lDir;   // integer offset -> Y (deg 2p)
    std::vector<cd> childDir[8];                            // 8 child-direction Y (deg p)

    dv3 boxCenter(int l,int ix,int iy,int iz){
        double cs = S/double(1<<l);
        return lo + dv3((ix+0.5)*cs,(iy+0.5)*cs,(iz+0.5)*cs);
    }
    void build(const std::vector<dv3>& pos){
        dv3 mn = pos[0], mx = pos[0];
        for (auto& q : pos){ mn = glm::min(mn,q); mx = glm::max(mx,q); }
        dv3 c = (mn+mx)*0.5; double half = 0.0;
        for (int a = 0; a < 3; ++a) half = std::max(half, std::max(mx[a]-c[a], c[a]-mn[a]));
        half *= 1.0001; if (half <= 0) half = 1.0;
        lo = c - dv3(half); S = 2*half; cell = S/double(1<<L);
        lvlMap.assign(L+1, {}); lvl.assign(L+1, {});
        int N = 1<<L;
        for (int i = 0; i < (int)pos.size(); ++i){
            int ix = std::min(N-1, std::max(0,(int)((pos[i].x - lo.x)/cell)));
            int iy = std::min(N-1, std::max(0,(int)((pos[i].y - lo.y)/cell)));
            int iz = std::min(N-1, std::max(0,(int)((pos[i].z - lo.z)/cell)));
            uint64_t kk = keyOf(ix,iy,iz); auto it = lvlMap[L].find(kk); int bi;
            if (it == lvlMap[L].end()){ bi = lvl[L].size(); Box b; b.ix=ix;b.iy=iy;b.iz=iz;
                b.center=boxCenter(L,ix,iy,iz); b.M=Exp(p); b.L=Exp(p);
                lvl[L].push_back(std::move(b)); lvlMap[L][kk]=bi; } else bi = it->second;
            lvl[L][bi].bodies.push_back(i);
        }
        for (int l = L-1; l >= 0; --l)
            for (auto& cb : lvl[l+1]){
                int px=cb.ix>>1, py=cb.iy>>1, pz=cb.iz>>1; uint64_t kk=keyOf(px,py,pz);
                if (lvlMap[l].find(kk) == lvlMap[l].end()){
                    int bi=lvl[l].size(); Box b; b.ix=px;b.iy=py;b.iz=pz;
                    b.center=boxCenter(l,px,py,pz); b.M=Exp(p); b.L=Exp(p);
                    lvl[l].push_back(std::move(b)); lvlMap[l][kk]=bi;
                }
            }
        for (int s = 0; s < 8; ++s){
            dv3 t((s&1)?1:-1,(s&2)?1:-1,(s&4)?1:-1);
            double r,th,ph; sph(t,r,th,ph); buildY(th,ph,p,childDir[s]);
        }
    }
    const std::vector<cd>& m2lTable(int dx,int dy,int dz){
        uint64_t k = ((uint64_t)(dx+16)<<20)|((uint64_t)(dy+16)<<10)|(uint64_t)(dz+16);
        auto it = m2lDir.find(k); if (it != m2lDir.end()) return it->second;
        dv3 t(dx,dy,dz); double r,th,ph; sph(t,r,th,ph);
        std::vector<cd> Y; buildY(th,ph,2*p,Y);
        return m2lDir.emplace(k, std::move(Y)).first->second;
    }
    // Pre-populate every possible interaction-list offset so the parallel M2L pass
    // only reads the cache (concurrent unordered_map reads are safe; writes are not).
    void prepM2L(){
        for (int dx=-3; dx<=3; ++dx) for (int dy=-3; dy<=3; ++dy) for (int dz=-3; dz<=3; ++dz){
            int cheb = std::max(std::abs(dx), std::max(std::abs(dy), std::abs(dz)));
            if (cheb >= 2) m2lTable(dx,dy,dz);      // V-list offsets have cheb 2 or 3
        }
    }
    const std::vector<cd>* m2lFind(int dx,int dy,int dz) const {
        uint64_t k = ((uint64_t)(dx+16)<<20)|((uint64_t)(dy+16)<<10)|(uint64_t)(dz+16);
        auto it = m2lDir.find(k); return it==m2lDir.end()? nullptr : &it->second;
    }
    void solve(const std::vector<dv3>& pos, const std::vector<double>& q, std::vector<dv3>& g){
        int N = 1<<L;
        prepM2L();   // fill direction cache once, single-threaded

        // ---- upward: P2M at leaves (parallel over leaves) ----
        {
            auto& leaves = lvl[L];
            parallel::forRange(leaves.size(), [&](size_t lo, size_t hi){
                for (size_t bi = lo; bi < hi; ++bi){
                    Box& b = leaves[bi]; b.M.zero();
                    for (int i : b.bodies) P2M(b.M, q[i], pos[i]-b.center);
                }
            });
        }
        // ---- upward: M2M (parallel over parents; each parent reads its children) ----
        for (int l = L-1; l >= 0; --l){
            double csP = S/double(1<<l); double rho = std::sqrt(3.0)*0.25*csP;
            std::vector<double> rpC; powfill(rho, p, rpC);
            auto& par = lvl[l]; auto& chMap = lvlMap[l+1]; auto& ch = lvl[l+1];
            parallel::forRange(par.size(), [&](size_t lo, size_t hi){
                for (size_t pi = lo; pi < hi; ++pi){
                    Box& P = par[pi]; P.M.zero();
                    for (int dx=0; dx<2; ++dx) for (int dy=0; dy<2; ++dy) for (int dz=0; dz<2; ++dz){
                        auto it = chMap.find(keyOf(2*P.ix+dx, 2*P.iy+dy, 2*P.iz+dz));
                        if (it == chMap.end()) continue;
                        Box& C = ch[it->second];
                        int s = (dx?1:0)|(dy?2:0)|(dz?4:0);
                        M2M(C.M, childDir[s], rpC, P.M);
                    }
                }
            });
        }
        // ---- zero locals ----
        for (int l = 0; l <= L; ++l) for (auto& b : lvl[l]) b.L.zero();

        // ---- M2L over interaction lists (parallel over target boxes) ----
        for (int l = 2; l <= L; ++l){
            int Nl = 1<<l; double cs = S/double(Nl);
            auto& boxes = lvl[l]; auto& map = lvlMap[l];
            parallel::forRange(boxes.size(), [&](size_t lo, size_t hi){
                thread_local std::vector<double> rp;
                for (size_t bi = lo; bi < hi; ++bi){
                    Box& B = boxes[bi];
                    int ppx=B.ix>>1, ppy=B.iy>>1, ppz=B.iz>>1;
                    for (int nx=ppx-1;nx<=ppx+1;++nx) for (int ny=ppy-1;ny<=ppy+1;++ny) for (int nz=ppz-1;nz<=ppz+1;++nz){
                        if (nx<0||ny<0||nz<0||nx>=(Nl>>1)||ny>=(Nl>>1)||nz>=(Nl>>1)) continue;
                        for (int cx=2*nx;cx<=2*nx+1;++cx) for (int cy=2*ny;cy<=2*ny+1;++cy) for (int cz=2*nz;cz<=2*nz+1;++cz){
                            if (std::abs(cx-B.ix)<=1 && std::abs(cy-B.iy)<=1 && std::abs(cz-B.iz)<=1) continue;
                            auto it = map.find(keyOf(cx,cy,cz)); if (it == map.end()) continue;
                            Box& C = boxes[it->second];
                            int dx=B.ix-cx, dy=B.iy-cy, dz=B.iz-cz;
                            const std::vector<cd>* Ys = m2lFind(dx,dy,dz); if (!Ys) continue;
                            double rho = std::sqrt(double(dx*dx+dy*dy+dz*dz))*cs; powfill(rho,2*p+1,rp);
                            M2L(C.M, *Ys, rp, B.L);
                        }
                    }
                }
            });
        }
        // ---- downward: L2L (parallel over parents; each child has one parent) ----
        for (int l = 2; l <= L-1; ++l){
            double csP = S/double(1<<l); double rho = std::sqrt(3.0)*0.25*csP;
            std::vector<double> rpC; powfill(rho, p, rpC);
            auto& par = lvl[l]; auto& chMap = lvlMap[l+1]; auto& ch = lvl[l+1];
            parallel::forRange(par.size(), [&](size_t lo, size_t hi){
                for (size_t pi = lo; pi < hi; ++pi){
                    Box& B = par[pi];
                    for (int dx=0; dx<2; ++dx) for (int dy=0; dy<2; ++dy) for (int dz=0; dz<2; ++dz){
                        auto it = chMap.find(keyOf(2*B.ix+dx, 2*B.iy+dy, 2*B.iz+dz));
                        if (it == chMap.end()) continue;
                        Box& C = ch[it->second];
                        int s = (dx?1:0)|(dy?2:0)|(dz?4:0);
                        L2L(B.L, childDir[s], rpC, C.L);
                    }
                }
            });
        }
        // ---- leaf evaluation: far-field (FD of local) + near-field (softened direct) ----
        g.assign(pos.size(), dv3(0.0));
        double h = cell*1e-2;
        {
            auto& leaves = lvl[L]; auto& map = lvlMap[L];
            parallel::forRange(leaves.size(), [&](size_t lo, size_t hi){
                for (size_t bi = lo; bi < hi; ++bi){
                    Box& B = leaves[bi];
                    for (int i : B.bodies){
                        dv3 d = pos[i]-B.center;
                        dv3 gf(
                            (L2P(B.L,d+dv3(h,0,0))-L2P(B.L,d-dv3(h,0,0)))/(2*h),
                            (L2P(B.L,d+dv3(0,h,0))-L2P(B.L,d-dv3(0,h,0)))/(2*h),
                            (L2P(B.L,d+dv3(0,0,h))-L2P(B.L,d-dv3(0,0,h)))/(2*h));
                        g[i] += gf;
                    }
                    for (int nx=B.ix-1;nx<=B.ix+1;++nx) for (int ny=B.iy-1;ny<=B.iy+1;++ny) for (int nz=B.iz-1;nz<=B.iz+1;++nz){
                        if (nx<0||ny<0||nz<0||nx>=N||ny>=N||nz>=N) continue;
                        auto it = map.find(keyOf(nx,ny,nz)); if (it == map.end()) continue;
                        Box& C = leaves[it->second];
                        for (int i : B.bodies) for (int j : C.bodies){ if (i==j) continue;
                            dv3 dd = pos[j]-pos[i];
                            double r2 = dd.x*dd.x+dd.y*dd.y+dd.z*dd.z+eps2;
                            double inv = 1.0/std::sqrt(r2);
                            g[i] += (q[j]*inv*inv*inv)*dd;
                        }
                    }
                    for (int i : B.bodies) g[i] *= G;
                }
            });
        }
    }
};

} // anonymous namespace

namespace fmm {

int autoDepth(int N, int order){
    if (N <= 1) return 0;
    double occ = std::max(8.0, 2.6*order*order);       // balance M2L vs near-field
    int L = (int)std::lround(std::log2((double)N/occ)/3.0);
    if (L < 0) L = 0; if (L > 6) L = 6;
    return L;
}

void accelerations(const std::vector<dv3>& pos, const std::vector<double>& mass,
                   double G, double softening, int order,
                   std::vector<dv3>& accOut, int depth){
    const int N = (int)pos.size();
    accOut.assign(N, dv3(0.0));
    if (N == 0) return;
    if (order < 1) order = 1;
    ensureTables(order);
    int L = (depth < 0) ? autoDepth(N, order) : depth;

    Tree t; t.p = order; t.L = L; t.G = G; t.eps2 = softening*softening;
    t.build(pos);
    t.solve(pos, mass, accOut);
}

} // namespace fmm
