# Soft HID fixtures (S8.2)

Soft-owned **SQHID1** mock USB HID transcripts wrapping boot-protocol
keyboard (8-byte) and mouse (4-byte) reports.

- `mock-hid-session.sqhid` — synthetic Soft session (not a live dump).
- Live `/dev/hidraw*` deferred until Eric freezes a Soft/host path.

Refresh when ISM publishes machine-readable HID capture notes.
