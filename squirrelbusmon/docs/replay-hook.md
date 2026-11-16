# Replay hook — Flight Lab / HIL

`squirrelbusmon replay` emits a line-oriented **REPLAY v1** stream for Soft
consumers (Steve’s Flight Lab, fighter HIL bench UI). Soft analyzer → Soft
ingest; physical bench definitions stay in ISM.

## Produce

```bash
make -C squirrelbusmon
./build/squirrelbusmon replay --capture fixtures/captures/sample-bus.ismcap \
  --out /tmp/bus.replay
# optional filters: --node MC --msgid 0x0101 --topic fcc.surface_cmd --bus ISM-Bus-A
```

## Stream shape

```
REPLAY v1
# Soft squirrelbusmon → Flight Lab / HIL ingest
inject t_ms=<ms> bus=<id> node=<name> msgid=0xNNNN topic=<name> display=<decoded or OPAQUE…>
```

- One `inject` line per frame, in capture order (after optional filter).
- `display=` is the analyzer’s safe decode string (opaque for SMS/stores).
- Consumers must ignore unknown keys on future `inject` lines.

## Flight Lab / HIL contract

1. Open the replay file (or pipe stdout) as text.
2. Require the first non-comment line to be `REPLAY v1`.
3. For each `inject` line, schedule a bus inject at `t_ms` relative to stream
   start (or wall-clock offset chosen by the operator).
4. Use `topic` / `msgid` / `bus` / `node` for routing; treat `display` as
   operator-visible only — do not re-parse weapon payloads from it.
5. HIL UI may log the same lines alongside channel health; Flight Lab may map
   non-opaque topics onto scenario / FCS co-check inputs when those hooks land.

## Out of scope (v0)

- Live USB / bay-debug capture (file import only).
- Binary wire framing (Soft sample text only until ISM freezes a capture ICD).
- Driving real release / arm discretes from replay (display/integration only).
