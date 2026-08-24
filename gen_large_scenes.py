#!/usr/bin/env python3
"""
Large-scale scene generator for gravity3d — the kinds of systems GPU N-body codes
(Bonsai, GADGET, ExaFMM demos) like to run. Uses numpy, scales to millions of bodies.

Usage:
    python gen_large_scenes.py <scenario> <N> <out.csv>

Scenarios:
    cosmic_web          cold, clumpy box that collapses into filaments + halos (structure formation)
    spiral_galaxy       live disk + bulge + dark halo; develops spiral arms / a bar
    galaxy_merger       two disk galaxies on a collision orbit -> tidal tails, merger
    galaxy_cluster      many galaxy-sized halos bound in a cluster potential
    colliding_clusters  two galaxy clusters colliding (Bullet-cluster style)

Examples:
    python gen_large_scenes.py spiral_galaxy      300000 disk300k.csv
    python gen_large_scenes.py galaxy_merger      600000 merger600k.csv
    python gen_large_scenes.py cosmic_web        1000000 web1m.csv
    python gen_large_scenes.py galaxy_cluster    1000000 cluster1m.csv
    python gen_large_scenes.py colliding_clusters 2000000 bullet2m.csv
    python gen_large_scenes.py cosmic_web        3000000 web3m.csv
"""
import sys, numpy as np

G = 1.0
rng = np.random.default_rng(1234)


def _dirs(n):
    v = rng.normal(size=(n, 3))
    return v / np.linalg.norm(v, axis=1, keepdims=True)


def plummer(n, M, a, center, bulk, rcut=12.0):
    """Plummer sphere in virial equilibrium (Aarseth velocity sampling), radius-truncated."""
    center = np.asarray(center, float); bulk = np.asarray(bulk, float)
    umax = (1.0 + 1.0 / rcut ** 2) ** (-1.5)   # bound radius to rcut*a (drop the far tail)
    u = rng.random(n) * umax
    r = a / np.sqrt(u ** (-2.0 / 3.0) - 1.0)
    pos = _dirs(n) * r[:, None]
    # speed: q ~ g(q)=q^2 (1-q^2)^3.5 by rejection, then v = q * v_escape
    q = np.empty(n)
    filled = 0
    while filled < n:
        m = n - filled
        x = rng.random(m); y = rng.random(m) * 0.1
        ok = y < x * x * (1 - x * x) ** 3.5
        k = np.count_nonzero(ok)
        q[filled:filled + k] = x[ok][:m]
        filled += k
    ve = np.sqrt(2.0) * (1.0 + (r / a) ** 2) ** (-0.25) * np.sqrt(G * M / a)
    vel = _dirs(n) * (q * ve)[:, None]
    m = np.full(n, M / n)
    return pos + center, vel + bulk, m


def _menc_plummer(R, M, a):
    return M * R ** 3 / (R ** 2 + a ** 2) ** 1.5


def _menc_disk(R, Md, Rd):
    x = R / Rd
    return Md * (1.0 - (1.0 + x) * np.exp(-x))


def galaxy(n, center, bulk, Rd=3.0, Md=1.0, seed_spin=+1.0):
    """Live disk galaxy: exponential disk + Plummer bulge + Plummer dark halo."""
    center = np.asarray(center, float); bulk = np.asarray(bulk, float)
    n_disk = int(n * 0.6); n_bulge = int(n * 0.15); n_halo = n - n_disk - n_bulge
    Mb, ab = 0.3 * Md, 0.4 * Rd
    Mh, ah = 3.0 * Md, 3.5 * Rd  # dark halo flattens the rotation curve (compact for framing)

    # --- disk: R ~ Gamma(2,Rd) (exponential surface density), thin ---
    R = rng.gamma(2.0, Rd, n_disk)
    phi = rng.random(n_disk) * 2 * np.pi
    z = rng.laplace(0.0, 0.12 * Rd, n_disk)
    dpos = np.stack([R * np.cos(phi), R * np.sin(phi), z], axis=1)
    Menc = _menc_disk(R, Md, Rd) + _menc_plummer(R, Mb, ab) + _menc_plummer(R, Mh, ah)
    vc = np.sqrt(G * Menc / np.maximum(R, 1e-3))
    tang = np.stack([-np.sin(phi), np.cos(phi), np.zeros_like(phi)], axis=1) * seed_spin
    disp = 0.08 * vc[:, None] * rng.normal(size=(n_disk, 3))
    dvel = tang * vc[:, None] + disp
    dm = np.full(n_disk, Md / n_disk)

    bpos, bvel, bm = plummer(n_bulge, Mb, ab, [0, 0, 0], [0, 0, 0])
    hpos, hvel, hm = plummer(n_halo, Mh, ah, [0, 0, 0], [0, 0, 0])

    pos = np.concatenate([dpos, bpos, hpos]) + center
    vel = np.concatenate([dvel, bvel, hvel]) + bulk
    m = np.concatenate([dm, bm, hm])
    # colours: disk warm->cool by radius, bulge gold, halo dim blue
    col = np.empty((n, 3))
    t = np.clip(R / (5 * Rd), 0, 1)
    col[:n_disk] = np.stack([1 - 0.4 * t, 0.75 + 0.1 * t, 0.5 + 0.5 * t], axis=1)
    col[n_disk:n_disk + n_bulge] = [1.0, 0.85, 0.55]
    col[n_disk + n_bulge:] = [0.45, 0.55, 0.85]
    return pos, vel, m, col


def scen_spiral_galaxy(N):
    pos, vel, m, col = galaxy(N, [0, 0, 0], [0, 0, 0], Rd=3.0, Md=1.0)
    return pos, vel, m, col, dict(G=1, softening=0.05, dt=0.002, timescale=0.3)


def scen_galaxy_merger(N):
    n1 = N // 2
    sep, vrel = 45.0, 0.35
    p1, v1, m1, c1 = galaxy(n1, [-sep / 2, 0, 0], [vrel / 2, 0.05, 0], Rd=3.0, Md=1.0, seed_spin=+1)
    p2, v2, m2, c2 = galaxy(N - n1, [sep / 2, 6, 3], [-vrel / 2, -0.05, 0], Rd=2.6, Md=0.8, seed_spin=-1)
    c2 = c2 * np.array([0.7, 0.8, 1.0])  # tint the second galaxy bluer
    return (np.concatenate([p1, p2]), np.concatenate([v1, v2]),
            np.concatenate([m1, m2]), np.concatenate([c1, c2]),
            dict(G=1, softening=0.06, dt=0.0015, timescale=0.3))


def scen_cosmic_web(N):
    """Cold, clumpy cube -> gravitational collapse into filaments and halos."""
    L = 60.0
    n_bg = int(N * 0.35)
    n_cl = N - n_bg
    bg = (rng.random((n_bg, 3)) - 0.5) * L
    # multi-scale clumps seed hierarchical structure (the 'web')
    nclumps = max(30, N // 8000)
    centers = (rng.random((nclumps, 3)) - 0.5) * L
    sizes = rng.uniform(0.6, 4.0, nclumps)
    counts = rng.multinomial(n_cl, np.ones(nclumps) / nclumps)
    parts = [bg]
    for c, s, k in zip(centers, sizes, counts):
        if k:
            parts.append(c + rng.normal(0, s, (k, 3)))
    pos = np.concatenate(parts)[:N]
    if len(pos) < N:
        pos = np.concatenate([pos, (rng.random((N - len(pos), 3)) - 0.5) * L])
    vel = rng.normal(0, 0.02, (N, 3))  # cold start
    m = np.full(N, 1.0 / N * 40.0)
    d = np.linalg.norm(pos, axis=1)
    t = np.clip(d / (0.5 * L), 0, 1)
    col = np.stack([0.6 + 0.4 * t, 0.7 - 0.2 * t, 1.0 - 0.4 * t], axis=1)
    return pos, vel, m, col, dict(G=1, softening=0.08, dt=0.004, timescale=0.25)


def _cluster(n, center, bulk, Rcl=18.0, Mcl=1.0, ngal=None):
    """A galaxy cluster: many small halos placed in a big Plummer cluster potential."""
    center = np.asarray(center, float); bulk = np.asarray(bulk, float)
    if ngal is None:
        ngal = max(20, n // 6000)
    # galaxy-centre positions/velocities from the cluster Plummer
    gc_pos, gc_vel, _ = plummer(ngal, Mcl, Rcl, [0, 0, 0], [0, 0, 0])
    counts = rng.multinomial(n, np.ones(ngal) / ngal)
    P, Vv, M, C = [], [], [], []
    for i in range(ngal):
        k = counts[i]
        if k < 4:
            continue
        ag = rng.uniform(0.6, 1.6)
        mg = Mcl / ngal * rng.uniform(0.5, 1.5)
        p, v, m = plummer(k, mg, ag, gc_pos[i], gc_vel[i])
        P.append(p); Vv.append(v); M.append(m)
        hue = rng.uniform(0.6, 1.0)
        C.append(np.tile([hue, 0.8 * hue + 0.2, 1.2 - 0.4 * hue], (k, 1)))
    pos = np.concatenate(P) + center
    vel = np.concatenate(Vv) + bulk
    return pos, vel, np.concatenate(M), np.concatenate(C)


def scen_galaxy_cluster(N):
    p, v, m, c = _cluster(N, [0, 0, 0], [0, 0, 0], Rcl=18.0, Mcl=1.0)
    return p, v, m, c, dict(G=1, softening=0.08, dt=0.003, timescale=0.3)


def scen_colliding_clusters(N):
    n1 = N // 2
    sep, vrel = 90.0, 0.5
    p1, v1, m1, c1 = _cluster(n1, [-sep / 2, 0, 0], [vrel / 2, 0, 0], Rcl=15.0, Mcl=1.0)
    p2, v2, m2, c2 = _cluster(N - n1, [sep / 2, 10, 0], [-vrel / 2, 0, 0], Rcl=13.0, Mcl=0.8)
    c2 = c2 * np.array([1.0, 0.7, 0.6])
    return (np.concatenate([p1, p2]), np.concatenate([v1, v2]), np.concatenate([m1, m2]),
            np.concatenate([c1, c2]), dict(G=1, softening=0.1, dt=0.0025, timescale=0.25))


SCENARIOS = {
    "cosmic_web": scen_cosmic_web,
    "spiral_galaxy": scen_spiral_galaxy,
    "galaxy_merger": scen_galaxy_merger,
    "galaxy_cluster": scen_galaxy_cluster,
    "colliding_clusters": scen_colliding_clusters,
}


def write_csv(path, pos, vel, m, col, cfg):
    # zero total momentum so the frame doesn't drift
    p = (m[:, None] * vel).sum(0) / m.sum()
    vel = vel - p
    rad = np.full(len(m), 0.06)
    name = np.zeros(len(m))  # placeholder column 0 (ignored by loader)
    arr = np.column_stack([name, m, pos, vel, col, rad])
    with open(path, "w") as f:
        for k, v in cfg.items():
            f.write(f"@{k}={v}\n")
        np.savetxt(f, arr, fmt="%.5g", delimiter=",")


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in SCENARIOS:
        print(__doc__); sys.exit(1)
    name, N, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    print(f"generating {name} with {N:,} bodies ...")
    pos, vel, m, col, cfg = SCENARIOS[name](N)
    write_csv(out, pos, vel, m, col, cfg)
    import os
    print(f"wrote {out}  ({len(m):,} bodies, {os.path.getsize(out)/1e6:.0f} MB)")


if __name__ == "__main__":
    main()
