# NTRIP live transport (SquirrelGNSS)

Lane `-2eaf`. Builds on the buffer-fed `sqntrip` skeleton (#147).

## What this adds

- **`SqntIo`**: injectable read/write/close so unit tests and hosts share one
  client path.
- **`SqntMemIo`**: in-memory caster for offline tests (synthetic reply bytes).
- **`SqntClient`**: builds a GET via `sqnt_build_request`, writes it, pumps
  bytes into `SqntParser`.
- **`SqntTcp`**: portable TCP connect (POSIX sockets / Winsock), plus
  `listen_loopback` / `accept` for local synthetic casters on `127.0.0.1`.
- **TLS**: `use_tls` is accepted and fails cleanly today
  (`sqnt_tls_supported() == 0`). CONTINUANCE asks not to bolt on a third-party
  TLS stack; plain NTRIP over TCP remains the supported live path.

## Spec notes

Public NTRIP 1.0 (RTCM Paper 200-2004/SC104-STD) and NTRIP 2.0 HTTP headers.
Correction bodies stay opaque (RTCM decode is a later step).

## CI / safety

- `tests/test_ntrip.c` uses memory I/O and `127.0.0.1` loopback only.
- No remote caster, no credentials in the tree.
- CLI: `ntrip-synth` is CI-safe; `ntrip HOST:PORT …` is for operators and is
  not invoked from `cli_smoke.sh` against a real host.
