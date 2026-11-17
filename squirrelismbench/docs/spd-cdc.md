# Soft↔ISM USB-CDC SPD framing (S8.4b″ / Soft #203)

Soft-owned line framing for Eric’s ISM RAM-module SPD dumps over USB-CDC.
Hardware SoT stays in ISM (`memory/`); Soft only parses and checks.

## Line format

```
ISM SPD: [addr=0x50] [len=N] <hh> <hh> … <hh>
```

- Prefix is exactly `ISM SPD:` (case-sensitive).
- Optional Soft keywords `addr=…` / `len=…` may appear before the bytes;
  Soft currently ignores them for the buffer fill (iface still defaults
  `spd_i2c_addr` to `0x50` from the ISM-MEM fixture).
- Remaining tokens are hex bytes (`0`–`ff`). Soft needs at least two.
- **Soft XOR contract (unchanged):** last byte equals the XOR of all prior
  bytes. Same check as fixture `interface.ismem` / `squirrelramtest`.

## Transcript

A Soft stub or live ACM session may include other lines (`ISM HELLO`,
`ISM BYE`, comments `# …`). Soft scans for the first `ISM SPD:` line.

## Soft path

| Path | Role |
|------|------|
| `fixtures/memory/cdc-spd-stub.sqspd` | CI live-stub transcript |
| `/dev/ttyACM*` / `/dev/ttyUSB*` | Live USB-CDC when Eric’s board is present |
| `memory-test --spd-cdc PATH` | Apply framing onto the loaded ISM-MEM iface, then self-test |
| `squirrelramtest show --spd-cdc PATH` | Companion UI uses the same framing |

CI stays on the `.sqspd` stub. Opening a missing ACM node fails with a clear
error; Soft does not vendor USB stacks beyond POSIX `fopen`/`fgets`.
