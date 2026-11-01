# SquirrelGNSS

Continuous satellite / GPS positioning for land surveying.

Original geodesy and PVT math — not a vendor receiver firmware dump. Models
WGS84, simplified broadcast Kepler orbits, pseudorange least-squares solutions,
epoch streaming, and survey stakeout helpers.

## Features (v0)

- WGS84 geodetic ↔ ECEF
- Satellite position from Keplerian elements (GPS-style)
- Pseudorange observation + iterative least-squares PVT (x,y,z, clock)
- Continuous epoch tracker with dilution-of-precision (PDOP)
- Survey: baseline length, stakeout delta ENU, simple traverse close
- `.sqobs` observation log format

## Build

```bash
make
make test
./build/squirrelgnss help
```

## Example

```bash
./build/squirrelgnss demo-track 10
./build/squirrelgnss stakeout 35.0,139.0,50 35.001,139.001,51
```
