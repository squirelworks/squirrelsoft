# SquirrelGNSS overview

GNSS point positioning for land surveying. It covers:

- WGS84 geodesy: ECEF, geodetic, ENU, azimuth and elevation
- broadcast-orbit satellite positions and clocks
- weighted pseudorange least squares with an elevation mask
- dilution of precision, and fault detection and exclusion
- epoch tracking and stakeout ENU deltas
- RINEX 2.11 and 3.x GPS navigation and observation input, and a RINEX 3 writer

Next come carrier phase and double differences for RTK, and serial
receiver input.
