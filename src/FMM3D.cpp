#include "FMM3D.h"
#include "Parallel.h"
#include <complex>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <array>
#include <algorithm>

// ============================================================================
//  3D Laplace Fast Multipole Method — ADAPTIVE octree via dual tree traversal.
//
//  The five translation operators (P2M/M2M/M2L/L2L/L2P) are the numerically
//  validated Greengard-Rokhlin ones (no-Condon-Shortley harmonics; sign/phase
//  factors pinned down against direct summation). Only the TREE and the
//  INTERACTION discovery changed: the tree now subdivides only where mass is
//  dense (<= Ncrit points per leaf), and a dual tree traversal decides, for each
//  pair of boxes, whether they are far enough apart to translate (M2L) or must be
//  summed directly (P2P). This bounds leaf occupancy everywhere, so the near-field
//  cost stays ~O(N) no matter how concentrated the system becomes.
// ============================================================================

namespace {

using cd  = std::complex<double>;
using dv3 = glm::dvec3;
using Clock = std::chrono::high_resolution_clock;

// ---- optional per-phase profiling (GRAVITY3D_FMM_PROFILE=1) ----
struct Prof { double build=0,p2m=0,m2m=0,m2l=0,l2l=0,eval=0; int n=0; };
Prof g_prof;
bool profEnabled(){ static int e=-1; if(e<0){ const char* s=std::getenv("GRAVITY3D_FMM_PROFILE"); e=(s&&std::atoi(s))?1:0; } return e; }
inline double msSince(Clock::time_point t){ return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }

// ---- shared math tables ----
std::vector<double> g_fact, g_A;
int g_factDeg=-1, g_Adeg=-1;
cd  g_ip[4] = { cd(1,0), cd(0,1), cd(-1,0), cd(0,-1) };
inline int    idx(int n,int m){ return n*n + n + m; }
inline double fac(int k){ return (k<0)?0.0:g_fact[k]; }
inline cd     IPOW(int e){ int r=e&3; if(r<0)r+=4; return g_ip[r]; }
inline double Atab(int n,int m){ if(m<-n||m>n) return 0.0; return g_A[idx(n,m)]; }

void ensureTables(int order){
    int needF=4*order+8;
    if(g_factDeg<needF){ g_fact.assign(needF+1,1.0); for(int i=1;i<=needF;++i) g_fact[i]=g_fact[i-1]*i; g_factDeg=needF; }
    int needA=2*order+2;
    if(g_Adeg<needA){ g_A.assign((needA+1)*(needA+1),0.0);
        for(int n=0;n<=needA;++n) for(int m=-n;m<=n;++m){ double s=(n&1)?-1.0:1.0; g_A[idx(n,m)]=s/std::sqrt(fac(n-m)*fac(n+m)); }
        g_Adeg=needA; }
}
inline void sph(const dv3&v,double&r,double&th,double&ph){
    r=glm::length(v); if(r<1e-300){ th=0; ph=0; return; }
    th=std::acos(std::max(-1.0,std::min(1.0,v.z/r))); ph=std::atan2(v.y,v.x);
}
inline void powfill(double r,int D,std::vector<double>&rp){ rp.resize(D+1); rp[0]=1.0; for(int i=1;i<=D;++i) rp[i]=rp[i-1]*r; }

void buildY(double th,double ph,int D,std::vector<cd>&Y){
    Y.assign((D+1)*(D+1),cd(0,0));
    double x=std::cos(th), somx2=std::sqrt(std::max(0.0,(1.0-x)*(1.0+x)));
    std::vector<double> Pcs((D+1)*(D+1),0.0); double pmm=1.0;
    for(int m=0;m<=D;++m){
        if(m>0) pmm*=-(2*m-1)*somx2;
        Pcs[idx(m,m)]=pmm;
        if(m+1<=D) Pcs[idx(m+1,m)]=x*(2*m+1)*pmm;
        for(int n=m+2;n<=D;++n) Pcs[idx(n,m)]=(x*(2*n-1)*Pcs[idx(n-1,m)]-(n+m-1)*Pcs[idx(n-2,m)])/(n-m);
    }
    std::vector<cd> em(D+1); em[0]=cd(1,0); cd e1(std::cos(ph),std::sin(ph)); for(int m=1;m<=D;++m) em[m]=em[m-1]*e1;
    for(int n=0;n<=D;++n) for(int m=0;m<=n;++m){
        double v=Pcs[idx(n,m)]; if(m&1) v=-v;
        double pref=std::sqrt(fac(n-m)/fac(n+m)); cd base=pref*v*em[m];
        Y[idx(n,m)]=base; if(m>0) Y[idx(n,-m)]=std::conj(base);
    }
}

struct Exp { int p; std::vector<cd> c; Exp(int P=0):p(P),c((P+1)*(P+1),cd(0,0)){} void zero(){ std::fill(c.begin(),c.end(),cd(0,0)); } };

// Direction harmonics for same-level M2L (offsets in [-3,3]^3, Chebyshev 2..3).
// Depends only on p, so compute once; the parallel pass only reads it.
inline int m2lKey(int dx,int dy,int dz){ return (dx+3)*49+(dy+3)*7+(dz+3); }
const std::array<std::vector<cd>,343>& m2lDirCache(int p){
    static int cachedP=-1; static std::array<std::vector<cd>,343> C;
    if(cachedP!=p){
        for(int dx=-3;dx<=3;++dx) for(int dy=-3;dy<=3;++dy) for(int dz=-3;dz<=3;++dz){
            int ch=std::max(std::abs(dx),std::max(std::abs(dy),std::abs(dz)));
            if(ch<2) continue;
            double r,th,ph; sph(dv3(dx,dy,dz),r,th,ph); buildY(th,ph,2*p,C[m2lKey(dx,dy,dz)]);
        }
        cachedP=p;
    }
    return C;
}

// ---- validated translation operators (unchanged) ----
void P2M(Exp&M,double q,const dv3&d){
    double r,th,ph; sph(d,r,th,ph);
    thread_local std::vector<cd> Y; buildY(th,ph,M.p,Y);
    thread_local std::vector<double> rp; powfill(r,M.p,rp);
    for(int n=0;n<=M.p;++n) for(int m=-n;m<=n;++m) M.c[idx(n,m)]+=q*rp[n]*std::conj(Y[idx(n,m)]);
}
double L2P(const Exp&L,const dv3&D){
    double r,th,ph; sph(D,r,th,ph);
    thread_local std::vector<cd> Y; buildY(th,ph,L.p,Y);
    thread_local std::vector<double> rp; powfill(r,L.p,rp);
    cd s(0,0);
    for(int n=0;n<=L.p;++n) for(int m=-n;m<=n;++m) s+=L.c[idx(n,m)]*rp[n]*Y[idx(n,m)];
    return s.real();
}
void M2M(const Exp&O,const std::vector<cd>&Ys,const std::vector<double>&rp,Exp&M){
    int p=M.p;
    for(int j=0;j<=p;++j) for(int k=-j;k<=j;++k){ cd sum(0,0);
        for(int n=0;n<=j;++n){ int jn=j-n; for(int m=-n;m<=n;++m){ int km=k-m; if(km<-jn||km>jn) continue;
            sum+=O.c[idx(jn,km)]*IPOW(std::abs(k)-std::abs(m)-std::abs(km))*Atab(n,m)*Atab(jn,km)*rp[n]*std::conj(Ys[idx(n,m)]); } }
        double aj=Atab(j,k); if(aj!=0.0) M.c[idx(j,k)]+=sum/aj; }
}
void M2L(const Exp&O,const std::vector<cd>&Ys,const std::vector<double>&rp,Exp&L){
    int p=L.p;
    for(int j=0;j<=p;++j) for(int k=-j;k<=j;++k){ cd sum(0,0); double aj=Atab(j,k); double sgn=(j&1)?-1.0:1.0;
        for(int n=0;n<=p;++n) for(int m=-n;m<=n;++m){ int mk=m-k; double a2=Atab(j+n,mk); if(a2==0.0) continue;
            sum+=O.c[idx(n,m)]*IPOW(std::abs(k-m)-std::abs(k)-std::abs(m))*Atab(n,m)*aj*Ys[idx(j+n,mk)]/(sgn*a2*rp[j+n+1]); }
        L.c[idx(j,k)]+=sum; }
}
void L2L(const Exp&O,const std::vector<cd>&Ys,const std::vector<double>&rp,Exp&L){
    int p=L.p;
    for(int j=0;j<=p;++j) for(int k=-j;k<=j;++k){ cd sum(0,0);
        for(int n=j;n<=p;++n) for(int m=-n;m<=n;++m){ int mk=m-k,nj=n-j; if(mk<-nj||mk>nj) continue; double anm=Atab(n,m); if(anm==0.0) continue;
            sum+=O.c[idx(n,m)]*IPOW(std::abs(m)-std::abs(mk)-std::abs(k))*Atab(nj,mk)*Atab(j,k)*Ys[idx(nj,mk)]*rp[nj]/anm; }
        L.c[idx(j,k)]+=sum; }
}

// ---- adaptive octree ----
struct Box {
    dv3 center; double half; int level;
    int ix,iy,iz;                 // integer coords at this level (for well-separation test)
    int parent; int child[8];
    int first, count;             // range into the reordered point-index array
    bool leaf;
    Exp M, L;
};

struct Tree {
    int p; double G, eps2; int Ncrit, maxLevel;
    dv3 lo; double rootSize;
    std::vector<Box> box;
    std::vector<int> order;              // body indices, grouped by box
    std::vector<int> bodyLeaf;           // body -> leaf box index
    std::vector<std::vector<int>> levelBoxes;
    std::vector<int> leaves;
    std::vector<cd> childDir[8];         // child-direction harmonic tables (deg p)
    std::vector<std::vector<int>> m2lSrc, p2pSrc;   // per target box: source boxes

    inline int octant(int b) const { return (box[b].ix&1) | ((box[b].iy&1)<<1) | ((box[b].iz&1)<<2); }

    int build_rec(const std::vector<dv3>& pos, int first,int count,
                  const dv3& c,double half,int level,int ix,int iy,int iz,int parent){
        int bi=(int)box.size();
        Box b; b.center=c; b.half=half; b.level=level; b.ix=ix; b.iy=iy; b.iz=iz;
        b.parent=parent; for(int o=0;o<8;++o) b.child[o]=-1;
        b.first=first; b.count=count; b.leaf=true; b.M=Exp(p); b.L=Exp(p);
        box.push_back(std::move(b));
        if(count<=Ncrit || level>=maxLevel) return bi;
        // partition order[first..first+count) into 8 octants about c
        int cnt[8]={0};
        for(int k=first;k<first+count;++k){ const dv3& q=pos[order[k]];
            int o=(q.x>c.x?1:0)|(q.y>c.y?2:0)|(q.z>c.z?4:0); cnt[o]++; }
        int start[8]; int s=first; for(int o=0;o<8;++o){ start[o]=s; s+=cnt[o]; }
        std::vector<int> tmp(count); int put[8]; for(int o=0;o<8;++o) put[o]=start[o]-first;
        for(int k=first;k<first+count;++k){ const dv3& q=pos[order[k]];
            int o=(q.x>c.x?1:0)|(q.y>c.y?2:0)|(q.z>c.z?4:0); tmp[put[o]++]=order[k]; }
        for(int k=0;k<count;++k) order[first+k]=tmp[k];
        double h=half*0.5;
        for(int o=0;o<8;++o){ if(cnt[o]==0) continue;
            double sx=(o&1)?1.0:-1.0, sy=(o&2)?1.0:-1.0, sz=(o&4)?1.0:-1.0;
            dv3 cc(c.x+sx*h, c.y+sy*h, c.z+sz*h);
            int ci=build_rec(pos,start[o],cnt[o],cc,h,level+1,
                             2*ix+((o&1)?1:0),2*iy+((o&2)?1:0),2*iz+((o&4)?1:0),bi);
            box[bi].child[o]=ci;
        }
        box[bi].leaf=false;
        return bi;
    }

    void build(const std::vector<dv3>& pos){
        int N=(int)pos.size();
        dv3 mn=pos[0],mx=pos[0]; for(auto&q:pos){ mn=glm::min(mn,q); mx=glm::max(mx,q); }
        dv3 c=(mn+mx)*0.5; double half=0; for(int a=0;a<3;++a) half=std::max(half,std::max(mx[a]-c[a],c[a]-mn[a]));
        half*=1.0001; if(half<=0) half=1.0; lo=c-dv3(half); rootSize=2*half;
        order.resize(N); for(int i=0;i<N;++i) order[i]=i;
        box.clear(); box.reserve(std::max(8,2*N/std::max(1,Ncrit)));
        build_rec(pos,0,N,c,half,0,0,0,0,-1);
        // level buckets + leaves + bodyLeaf
        int maxL=0; for(auto&b:box) maxL=std::max(maxL,b.level);
        levelBoxes.assign(maxL+1,{}); leaves.clear(); bodyLeaf.assign(N,-1);
        for(int bi=0;bi<(int)box.size();++bi){ levelBoxes[box[bi].level].push_back(bi);
            if(box[bi].leaf){ leaves.push_back(bi);
                for(int k=box[bi].first;k<box[bi].first+box[bi].count;++k) bodyLeaf[order[k]]=bi; } }
        for(int s=0;s<8;++s){ dv3 t((s&1)?1:-1,(s&2)?1:-1,(s&4)?1:-1); double r,th,ph; sph(t,r,th,ph); buildY(th,ph,p,childDir[s]); }
    }

    // well separated: non-adjacent when both viewed at the coarser box's level
    inline bool wellSep(int a,int b) const {
        int la=box[a].level, lb=box[b].level, lc=std::min(la,lb);
        int ax=box[a].ix>>(la-lc), ay=box[a].iy>>(la-lc), az=box[a].iz>>(la-lc);
        int bx=box[b].ix>>(lb-lc), by=box[b].iy>>(lb-lc), bz=box[b].iz>>(lb-lc);
        int d=std::max(std::abs(ax-bx),std::max(std::abs(ay-by),std::abs(az-bz)));
        return d>=2;
    }

    // dual tree traversal: classify each box pair as M2L (far) or P2P (near leaves)
    void traverse(int a,int b){
        if(wellSep(a,b)){ m2lSrc[b].push_back(a); return; }
        bool al=box[a].leaf, bl=box[b].leaf;
        if(al&&bl){ p2pSrc[b].push_back(a); return; }
        bool splitA = bl ? true : (al ? false : (box[a].half>=box[b].half));
        if(splitA){ for(int o=0;o<8;++o){ int c=box[a].child[o]; if(c>=0) traverse(c,b); } }
        else      { for(int o=0;o<8;++o){ int c=box[b].child[o]; if(c>=0) traverse(a,c); } }
    }

    void solve(const std::vector<dv3>& pos, const std::vector<double>& q, std::vector<dv3>& g){
        const bool prof=profEnabled(); auto T=Clock::now();
        int maxL=(int)levelBoxes.size()-1;

        // ---- upward: P2M (parallel over leaves) ----
        parallel::forRange(leaves.size(),[&](size_t lo_,size_t hi){
            for(size_t li=lo_;li<hi;++li){ Box& b=box[leaves[li]]; b.M.zero();
                for(int k=b.first;k<b.first+b.count;++k) P2M(b.M,q[order[k]],pos[order[k]]-b.center); }
        });
        if(prof){ g_prof.p2m+=msSince(T); T=Clock::now(); }

        // ---- upward: M2M (levels high->low; parallel over internal boxes) ----
        for(int l=maxL-1;l>=0;--l){
            auto& lv=levelBoxes[l];
            parallel::forRange(lv.size(),[&](size_t lo_,size_t hi){
                thread_local std::vector<double> rpC;
                for(size_t ii=lo_;ii<hi;++ii){ int bi=lv[ii]; Box& B=box[bi]; if(B.leaf) continue; B.M.zero();
                    double rho=std::sqrt(3.0)*0.5*B.half; powfill(rho,p,rpC);
                    for(int o=0;o<8;++o){ int c=B.child[o]; if(c<0) continue; M2M(box[c].M,childDir[octant(c)],rpC,B.M); } }
            });
        }
        if(prof){ g_prof.m2m+=msSince(T); T=Clock::now(); }

        // ---- interaction discovery (serial dual tree traversal) ----
        m2lSrc.assign(box.size(),{}); p2pSrc.assign(box.size(),{});
        traverse(0,0);
        for(auto& b:box) b.L.zero();
        if(std::getenv("GRAVITY3D_FMM_STATS")){
            long m2lSame=0,m2lCross=0,p2p=0,p2pPairs=0; int maxLf=0;
            for(int bi=0;bi<(int)box.size();++bi){
                for(int a:m2lSrc[bi]){ if(box[a].level==box[bi].level) ++m2lSame; else ++m2lCross; }
                for(int a:p2pSrc[bi]){ ++p2p; p2pPairs+=box[a].count; }
            }
            for(int lf:leaves) maxLf=std::max(maxLf,box[lf].count);
            std::fprintf(stderr,"[FMM stats] boxes=%zu leaves=%zu maxLvl=%d | M2L same=%ld cross=%ld | P2P boxpairs=%ld pointpairs=%ld maxLeaf=%d\n",
                box.size(),leaves.size(),(int)levelBoxes.size()-1,m2lSame,m2lCross,p2p,p2pPairs,maxLf);
        }
        if(prof){ g_prof.build+=msSince(T); T=Clock::now(); }   // fold traversal into build phase

        // ---- downward: L2L from parent then M2L from far sources (levels low->high) ----
        const std::array<std::vector<cd>,343>& dirCache=m2lDirCache(p);   // populate serially
        for(int l=0;l<=maxL;++l){
            auto& lv=levelBoxes[l];
            parallel::forRange(lv.size(),[&](size_t lo_,size_t hi){
                thread_local std::vector<cd> Ys; thread_local std::vector<double> rp, rpC;
                for(size_t ii=lo_;ii<hi;++ii){ int bi=lv[ii]; Box& B=box[bi];
                    if(B.parent>=0){ double rho=std::sqrt(3.0)*B.half; powfill(rho,p,rpC);
                        L2L(box[B.parent].L,childDir[octant(bi)],rpC,B.L); }
                    for(int a:m2lSrc[bi]){ const Box& S=box[a];
                        int dx=B.ix-S.ix, dy=B.iy-S.iy, dz=B.iz-S.iz;
                        bool cached = (B.level==S.level) &&
                                      std::abs(dx)<=3 && std::abs(dy)<=3 && std::abs(dz)<=3;
                        if(cached){ double cs=rootSize/double(1<<B.level);
                            double rho=std::sqrt((double)(dx*dx+dy*dy+dz*dz))*cs; powfill(rho,2*p+1,rp);
                            M2L(S.M, dirCache[m2lKey(dx,dy,dz)], rp, B.L);
                        } else { dv3 t=B.center-S.center; double r,th,ph; sph(t,r,th,ph);
                            buildY(th,ph,2*p,Ys); powfill(r,2*p+1,rp); M2L(S.M,Ys,rp,B.L); }
                    } }
            });
        }
        if(prof){ g_prof.m2l+=msSince(T); T=Clock::now(); }

        // ---- evaluation (parallel over BODIES: far = grad of local, near = direct) ----
        g.assign(pos.size(),dv3(0.0));
        parallel::forRange(pos.size(),[&](size_t lo_,size_t hi){
            for(size_t i=lo_;i<hi;++i){ int bi=bodyLeaf[i]; if(bi<0) continue; Box& B=box[bi];
                double h=B.half*2e-2; dv3 d=pos[i]-B.center;
                dv3 gi(
                    (L2P(B.L,d+dv3(h,0,0))-L2P(B.L,d-dv3(h,0,0)))/(2*h),
                    (L2P(B.L,d+dv3(0,h,0))-L2P(B.L,d-dv3(0,h,0)))/(2*h),
                    (L2P(B.L,d+dv3(0,0,h))-L2P(B.L,d-dv3(0,0,h)))/(2*h));
                const dv3 pi=pos[i];
                for(int a:p2pSrc[bi]){ const Box& S=box[a];
                    for(int k=S.first;k<S.first+S.count;++k){ int j=order[k]; if(j==(int)i) continue;
                        dv3 dd=pos[j]-pi; double r2=dd.x*dd.x+dd.y*dd.y+dd.z*dd.z+eps2; double inv=1.0/std::sqrt(r2);
                        gi+=(q[j]*inv*inv*inv)*dd; } }
                g[i]=G*gi;
            }
        });
        if(prof){ g_prof.eval+=msSince(T);
            if(++g_prof.n>=30){ double n=g_prof.n;
                std::fprintf(stderr,"[FMM/%d thr] avg ms over %d solves: build=%.2f P2M=%.2f M2M=%.2f M2L=%.2f L2L=%.2f eval=%.2f | total=%.2f\n",
                    (int)parallel::threadCount(),(int)n,g_prof.build/n,g_prof.p2m/n,g_prof.m2m/n,g_prof.m2l/n,g_prof.l2l/n,g_prof.eval/n,
                    (g_prof.build+g_prof.p2m+g_prof.m2m+g_prof.m2l+g_prof.l2l+g_prof.eval)/n);
                g_prof=Prof{}; }
        }
    }
};

} // anonymous namespace

namespace fmm {

int autoDepth(int N,int order){          // kept for API compat; estimate only
    if(N<=1) return 0;
    double occ=std::max(8.0,2.6*order*order);
    int L=(int)std::lround(std::log2((double)N/occ)/3.0);
    return L<0?0:(L>7?7:L);
}

void accelerations(const std::vector<dv3>& pos, const std::vector<double>& mass,
                   double G, double softening, int order,
                   std::vector<dv3>& accOut, int depth){
    const int N=(int)pos.size();
    accOut.assign(N,dv3(0.0));
    if(N==0) return;
    if(order<1) order=1;
    ensureTables(order);

    int Ncrit=std::max(64, 6*order*order);       // adaptive leaf capacity (balances M2L vs near-field)
    if(const char* e=std::getenv("GRAVITY3D_FMM_LEAF")){ int v=std::atoi(e); if(v>=1) Ncrit=v; }
    (void)depth;

    bool prof=profEnabled(); auto tb=Clock::now();
    Tree t; t.p=order; t.G=G; t.eps2=softening*softening; t.Ncrit=Ncrit; t.maxLevel=20;
    t.build(pos);
    if(prof) g_prof.build+=msSince(tb);
    t.solve(pos,mass,accOut);
}

} // namespace fmm
