#!/usr/bin/env python3
"""SquirrelSuica desktop: end-to-end read through PC/SC, with no hardware.

A made-up Suica card sits behind a virtual contactless reader: this script
connects to the vpcd driver of vsmartcard (pcscd loads it from
/etc/reader.conf.d/vpcd) and answers the PC/SC Part 3 transparent exchange
APDUs the way a FeliCa reader such as the RC-S300 does. The desktop reader
then reads the card live (--reader) and the result must match reading the
same card from a dump file (--dump).

Needs pcscd running with the vpcd driver (Debian/Ubuntu: pcscd and
vsmartcard-vpcd). Exits 0 with a note when they are missing, unless
--require is given.

Usage: python3 -I desktop/tests/test_pcsc.py BUILD/squirrelsuica [--require]
"""
import json
import socket
import struct
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

EXE = sys.argv[1]
REQUIRE = "--require" in sys.argv
VPCD_PORT = 0x8C7B
# PC/SC Part 3 ATR for a contactless storage card: RID A0 00 00 03 06,
# standard 11 (FeliCa), card name 003B.
ATR = bytes.fromhex("3b8f8001804f0ca0000003061100" "3b0000000042")
STATIONS = str(Path(__file__).resolve().parent.parent.parent / "data" / "StationCode.csv")
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg, file=sys.stderr)


def skip(why):
    print(f"squirrelsuica PC/SC test: {why}", file=sys.stderr)
    raise SystemExit(1 if REQUIRE else 0)


# ------------------------------------------------------- the made-up card ----

IDM = bytes.fromhex("0114aabbccddeeff")
PMM = bytes.fromhex("0118344455667788")


def history():
    """Six invented records, newest first, with balances that add up."""
    out, bal = [], 4210
    rows = [(0x16, 0x01, 0x25, 0x11, 0x25, 0x18, 168), (0x08, 0x02, 0x25, 0x11, 0, 0, -2000),
            (0xC7, 0x46, 0x5A, 0x60, 0x12, 0x34, 380), (0x16, 0x01, 0x25, 0x18, 0x25, 0x11, 168),
            (0x05, 0x0D, 0x01, 0x02, 0x03, 0x04, 210), (0x16, 0x01, 0x25, 0x11, 0x25, 0x1A, 199)]
    for n, (term, proc, b6, b7, b8, b9, fare) in enumerate(rows):
        day = 20 - n
        w = (24 << 9) | (6 << 5) | day
        out.append(bytes([term, proc, 0, 0, w >> 8, w & 0xFF, b6, b7, b8, b9,
                          bal & 0xFF, bal >> 8, 0, 0, 0x30 - n, 0]))
        bal += fare
    return out


HISTORY = history()
ATTR = bytes(11) + struct.pack("<H", 4210) + bytes(3)
SERVICES = {0x090F: HISTORY, 0x008B: [ATTR]}


def felica(frame):
    """One FeliCa command (with its length byte) in, the card's frame out."""
    cmd = frame[1]
    if cmd == 0x00:  # Polling
        sc = frame[2] << 8 | frame[3]
        if sc not in (0x0003, 0xFFFF):
            return None
        rd = bytes([0x00, 0x03]) if frame[4] == 1 else b""
        body = bytes([0x01]) + IDM + PMM + rd
        return bytes([len(body) + 1]) + body
    if frame[2:10] != IDM:
        return None
    if cmd == 0x06:  # Read Without Encryption, one service
        svc = frame[11] | frame[12] << 8
        n, off, idx = frame[13], 14, []
        for _ in range(n):
            if frame[off] & 0x80:
                idx.append(frame[off + 1]); off += 2
            else:
                idx.append(frame[off + 1] | frame[off + 2] << 8); off += 3
        blocks = SERVICES.get(svc)
        if blocks is None:
            status = b"\x01\xa6"
        elif any(i >= len(blocks) for i in idx):
            status = b"\x01\xa8"
        else:
            body = bytes([0x07]) + IDM + b"\x00\x00" + bytes([n]) + b"".join(blocks[i] for i in idx)
            return bytes([len(body) + 1]) + body
        body = bytes([0x07]) + IDM + status
        return bytes([len(body) + 1]) + body
    return None


def dump_file():
    """The same card as a squirrel-felica-dump/1 file."""
    def svc(code, blocks):
        return {"code": f"0x{code:04x}", "kind": "cyclic-ro", "readable": True, "key_version": "0x0000",
                "blocks": [b.hex() for b in blocks], "attempts": []}
    system = {"system_code": "0x0003", "idm": IDM.hex(), "pmm": PMM.hex(), "mode": 0,
              "search_stopped_at": None, "areas": [],
              "services": [svc(0x008B, [ATTR]), svc(0x090F, HISTORY)]}
    return {"schema": "squirrel-felica-dump/1", "tool": "test_pcsc.py", "created_at": "2024-01-01T00:00:00Z",
            "idm": IDM.hex(), "pmm": PMM.hex(), "pmm_decoded": None, "polls": [], "system_codes": ["0x0003"],
            "systems": [system], "log": [], "error": None}


# ---------------------------------------------- the virtual PC/SC reader ----

class VirtualReader:
    """vpcd client: answers the transparent exchange APDUs (FF C2 ...)."""

    def __init__(self, sock):
        self.sock = sock
        self.session = False
        self.felica_mode = False
        self.apdus = []

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError
            buf += chunk
        return buf

    def _send(self, data):
        self.sock.sendall(struct.pack(">H", len(data)) + data)

    def serve(self):
        try:
            while True:
                (n,) = struct.unpack(">H", self._recv(2))
                msg = self._recv(n)
                if n == 1:  # control: 0 off, 1 on, 2 reset, 4 ATR
                    if msg[0] == 4:
                        self._send(ATR)
                    continue
                self.apdus.append(msg)
                self._send(self.apdu(msg))
        except (ConnectionError, OSError):
            pass

    def apdu(self, a):
        if a[:2] != b"\xff\xc2":
            return b"\x6d\x00"
        p2, data = a[3], a[5:5 + a[4]]
        ok = b"\xc0\x03\x00\x90\x00"
        if p2 == 0x00:  # manage session
            if data[:1] == b"\x81":
                self.session = True
            elif data[:1] == b"\x82":
                self.session = self.felica_mode = False
            return ok + b"\x90\x00"
        if p2 == 0x02:  # switch protocol
            if not self.session or data[:4] != b"\x8f\x02\x03\x00":
                return b"\x6a\x81"
            self.felica_mode = True
            return ok + b"\x90\x00"
        if p2 == 0x01:  # transparent exchange
            if not self.felica_mode:
                return b"\x69\x85"
            i, frame = 0, None
            while i < len(data):
                tag = data[i]
                i += 1
                if tag & 0x1F == 0x1F:
                    tag = tag << 8 | data[i]
                    i += 1
                ln = data[i]
                i += 1
                if tag == 0x95:
                    frame = data[i:i + ln]
                i += ln
            resp = felica(frame) if frame else None
            if resp is None:  # the card stayed silent: timeout status
                return b"\xc0\x03\x01\x64\x01\x90\x00"
            return ok + b"\x92\x01\x00\x96\x02\x00\x00\x97" + bytes([len(resp)]) + resp + b"\x90\x00"
        return b"\x6a\x86"


def run(*args):
    return subprocess.run([EXE, "--stations", STATIONS, *args], capture_output=True, timeout=60)


def main():
    if not Path("/etc/reader.conf.d/vpcd").exists() and not REQUIRE:
        skip("skipped (no vpcd driver; install pcscd and vsmartcard-vpcd)")
    p = run("--list-readers")
    if p.returncode != 0:
        skip(f"skipped (PC/SC not running: {p.stderr.decode().strip()})")
    readers = p.stdout.decode().splitlines()
    vpcd = [r for r in readers if "Virtual PCD" in r]
    if not vpcd:
        skip(f"skipped (no Virtual PCD reader among {readers})")

    # No card yet: the reader waits, then says so.
    p = run("--reader", vpcd[0], "--wait", "0")
    check(p.returncode == 1 and b"no card" in p.stderr, f"empty reader: {p.returncode} {p.stderr!r}")

    sock = socket.create_connection(("127.0.0.1", VPCD_PORT), timeout=10)
    sock.settimeout(None)
    reader = VirtualReader(sock)
    threading.Thread(target=reader.serve, daemon=True).start()

    live = run("--reader", vpcd[0], "--wait", "10", "--json")
    check(live.returncode == 0, f"live read: exit {live.returncode} {live.stderr.decode()}")
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(dump_file(), f)
    from_dump = run("--dump", f.name, "--json")
    Path(f.name).unlink()
    check(from_dump.returncode == 0, f"dump read: {from_dump.stderr.decode()}")
    if live.returncode == 0 and from_dump.returncode == 0:
        a, b = json.loads(live.stdout), json.loads(from_dump.stdout)
        check(a == b, f"live and dump reads differ:\n{a}\n{b}")
        check(a.get("balance_yen") == 4210 and len(a.get("history", [])) == 6, f"live read: {a}")
    check(any(x[:4] == b"\xff\xc2\x00\x01" and x[5:8] == b"\x5f\x46\x04" for x in reader.apdus),
          "no transparent exchange with a timer object was sent")
    check(reader.apdus and reader.apdus[-1][:7] == bytes.fromhex("ffc20000028200"),
          "the session was not ended")
    text = run("--reader", vpcd[0], "--wait", "5")
    check(text.returncode == 0 and b"4210" in text.stdout.replace(b",", b""), f"text read: {text.stdout!r}")
    sock.close()

    print("squirrelsuica PC/SC test:", "FAILED" if failures else "ok")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
