# SquirrelGNSS overview

GNSS point positioning for land surveying. It covers:

- WGS84 geodesy: ECEF, geodetic, ENU, azimuth and elevation
- broadcast-orbit satellite positions and clocks
- weighted pseudorange least squares with an elevation mask
- dilution of precision, and fault detection and exclusion
- epoch tracking, an epoch stream with occupation averaging and velocity,
  and stakeout ENU deltas with go/cut/fill guidance
- RINEX 2.11 and 3.x GPS navigation and observation input (pseudorange,
  L1, L2 and L5 code and carrier, SNR, loss of lock), and a RINEX 3 writer
- the ionosphere-free point position from L1/L2 or L1/L5 code
- a receiver live over a serial port or from a recording: NMEA fixes and
  satellites in view, and u-blox raw measurements, broadcast subframes
  (decoded to ephemerides) and solutions, solved here beside the
  receiver's own fix and logged as RINEX
- code DGNSS, and static and kinematic RTK from double differences on one
  or two frequencies: wide lanes from the Melbourne-Wubbena combination,
  then integer least squares (LAMBDA-style decorrelation and search) with
  a ratio test; the kinematic filter keeps the ambiguities in information
  form, eliminating each epoch's rover, and handles cycle slips
- NTRIP client (`sqntrip`): builds caster GET requests (NTRIP 1/2 headers,
  Basic auth), parses ICY/HTTP replies and sourcetables, and opens a live
  TCP session through injectable `SqntIo` (memory bytes or portable
  sockets). Unit tests use synthetic replies and `127.0.0.1` loopback only.
  TLS is requested via a flag but unsupported without an in-tree stack.
  Correction bodies stay opaque (RTCM decode is next).

Android (`android/`): JNI to this core with a synthetic packed-observation
feed for CI, and an optional phone `GnssMeasurement` / `Location` listener.
NTRIP corrections are not wired into the app UI yet; the C transport is ready.

Not yet: other constellations (Galileo, BeiDou, GLONASS), CNAV inter-signal
corrections for L5, RTK on L5, NTRIP TLS, RTCM decode, and app-side NTRIP.
The receiver path has been checked on synthetic streams and through a
pseudo-terminal, not yet on a receiver.
