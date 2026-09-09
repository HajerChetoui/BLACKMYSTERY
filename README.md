# BLACKMYSTERY

**BLACKMYSTERY** is an educational astronomy application that lets you explore how different types of black holes can (and can't) actually be observed, using real physics and the genuine resolving limits of real astronomical instruments.

Rather than rendering a generic "artist's impression," BlackMystery computes the true apparent angular size of a black hole's shadow for the mass, distance, and instrument you choose, and compares that against the real angular resolution of that instrument. If the object is too small or too far away to resolve, you don't get a fabricated close-up — you get exactly what a real telescope would return: an unresolved point of light.

Built in C with [raylib](https://www.raylib.com/).

---

## Table of Contents

- [Overview](#overview)
- [How It Works](#how-it-works)
  - [1. Choose a Black Hole](#1-choose-a-black-hole)
  - [2. Choose Distance & Instrument](#2-choose-distance--instrument)
  - [3. The Simulation](#3-the-simulation)
  - [4. Decoding the View](#4-decoding-the-view)
  - [5. Real Photo Comparison](#5-real-photo-comparison)
- [The Physics](#the-physics)
- [Building From Source](#building-from-source)
- [Project Structure](#project-structure)
- [Credits & Sources](#credits--sources)
- [License](#license)

---

## Overview

BlackMystery walks the user through the same decisions a real observational astronomer has to make: *which* black hole, from *how far away*, using *which instrument* — and then shows them, honestly, what comes back.

The core idea driving the whole app: **a black hole's apparent size is a real, computable number**, and whether it's visible at all depends entirely on whether that number clears the resolution threshold of the chosen instrument. No part of the "is it visible" decision is faked or scripted — it falls directly out of general relativity and the real specs of VLBI and X-ray telescopes.

## How It Works

### 1. Choose a Black Hole

Four categories are available, each backed by a real representative mass and the corresponding Schwarzschild radius:

| Type | Representative Mass | Example |
|---|---|---|
| Stellar-mass | ~21 M☉ | Cygnus X-1 |
| Intermediate-mass | ~5,000 M☉ | Candidate IMBH in Omega Centauri |
| Supermassive | ~4.3 million M☉ | Sagittarius A* (our galaxy's core) |
| Primordial | ~Earth-mass (hypothetical) | Horizon radius of only ~8.9 mm |

The app displays each type's mass, horizon radius, and a real-world example alongside the selection.

### 2. Choose Distance & Instrument

**Observation distance** — how far above the event horizon the observer is:
- 1 light-year away
- 1 AU away
- 1,000 km above the horizon
- 10 km above the horizon

**Observation method:**
- **Very Long Baseline Interferometry (VLBI)** — detects radio emission, targeting the black hole's shadow and surrounding radio-emitting material. This is the technique the Event Horizon Telescope uses to image M87* and Sagittarius A*. Angular resolution: ~25 microarcseconds.
- **High-resolution X-ray imaging** — detects X-rays from superheated material near the black hole, revealing the hot, energetic regions around it rather than the shadow itself. Angular resolution: ~0.5 arcsecond (500,000 microarcseconds).

### 3. The Simulation

Once mass, distance, and instrument are chosen, the app's physics engine computes the true apparent angular size of the black hole's shadow and checks it against the chosen instrument's resolution.

- **Resolved:** the object is large enough — the user sees a dark shadow ringed by a glowing, gravitationally-lensed accretion disk, styled according to the observation mode (VLBI or X-ray).
- **Unresolved:** the object is too small or too distant — instead of a fake close-up, the user sees a small pulsing point of light, exactly the honest result a real telescope would return.

No image is ever fabricated to look more detailed than the physics justifies.

### 4. Decoding the View

After viewing the simulation, the user can tap "Explain This View" for a plain-language breakdown of what they're looking at:

- The black hole type, its actual physical size, and the exact computed apparent size.
- If resolved: what each visible feature is — the event horizon shadow, the photon ring, and (in X-ray mode) the relativistic jets and the clumpy, flickering accretion disk.
- If unresolved: what would need to change (a different instrument, a closer distance, a more massive black hole) to actually see something.

### 5. Real Photo Comparison

This is the reality check: a "Compare to Real Photo" screen shows an actual, genuine published photograph for combinations where one exists — such as the 2022 Event Horizon Telescope image of Sagittarius A*, or a Chandra X-ray image of Cygnus X-1.

For every other combination, the app says so plainly instead of pretending:

- No intermediate-mass or primordial black hole has ever been directly imaged.
- Stellar-mass black holes have been detected through their gravitational effects on companion stars and surrounding matter, but no ordinary stellar-mass black hole has been resolved at event-horizon scale.

The goal of this screen is to let the user see exactly how much of the simulation lines up with something astronomers have actually observed — and to be transparent about the rest.

## The Physics

The apparent angular size of a Schwarzschild black hole's shadow is computed using the real general-relativistic result (Synge, 1966), not a naive Euclidean size/distance approximation:

```
sin²(ψ) = (27/4) · (Rs/r)² · (1 − Rs/r)
```

where `ψ` is the shadow's angular radius, `Rs = 2GM/c²` is the Schwarzschild radius, and `r` is the observer's radial distance from the black hole's center. This accounts for how strongly light bends near the photon sphere — the shadow looks noticeably larger than a naive calculation would suggest, especially at close range. Inside the photon sphere (`r ≤ 1.5·Rs`), the shadow is clamped to fill a full hemisphere of the sky, since the simple formula no longer applies there.

The resulting angular size (in microarcseconds) is compared directly against each instrument's real angular resolution to decide whether the object is resolved.

## Building From Source

### Option A — Visual Studio (Windows)

1. Open the project (`WindowsProject1.sln`) in Visual Studio.
2. All required image assets (background, icon, and the comparison photos) are embedded into the executable at compile time via `RCDATA` resources declared in `WindowsProject1.rc` — no external `assets` folder is needed at runtime.
3. To rebuild from scratch with your own images, place the PNGs in an `assets/` folder next to the `.rc` file (see the `RCDATA` entries in `WindowsProject1.rc` for exact filenames), then build.
4. Set the configuration to **Release**, build, and the resulting `.exe` is fully self-contained (assuming raylib is statically linked).

### Option B — GCC / MSYS2 / w64devkit

```bash
gcc main.c -o blackmystery.exe -O2 -Wall \
    -I<raylib_include> -L<raylib_lib> -lraylib -lopengl32 -lgdi32 -lwinmm
```

## Project Structure

```
WindowsProject1/
├── WindowsProject1.c      # Application source (screens, physics, rendering)
├── WindowsProject1.rc     # Resource script — embeds icons & PNG assets into the .exe
├── Resource.h              # Resource ID definitions
├── WindowsProject1.ico
├── small.ico
└── assets/                 # Source images (build-time only, not shipped)
    ├── background.png
    ├── icon.png
    ├── sgra_eht.png
    ├── sgra_chandra.png
    └── cygx1_chandra.png
```

## Credits & Sources

- Built with [raylib](https://www.raylib.com/), a simple and easy-to-use library for videogame and graphics programming.
- Comparison photograph credits:
  - Sagittarius A* (radio/VLBI) — ESO/EHT Collaboration
  - Sagittarius A* (X-ray context) — NASA/CXC (Chandra X-ray Observatory)
  - Cygnus X-1 (X-ray) — NASA/CXC (Chandra X-ray Observatory)
- Created by Hajer Chetoui.

## License

*(Add your chosen license here — e.g. MIT, GPL-3.0 — or state "All rights reserved" if you don't want the code reused.)*
