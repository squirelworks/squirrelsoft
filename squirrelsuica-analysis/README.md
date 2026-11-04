# SquirrelSuicaAnalysis

A debug and analysis FeliCa reader that shows what a card actually answers, so
SquirrelSuica can be fixed from real data. It is read-only: it only sends plain
FeliCa commands and never uses keys. SquirrelSuica itself stays the consumer app.

| | |
|---|---|
| Android | `android/` · applicationId `works.squirrel.suicaanalysis` · version `0.0.1` (1) |
| Desktop | `suica_analyze.py` (nfcpy + PaSoRi RC-S380/S300) |
| Dump format | `schema/felica-dump.schema.json` (`squirrel-felica-dump/1`), the same for both |

## What a scan does

1. Polling for system codes `FFFF`, `0003` and `FE00`, with request code 01 (system code) and 02 (communication performance).
2. Request System Code (0x0C). For each system, it polls that system for its IDm, sends Request Response (0x04), and runs Search Service Code (0x0A) until the end marker, recording every area and service.
3. Request Service (0x02) for every service's key version.
4. For every service readable without a key, it runs Read Without Encryption (0x06) from block 0 at chunk sizes 1, 2, 4, 8, 12 and 15. For each chunk size it records the blocks read, where reading stopped, SF1/SF2, the number of commands and the average latency.
5. The PMm is decoded into ROM/IC type and the maximum response time per command class.

The Android app shows a Summary tab, a History tab and a raw Command log tab. History parses the blocks of the Suica (090f), nanaco (564f) and Edy (170f) history services, plus the Suica, nanaco, Edy and WAON balances, with the shared C core in `libs/sqfelica` through JNI (`app/src/main/cpp`). Rail entries show raw line/station codes rather than names. You can share the JSON or save it with the system file picker. "Redact IDm", which is on by default, replaces every IDm, including the ones inside raw frames.

## Desktop

```
python3 suica_analyze.py --redact -o dump.json   # live, first USB reader
python3 suica_analyze.py --demo                  # built-in fake card
make test
```

## Build (Android)

```
cd android && ./gradlew :app:assembleDebug :app:testDebugUnitTest
```

The Android build compiles `libs/sqfelica` with the NDK (CMake), so it needs the NDK that Android Studio or the SDK manager installs.
