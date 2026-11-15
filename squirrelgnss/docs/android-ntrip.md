# Android NTRIP caster wiring (SquirrelGNSS)

Lane `-2eaf`. Wires the portable `sqntrip` + `sqrtcm` cores into the
SquirrelGNSS Android app so a mount can be dialled and MSM corrections
framed/parsed on the phone. Builds on live NTRIP transport (#150), the RTCM
skeleton (#151–#153 / #157), and MSM observation fields (#160).

## How Android reaches the portable cores

```
MainActivity / NtripCasterFeed.kt
        │
        ▼
   SqGnss.kt  (Kotlin API)
        │  System.loadLibrary("sqgnss_jni")
        ▼
   sqgnss_jni.c  (JNI)
        │
        ├── sqntrip.c / sqntrip_transport.c   GET + ICY/HTTP parse + SqntIo TCP/memio
        └── sqrtcm.c                          framer_feed → parse_msm (MSM1–7)
```

`CMakeLists.txt` links the same C sources the desktop CLI uses. Host smoke
(`make test-jni`) builds that bridge for the development machine and drives
identical JNI symbols from `android/jni-smoke/`.

## API surface

| Path | JNI | When |
|------|-----|------|
| Memio synth | `nativeNtripSynthMsm` | CI / UI “NTRIP synth” — ICY 200 + synthetic MSM4 |
| Loopback TCP | `nativeNtripLoopbackMsm` | Host/device smoke on `127.0.0.1` only |
| Feed bytes | `nativeNtripFeedResponse` | Inject a full caster reply (synthetic) |
| Live dial | `nativeNtripDial` | Operator UI only — never from CI |

Packed summary (`NTRIP_OUT_LEN` = 12 ints): status, kind, body_len, n_frames,
msg_type, station_id, epoch_ms, msm_type, n_sat, n_sig, n_cell, category.

## CI / safety

- Unit tests and host JNI smoke use memio + `127.0.0.1` only.
- No remote caster, no credentials, no real correction dumps in the tree.
- `INTERNET` is declared for operator dial; Gradle CI does not dial.
- Synthetic MSM4 constants match `tests/test_rtcm.c` (`fill_msm4_demo`).

## Spec notes

NTRIP 1.0 / 2.0 headers as in `docs/ntrip-transport.md`. RTCM 10403.x framing
and MSM1–7 as in `docs/rtcm-decode.md`. Applying MSM cells into the PVT /
RTK solver remains a later step; this card stops at framed MSM decode on
Android.
