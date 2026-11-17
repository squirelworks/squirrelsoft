# Soft host PSRAM march path (S8.4b″ / Soft #204)

Soft-driven march / walking / checker / Park–Miller LCG patterns against a
Soft live bank. Hardware SoT stays in ISM; Soft does not design modules.

## Soft host API

| Call | Role |
|------|------|
| `ib_memory_live_open` | Soft bank endpoint (`stub`, `.sqmem`, or future ACM) |
| `ib_memory_live_write` / `read` | Soft Soft-driven Soft bank access |
| `ib_memory_live_march` | Soft march through Soft write/read |
| `memory-test --live-march ENDPOINT` | CLI |

Fixture self-test (`ib_memory_selftest`) stays local Soft malloc and is
unchanged. Soft #204 adds the Soft host Soft-driven path.

## Endpoints

| Endpoint | CI / lab |
|----------|----------|
| `stub` | Soft in-memory Soft bank (default Soft CI) |
| `fixtures/memory/live-stub.sqmem` | Soft live-stub descriptor (`size:`) |
| `/dev/ttyACM*` | **Refused** until Eric freezes Soft/host Soft MEM CDC |

## Soft-proposed Soft MEM CDC framing (future)

When Eric freezes Soft/host SoT, Soft expects Soft line framing similar to
Soft #203 SPD:

```
ISM MEM WRITE off=<hex> <hh>+
ISM MEM READ off=<hex> len=<n>
ISM MEM DATA <hh>+
ISM MEM OK
```

Soft #212 covers ACM soak for SPD; Soft MEM CDC soak can track the same
hardware when available. CI remains Soft stub / `.sqmem` only.
