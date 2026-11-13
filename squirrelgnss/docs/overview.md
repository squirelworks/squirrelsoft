# SquirrelGNSS overview

GNSS point positioning for land surveying. It covers:

- WGS84 geodesy: ECEF, geodetic, ENU, azimuth and elevation
- broadcast-orbit satellite positions and clocks
- weighted pseudorange least squares with an elevation mask
- dilution of precision, and fault detection and exclusion
- epoch tracking, an epoch stream with occupation averaging and velocity,
  and stakeout ENU deltas with go/cut/fill guidance
- RINEX 2.11 and 3.x GPS navigation and observation input (pseudorange,
  L1 carrier, SNR), and a RINEX 3 writer
- code DGNSS and static single-frequency RTK from double differences, with
  integer ambiguity fixing by rounding and a ratio test

Next come dual-frequency observations, a decorrelated ambiguity search,
kinematic RTK and serial receiver input.
