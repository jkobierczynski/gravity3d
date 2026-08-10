# Gravity3D

A 3D N-body gravity simulator/visualiser in C++ (OpenGL 3.3). Loads a scene of
objects from a file and shows the motion with selectable projections, stereoscopic
3D modes, VR output, adjustable speed, pause/restart, and configurable motion trails.

---

## Features at a glance

| Requirement | How it's covered |
|---|---|
| Load objects from a file (name, mass, position, velocity, colour) | `data/*.csv`, see **Scene file format** |
| Single-camera projections | Keys `1`–`4`: Front · Isometric · Axonometric · Oblique (cabinet) |
| "American projection" (third-angle) | `F7` **multiview**; key `5` toggles third-angle (American) / first-angle (ISO/European) — see **Projection standards** |
| Anaglyph 3D | `F2` — red/cyan |
| Straight & cross-eye stereo | `F3` (parallel) · `F4` (cross-eye) |
| VR headset | `F5` side-by-side (phone/Cardboard) · `F6` OpenXR (real PC-VR headset) |
| Simulation speed | `+` / `-` |
| Pause / restart | `Space` / `R` |
| Trail stays vs. moves with object | `T` cycles Off → Follow (moves along) → Stay (persists) |

---

## Building on Windows

Prerequisites: **Visual Studio 2019/2022** (Desktop C++ workload) and **CMake ≥ 3.16**
(the VS installer includes it). GLFW and GLM are downloaded automatically by CMake;
the OpenGL loader (GLAD) is already vendored in the repo, so there is nothing extra
to install for the core build.

```bat
cd gravity3d
cmake -S . -B build
cmake --build build --config Release
build\Release\gravity3d.exe
```

The sample scene is copied next to the executable, so it runs with no arguments.
To load your own file: `gravity3d.exe path\to\your_scene.csv`

### Optional: real VR (OpenXR)

```bat
cmake -S . -B build -DENABLE_OPENXR=ON
cmake --build build --config Release
```

This pulls the Khronos OpenXR-SDK and enables `F6`. You need an OpenXR runtime
running (SteamVR, Oculus/Meta, WMR, …) and a headset. See the caveat at the bottom.

> Builds on Linux too (needs `cmake`, `libgl1-mesa-dev`, `xorg-dev`); macOS is
> limited to OpenGL 3.3 / 4.1 and has no OpenXR path.

---

## Controls

```
Projection    1 Front   2 Isometric   3 Axonometric   4 Oblique
View mode     F1 Mono   F2 Anaglyph   F3 Stereo-Parallel   F4 Stereo-Cross
              F7 Multiview (engineering 3-view)
              F5 VR side-by-side (fullscreen it)   F6 VR OpenXR
Multiview     5    toggle Third-angle (American) / First-angle (ISO/European)
Perspective   P    toggle ortho/perspective for Front/Iso/Axono
Trails        T    cycle Off / Follow / Stay
Speed         + / -
Pause         Space          Restart   R
Stereo tune   [ ] eye separation      , . convergence distance
Camera        drag = orbit    scroll = zoom   (zoom also scales the multiview)
Fullscreen    F11            Quit  Esc
```

Current state is always shown in the window title bar.

---

## Scene file format

Plain text. Lines starting with `#` are comments; blank lines are ignored.
Lines starting with `@` are configuration directives. Everything else is one body,
as comma-separated fields:

```
name, mass, px, py, pz, vx, vy, vz, r, g, b [, radius]
```

- Positions/velocities are in arbitrary scene units.
- Colours may be `0..1` floats **or** `0..255` (auto-detected).
- `radius` is optional; if omitted the drawn size is derived from mass.

Config directives (all optional, with defaults):

```
@G=1.0             gravitational constant
@softening=0.05    Plummer softening length (avoids singular close encounters)
@dt=0.004          fixed internal physics timestep
@timescale=1.0     starting speed multiplier
@zeroMomentum=1    subtract net drift so the system stays centred
```

Example (`data/sample_system.csv`): a heavy central star with four orbiters, one on
an inclined elliptical path. For a roughly circular orbit at radius *r* around a
dominant mass *M*, set speed = `sqrt(G*M/r)` perpendicular to the radius.

Physics: velocity-Verlet integration, O(n²) pairwise gravity with Plummer softening.
Good for tens of bodies; not a Barnes–Hut tree, so it's not meant for thousands.

---

## Notes & interpretations

### Projection standards (the "American projection")

In technical drawing, "American projection" means **third-angle** orthographic
projection, and the ISO/European standard is **first-angle**. Both show the *same*
orthographic views (front, top, side) — the difference is purely *where each view is
placed* relative to the front view. Third-angle puts each view where the eye naturally
expects it (the object is behind a pane of glass you look through); first-angle flips
top↔bottom and left↔right (the object casts its image onto a plane behind it).

Press `F7` for the **multiview** mode and `5` to switch standard. The layout:

```
   THIRD-ANGLE (American)          FIRST-ANGLE (ISO / European)
  +-----------+-----------+       +-----------+-----------+
  |   TOP     |   (iso)   |       |  RIGHT    |   FRONT   |
  +-----------+-----------+       +-----------+-----------+
  |  FRONT    |  RIGHT    |       |  (iso)    |   TOP     |
  +-----------+-----------+       +-----------+-----------+
   top ABOVE front,                top BELOW front,
   right-view to the RIGHT         right-view to the LEFT
```

The fourth cell shows an isometric for reference (not part of the standard). All four
panels share one orthographic scale, so sizes are directly comparable, and scroll-zoom
scales them together. The single-camera presets remain available too: **Axonometric**
(`3`) and **cabinet Oblique** (`4`), alongside Front (`1`) and Isometric (`2`); mouse
free-orbit reaches any exact angle. If you'd prefer a different oblique (cavalier vs
cabinet) or a specific axonometric ratio for those presets, it's a one-line change in
`Camera.cpp`.

- **Stereo.** Anaglyph and side-by-side use correct off-axis (parallel-axis) frusta
  with tunable eye separation (`[` `]`) and convergence (`,` `.`). Straight vs
  cross-eye is just which image goes to which half. If depth looks inverted for you,
  nudge eye separation or swap `F3`/`F4`.

- **VR — honest status.** `F5` (side-by-side) works today with a phone-in-holder
  viewer or any display that accepts SBS. `F6` uses OpenXR for a real PC-VR headset;
  the backend is complete (instance → session → per-eye swapchains → frame loop →
  projection layer) and compiles against the official SDK, but it hasn't been run on
  a headset in this project, so expect to test and possibly tweak per-runtime details.
  It's viewer-only (head tracking, no controller input).
