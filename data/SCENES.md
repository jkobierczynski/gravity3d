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
