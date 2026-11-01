# squirrelsoft roadmap

## Principles

1. Original implementations only — no proprietary CAD/GNSS/audio source.
2. Host-first C tools that can later port to Squirrel-OS/2 userland.
3. Small, testable cores before UI chrome.
4. Stable, documented file formats owned by this project (`.sqdgn`, `.sqdwg`,
   `.sqobs`).

## Near-term milestones

### SquirrelStation

- Element graph: line, arc, curve, mesh, solid placeholders
- Levels, cells, reference attachments
- Accu-style snaps and precise keyed input
- `.sqdgn` text interchange + binary sketch

### SquirrelCAD

- Classic command loop: LINE, CIRCLE, ARC, PLINE, LAYER, BLOCK, INSERT
- Model space / paper space sketch
- `.sqdwg` interchange
- DXF-like export of *our* entities (not Autodesk DXF fidelity claims)

### SquirrelGNSS

- WGS84 / ECEF / geodetic conversions
- Broadcast-ephemeris style Kepler propagator (simplified)
- Pseudorange least-squares PVT
- Continuous epoch stream + survey stakeout helpers
- Dual-frequency / RTK double-difference hooks (math first)

### SquirrelSound

- Radix-2 FFT, windows, periodogram, Welch PSD, STFT
- Pitch (autocorr + spectral peak), THD, SNR estimates
- WAV I/O and CLI reports / SVG spectrum plots
- Expand to realtime capture when OS audio exists
