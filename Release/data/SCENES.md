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

**What to expect.** The FMM here uses a *uniform* octree, so it's happiest on
space-filling 3D distributions:

- The **uniform clouds** (`09`, `12`, `13`) scale essentially linearly — the cost per
  body is roughly constant as N grows. These are the clean large-N wins; `13` at 100k
  is many times faster than the direct sum even on one core, and near-interactive with
  a few cores.
- The **Plummer cluster** (`10`) is spherical but has a dense core, and the **thin disk**
  (`11`) is nearly 2D — both pack many bodies into a few octree cells, so the near-field
  work climbs. They run correctly (energy is conserved) but are the FMM's harder cases
  and cost more per step. `10` is the heaviest of the set.

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
