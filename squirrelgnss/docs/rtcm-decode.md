# RTCM 3 decode skeleton (SquirrelGNSS)

Lane `-2eaf`. Builds on live NTRIP transport (`sqntrip`) so a stream body
can be framed and header-parsed offline.

## What this adds

- **`sqrtcm_crc24q`**: CRC-24Q over preamble + length + payload.
- **`sqrtcm_frame` / pack helpers**: build synthetic frames for tests (MSM
  common header or legacy obs/station 24-bit header).
- **`sqrtcm_parse_header`**: DF002 message number, DF003 station ID; for MSM,
  GPS-style 30-bit epoch and the multiple-message bit.
- **`SqrtcmFramer`**: byte-fed sync on `0xD3`, 10-bit length (reserved bits
  must be zero), CRC check, resync on junk / bad CRC.
- **NTRIP glue**: after `sqnt_client_drain_body`, call
  `sqrtcm_framer_feed` on `cli.parser.body` (see `ntrip-synth` and
  `tests/test_rtcm.c`).

Satellite/signal masks and observation fields are intentionally out of scope
for this skeleton.

## Spec notes

Public RTCM 10403.x transport: preamble `0xD3`, 6 reserved + 10-bit length,
message body, CRC-24Q (polynomial `0x1864CFB`). Message numbers 1001–1004 /
1009–1012 (legacy obs), 1005–1006 (station ARP) and 1071–1127 (MSM) are the
v1 header subset.

## CI / safety

- `tests/test_rtcm.c` builds frames in memory only.
- No remote caster, no real correction dumps, no credentials.
- CLI: `rtcm-synth` and `ntrip-synth` (now emits a CRC-valid MSM7) are
  CI-safe; live `ntrip HOST:PORT` is operator-only.
