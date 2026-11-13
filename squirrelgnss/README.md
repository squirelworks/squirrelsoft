# SquirrelGNSS

GPS-style point positioning and land-survey helpers. Everything is original
geodesy and positioning math written from the published GPS interface
formulas. None of it comes from vendor receiver firmware.

## What it does

- **WGS84 geodesy:** geodetic and ECEF both ways, exact to well under a
  millimetre from below the ground out past the satellites, poles included.
  Also local east/north/up both ways, and azimuth and elevation.
- **Orbits:** GPS broadcast ephemerides with Kepler's equation by Newton's
  method, the six harmonic corrections, node and inclination rates, and week
  rollover. Satellite clocks include the polynomial, the relativistic
  eccentricity term and the group delay.
- **Point positioning:** pseudorange least squares for position and receiver
  clock. It starts from anywhere, even the centre of the Earth, and accounts
  for the satellite clock and the Earth's rotation during the signal's
  flight. The elevation mask and sin²(elevation) weighting apply once the
  solution is near the ground. It also reports:
  - GDOP, PDOP, HDOP, VDOP and TDOP in local axes
  - per-satellite residuals and their RMS
  - fault detection and exclusion: when the residual RMS is above the limit
    and at least two satellites are spare, it drops the one satellite whose
    removal explains the fault
- **RINEX input:** GPS navigation and observation files in RINEX 2.11 and
  3.x, so real receiver logs can be solved. Navigation records from other
  systems in mixed files are skipped, Fortran `D` exponents are read, and a
  10-bit week is mended. For each epoch, the nearest healthy ephemeris
  within 4 hours is picked. Observations use C1C (then C1, C1W, P1, C1P) for
  the pseudorange and S1C or S1 for the SNR. Event blocks are skipped.
  Errors give the line number. A RINEX 3 writer makes synthetic files to
  match.
- **Track:** an epoch track seeded from the last fix, and a `.sqobs` log.
- **Survey:** stakeout deltas in east/north/up and baseline lengths.
- **Synthetic sky:** 30 satellites in 6 planes with clock offsets, and
  pseudoranges for any site, clock bias, mask and seeded noise. It's for
  tests and demos.

## Build

```bash
make
make test
./build/squirrelgnss help
```

## Examples

```bash
./build/squirrelgnss llh2ecef 35.681236,139.767125,40
./build/squirrelgnss stakeout 35.0,139.0,50 35.001,139.001,51
./build/squirrelgnss sky                       # satellites in view: PRN, azimuth, elevation
./build/squirrelgnss demo-track 10 --noise 1   # error in E/N/U per epoch, DOPs, residual RMS
./build/squirrelgnss demo-track 3 --fault 3:100 --out demo.sqobs   # PRN 3 is excluded
./build/squirrelgnss rinex-demo demo.26o demo.26n 20    # synthetic RINEX 3 obs + nav files
./build/squirrelgnss solve demo.26o demo.26n --truth 35.681236,139.767125,40
./build/squirrelgnss solve station.obs brdc.nav --mask 15   # a real receiver's RINEX 2.11 or 3.x
```

`solve` prints one line per epoch: GPS time, satellites used, latitude,
longitude, height, HDOP, VDOP, residual RMS, any excluded satellite, and the
E/N/U error when `--truth` is given. A broadcast navigation file for the day
is published by the IGS data centres.

## Tests

`make test` runs `tests/test_sqgnss.c` (with `tests/test_rinex.c`) and
`tests/cli_smoke.sh`. The unit
tests cover:

- fixed points of the ellipsoid, and 539 round trips (poles, the antimeridian,
  -5 km to 20,200 km)
- ENU both ways, and azimuth and elevation
- orbit radius bounds, one period in inertial space, and the week wrap
- each satellite clock term
- noiseless fixes at five sites around the world, eight times a day, to a
  millimetre, starting from the centre of the Earth, with the mask and DOP
  identities checked
- seeded noise
- a 100 m fault found and excluded, and what happens without exclusion
- masks of 0, 10 and 85 degrees, too few satellites, and the track
- RINEX: GPS time against the calendar (week 0, the 1999 rollover, today);
  RINEX 3 navigation and observation round trips solved to under a
  centimetre; RINEX 2.11 files with `D` exponents, three and seven
  observation types and more than 12 satellites in an epoch; mixed files;
  ephemeris selection; and the error cases (no header end, a cut-short
  record, a bad number, a navigation file opened as observations, no
  pseudorange type, an event block, a bad epoch line)
