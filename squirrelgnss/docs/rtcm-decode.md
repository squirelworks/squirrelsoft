# RTCM 3 decode (SquirrelGNSS)

Lane `-2eaf`. Builds on live NTRIP transport (`sqntrip`) so a stream body
can be framed and MSM-decoded offline.

## What this adds

- **`sqrtcm_crc24q`**: CRC-24Q over preamble + length + payload.
- **`sqrtcm_frame` / pack helpers**: build synthetic frames for tests (MSM
  common header, full MSM1–7 payloads, or legacy obs/station 24-bit header).
- **`sqrtcm_parse_header`**: DF002 message number, DF003 station ID; for MSM,
  GPS-style 30-bit epoch and the multiple-message bit.
- **`sqrtcm_pack_msm` / `sqrtcm_parse_msm`**: full MSM observation decode —
  DF394–DF396 satellite / signal / cell masks, DF397–DF399 satellite rough
  ranges (and rates for MSM5/7), and per-cell DF400–DF408 / DF420 fields for
  MSM1–MSM7. Meter helpers combine rough + fine ranging units.
- **`SqrtcmFramer`**: byte-fed sync on `0xD3`, 10-bit length (reserved bits
  must be zero), CRC check; after bad CRC / rejected length, re-queues from
  `buf[1]` into a sized head/tail input ring (`SQRTCM_INQ_MAX`) so every good
  frame in that tail is delivered (not only the first). If a prepend cannot
  fit, the whole rejected tail is dropped and `rescan_drop` counts those
  bytes. `sqrtcm_framer_feed` copies each payload into `SqrtcmMsg.payload_copy`;
  `feed(..., 0)` drains frames left queued by the byte API.
- **`SqrtcmMsg` copy safety**: `payload_owned` + `sqrtcm_msg_payload()` /
  `sqrtcm_msg_fix_payload()` / `sqrtcm_msg_assign()` so a plain struct copy
  does not leave `.payload` pointing at another message's buffer.
- **NTRIP glue**: after `sqnt_client_drain_body`, call
  `sqrtcm_framer_feed` on `cli.parser.body` (see `ntrip-synth` and
  `tests/test_rtcm.c`).

## Spec notes (public RTCM 10403.x)

Transport: preamble `0xD3`, 6 reserved + 10-bit length, message body,
CRC-24Q (polynomial `0x1864CFB`).

MSM header (DF002–DF418, 73 bits) and observation layout follow
**RTCM 10403.x §3.5.12–3.5.13** (Multiple Signal Messages):

| Block | Data fields |
|-------|-------------|
| Header | DF002, DF003, DF004 epoch, DF393 multi, DF409 IODS, reserved, DF411–DF412, DF417–DF418 |
| Masks | DF394 sat (64), DF395 signal (32), DF396 cell (`Nsat×Nsig`, ≤64 cells) |
| Satellite | DF397 + DF398 (MSM1,3–7); DF399 rough PhaseRangeRate (MSM5,7) |
| Cell | MSM1: DF400; MSM2: DF401+DF402+DF420; MSM3: DF400+DF401+DF402+DF420; MSM4: DF405+DF406+DF402+DF420+DF403; MSM5: MSM4+DF404; MSM6: extended DF405/406/407/420/408; MSM7: MSM6+DF404 |

Message numbers 1001–1004 / 1009–1012 (legacy obs), 1005–1006 (station ARP)
and 1071–1127 (MSM) are the v1 category subset. MSM subtype is
`msg_type % 10` (1–7).

GLONASS MSM (1081–1087): the 30-bit epoch is day-of-week (3) + ms-of-day (27),
not GPS ms-of-week. The library still exposes it as `epoch_ms`; split when a
GLONASS-aware consumer needs day/ms separately.

## Still open

- Android NTRIP caster wiring in the SquirrelGNSS UI (follow-up PR).

## CI / safety

- `tests/test_rtcm.c` builds frames in memory only (header + MSM4/MSM7 obs
  round-trips, invalid sentinels, framer + NTRIP body glue).
- No remote caster, no real correction dumps, no credentials.
- CLI: `rtcm-synth` (MSM4 obs + ARP) and `ntrip-synth` are CI-safe; live
  `ntrip HOST:PORT` is operator-only.
