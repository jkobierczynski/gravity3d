# Sample scenes

Load any of these by passing the path on the command line:

```
gravity3d.exe data\04_galaxy_disk_300.csv       (Windows)
./gravity3d   data/04_galaxy_disk_300.csv       (Linux)
```

All use `G = 1`. Disks are given a proper enclosed-mass rotation curve
(`v = sqrt(G·M_enc/r)`); clusters are sampled from a Plummer model in virial
equilibrium, so they hold together on their own. Each scene was integrated through
the real solver and conserves energy to about 1e-6 or better (the merger included).

| File | N | What it is |
|---|---|---|
| `01_binary_planets.csv` | 5 | Two equal stars orbiting their barycentre, with three circumbinary planets orbiting the pair. |
| `02_solar_system.csv` | 9 | A dominant star with eight planets on near-circular, near-coplanar orbits. |
| `03_random_cloud_100.csv` | 100 | 100 random objects — a cold, mildly rotating cloud that collapses, mixes, and virialises into a rotating clump. |
| `04_galaxy_disk_300.csv` | 301 | A rotating plane of 300 stars around a central bulge; colour runs warm (inner) to cool (outer). |
| `05_globular_cluster_500.csv` | 500 | A 500-star globular cluster (Plummer sphere) in equilibrium; holds its shape and slowly relaxes. |
| `07_three_clusters.csv` | 750 | Three Plummer clusters at the corners of a triangle, drifting inward — they fall together and merge. |
| `06_colliding_galaxies.csv` | 802 | Two rotating disk galaxies (gold + blue) on a collision course. Watch tidal tails and bridges form as they merge. |
| `08_galaxy_disk_3000.csv` | 3001 | A 3000-star galaxy. Heavy for the direct O(N²) solver — press **G** to switch to the O(N) FMM. |

## Few-body, chaotic & semi-chaotic

Small systems where you can follow every body. Turn on **trails** (`T`) — the orbits and
tidal tails are the whole point. These use the CPU direct solver (they're tiny); chaotic
ones are exquisitely sensitive to initial conditions, but each conserves energy, so the
integration is sound even when the motion looks wild.

| File | N | Behaviour |
|---|---|---|
| `14_pythagorean_3body.csv` | 3 | **Chaotic.** Burrau's problem — masses 3-4-5 released from rest. Repeated close passes, then a binary forms and ejects the third body. |
| `15_figure_eight.csv` | 3 | **Choreography.** Three equal masses chase each other around one figure-eight curve. Stable but delicate — softening slowly makes it precess. |
| `16_lagrange_triangle.csv` | 3 | **Semi-chaotic.** An equilateral triangle rotating rigidly; equal masses make it unstable, so it holds, then tips into chaos. |
| `17_hierarchical_triple.csv` | 3 | **Semi-chaotic (Kozai-Lidov).** A tight binary plus a distant, steeply-inclined companion that pumps the pair's eccentricity up and down. |
| `18_binary_binary_scatter.csv` | 4 | **Chaotic scattering.** Two binaries collide off-centre — partner-swaps, a hardened binary, and usually one star flung away. |
| `19_cold_collapse_12.csv` | 12 | **Chaotic.** Twelve masses fall together, overshoot, and violently relax; transient binaries and a couple of ejections. |
| `23_hard_binary_in_cluster.csv` | 62 | **Semi-chaotic (Heggie).** A tight massive binary at a cluster's heart scatters passing stars, hardening itself and heating the cluster. |

## Cluster variations

| File | N | What it is |
|---|---|---|
| `20_double_cluster.csv` | 600 | Two Plummer clusters on a bound orbit about each other — they swing past, raise tidal tails, and merge. |
| `21_cluster_with_blackhole.csv` | 401 | A 400-star cluster around a dominant central mass (a stand-in black hole); a fast, dense nucleus forms. |
| `22_mass_segregation.csv` | 325 | Many light stars plus a few heavy ones (orange) that sink toward the centre as light stars diffuse outward. |

## Large systems (for testing the FMM)

These are sized to exercise the Fast Multipole Method. **Turn the FMM on with `G`** —
the direct O(N²) solver is impractical at these counts. Give it multiple cores
(threading is automatic; the startup line shows how many).

| File | N | What it is |
|---|---|---|
| `09_uniform_cloud_10k.csv` | 10 000 | A rotating uniform 3D cloud that settles into a spheroid. |
| `10_plummer_cluster_20k.csv` | 20 000 | A 20k-star Plummer sphere in equilibrium. |
| `11_galaxy_disk_15k.csv` | 15 001 | A 15k-star rotating galaxy (uniform surface density). |
| `12_uniform_cloud_30k.csv` | 30 000 | A larger rotating cloud — a comfortable FMM showcase. |
| `13_uniform_cloud_100k.csv` | 100 000 | The stress test. Direct is ~seconds/step; the FMM is where this becomes feasible. |

**What to expect.** The FMM uses an **adaptive** octree (it refines only where mass is
dense), so it stays efficient across very different distributions:

- The **uniform clouds** (`09`, `12`, `13`) scale essentially linearly — the cost per
  body is roughly constant as N grows. `13` at 100k is many times faster than the direct
  sum even on one core, and near-interactive with a few cores.
- The **Plummer cluster** (`10`) and the **thin disk** (`11`) are concentrated, so a
  fixed uniform grid used to bog down on their dense cores. The adaptive tree bounds the
  leaf occupancy there, keeping the near-field in check — the Plummer solve is roughly
  2× faster than it was on a uniform tree, and stays balanced across cores.

All were checked: FMM accelerations match direct summation to ~1e-3 (order p=4), and
each integrates without blowing up (energy conserved to ~1e-5–1e-6 over the test).

At 100k, use a smaller `@timescale` or pause (`Space`) to watch the collapse; the point
is that the FMM makes a system this size tractable at all.

## Tips

- **Trails** (`T`) make orbits and tidal tails much clearer — Follow for a moving
  streak, Stay to paint the whole path.
- **Speed** (`+` / `-`) and **pause** (`Space`) help you catch fast events like the
  galaxy pericentre passage (around t≈3–5 in the merger).
- For the big disk, toggle the **solver** with `G` and watch the title bar; the FMM
  keeps up where the direct sum starts to crawl.
- Each file sets its own `@softening`, `@dt`, `@timescale`. If you crank the speed and
  see a system heat up, lower `@dt` a touch — massive cores need a small step to
  resolve close passages.
