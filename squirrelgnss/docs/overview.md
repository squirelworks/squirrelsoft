# SquirrelGNSS overview

GNSS point positioning for land surveying. It covers:

- WGS84 geodesy: ECEF, geodetic, ENU, azimuth and elevation
- broadcast-orbit satellite positions and clocks
- weighted pseudorange least squares with an elevation mask
- dilution of precision, and fault detection and exclusion
- epoch tracking, an epoch stream with occupation averaging and velocity,
  and stakeout ENU deltas with go/cut/fill guidance
- RINEX 2.11 and 3.x GPS navigation and observation input (pseudorange,
  L1 and L2 carrier, SNR, loss of lock), and a RINEX 3 writer
- code DGNSS, and static and kinematic RTK from double differences on one
  or two frequencies: wide lanes from the Melbourne-Wubbena combination,
  then integer least squares (LAMBDA-style decorrelation and search) with
  a ratio test; the kinematic filter keeps the ambiguities in information
  form, eliminating each epoch's rover, and handles cycle slips

Next comes serial receiver input.
