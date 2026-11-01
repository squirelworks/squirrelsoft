# squirrelsoft

Application suite for Squirrel-OS/2 and host development.

All code here is project-native. Product names below are original Squirrelsoft
titles for clean-room reimplementations inspired by well-known CAD, GNSS, and
audio-analysis workflows — not ports of proprietary binaries or copyrighted
libraries.

| Directory | Program | Inspired domain |
|-----------|---------|-----------------|
| `squirrelstation/` | **SquirrelStation** | MicroStation-class 3D design / DGN-style CAD |
| `squirrelcad/` | **SquirrelCAD** | AutoCAD-class 2D/3D drafting |
| `squirrelgnss/` | **SquirrelGNSS** | Continuous satellite / GPS land surveying |
| `squirrelsound/` | **SquirrelSound** | Numerical spectral sound analyzer |

## Build

Requires a C11 toolchain (`gcc` or `clang`) and `make`.

```bash
make            # build every program
make test       # run smoke / unit tests
make clean
```

Or build one tree:

```bash
make -C squirrelsound
make -C squirrelsound test
```

## Layout

```
squirrelsoft/
├── README.md
├── ROADMAP.md
├── Makefile
├── squirrelstation/   # levels, cells, 3D elements, .sqdgn
├── squirrelcad/       # layers, blocks, command CAD, .sqdwg
├── squirrelgnss/      # WGS84, PVT, continuous track, survey
└── squirrelsound/     # FFT / PSD / STFT analyzer (original numerics)
```

## License posture

Original Squirrelsoft source. Do not drop proprietary MicroStation, AutoCAD,
vendor GNSS firmware, or Numerical Recipes book code into this tree.
SquirrelSound implements standard spectral methods from first principles.
