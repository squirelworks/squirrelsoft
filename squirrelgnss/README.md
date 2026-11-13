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
```

## Tests

`make test` runs `tests/test_sqgnss.c` and `tests/cli_smoke.sh`. The unit
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
