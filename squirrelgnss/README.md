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
  the pseudorange, L1C (then L1, L1W, L1P) for the carrier phase, and S1C
  or S1 for the SNR. Event blocks are skipped.
  Errors give the line number. A RINEX 3 writer makes synthetic files to
  match.
- **Track:** an epoch track seeded from the last fix, and a `.sqobs` log.
- **Epoch stream:** fixes epoch after epoch, each started from the last.
  It keeps a running mean and spread of an occupation (east/north/up about
  the first fix), and speed and course from the last two fixes.
- **Survey:** stakeout deltas in east/north/up and baseline lengths, and
  stakeout guidance such as `go 1.230 m N, 0.450 m W (1.310 m at 339.9 deg);
  fill 0.100 m`, or `on point` within a tolerance.
- **Differential (sqrtk):** a base at a known place and a rover seeing the
  same satellites.
  - DGNSS: code double differences in one epoch, to the code noise.
  - Static RTK: code and L1 carrier double differences over a session,
    solving for the rover and one float ambiguity per satellite pair, then
    rounding to whole cycles. The fix is kept only when a ratio test passes:
    each ambiguity moved one cycle toward its float value must fit at least
    3 times worse. This is single frequency, and the fix is rounding plus
    the ratio test, not LAMBDA, so a short session can stay float.
- **Synthetic sky:** 30 satellites in 6 planes with clock offsets, and
  pseudoranges and L1 carrier phase (with whole-cycle ambiguities that
  differ by site and satellite) for any site, clock bias, mask and seeded
  noise. It's for tests and demos.

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
./build/squirrelgnss solve station.obs brdc.nav --average   # plus the occupation mean and spread
./build/squirrelgnss guide 35.0,139.0,50 35.00001,139.0,50.1  # go 1.109 m N (...); fill 0.100 m
./build/squirrelgnss rtk-demo --baseline 300,-150,4          # synthetic static RTK session
./build/squirrelgnss rtk base.obs rover.obs brdc.nav --base-at 35.681236,139.767125,40
```

`rtk` pairs the base and rover epochs by time and prints `fixed` or
`float`, the ratio, the rover position and the float ambiguities. Both
receivers need L1 carrier phase in their RINEX files.

`solve` prints one line per epoch: GPS time, satellites used, latitude,
longitude, height, HDOP, VDOP, residual RMS, any excluded satellite, and the
E/N/U error when `--truth` is given. A broadcast navigation file for the day
is published by the IGS data centres.

## Tests

`make test` runs `tests/test_sqgnss.c` (with `tests/test_rinex.c` and
`tests/test_rtk.c`) and
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
  pseudorange type, an event block, a bad epoch line), and the L1 carrier
  round trip
- the stream: a 60-epoch occupation's mean and spread, exact noiseless
  averaging, speed and course of a moving receiver, and a lost fix
- guidance text: both legs, on point, on grade, due north, a short buffer
- DGNSS exact without noise whatever the two receiver clocks, and under
  2.5 m with 0.3 m code noise
- static RTK over 20 epochs: every integer ambiguity right, within 1 cm
  horizontally and 2 cm vertically with 3 mm phase noise, exact without
  noise, float when the ratio can't pass, and no carrier
