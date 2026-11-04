#!/usr/bin/env python3
"""SquirrelSuicaAnalysis desktop twin: dump a FeliCa card with a PaSoRi (nfcpy).

Writes the same JSON format as the Android app ("squirrel-felica-dump/1",
schema/felica-dump.schema.json). Read-only: plain FeliCa commands, no keys.

  python3 suica_analyze.py                 # live, first USB reader
  python3 suica_analyze.py --redact -o d.json
  python3 suica_analyze.py --demo          # built-in fake card, no hardware
  python3 suica_analyze.py -i d.json --history   # balances and history of a saved dump

Original code written from the public FeliCa command descriptions.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import json
import sys
import time
from typing import Callable, Dict, List, Optional

SCHEMA = "squirrel-felica-dump/1"
REDACTED = "redacted"
PMM_CLASSES = ["request_service", "request_response", "authenticate", "read", "write", "other"]
T_MS = 256.0 * 16.0 / 13_560_000.0 * 1000.0
CHUNK_SIZES = [1, 2, 4, 8, 12, 15]


class TagLost(IOError):
    pass


# ---- frames -----------------------------------------------------------------

def _frame(code: int, body: bytes) -> bytes:
    return bytes([len(body) + 2, code]) + body


def _le16(v: int) -> bytes:
    return bytes([v & 0xFF, (v >> 8) & 0xFF])


def polling(system_code: int, request_code: int = 0) -> bytes:
    return _frame(0x00, bytes([(system_code >> 8) & 0xFF, system_code & 0xFF, request_code, 0]))


def request_system_code(idm: bytes) -> bytes:
    return _frame(0x0C, idm)


def search_service_code(idm: bytes, index: int) -> bytes:
    return _frame(0x0A, idm + _le16(index))


def request_response(idm: bytes) -> bytes:
    return _frame(0x04, idm)


def request_service(idm: bytes, codes: List[int]) -> bytes:
    return _frame(0x02, idm + bytes([len(codes)]) + b"".join(_le16(c) for c in codes))


def block_element(b: int) -> bytes:
    return bytes([0x80, b]) if b <= 0xFF else bytes([0x00, b & 0xFF, (b >> 8) & 0xFF])


def read_without_encryption(idm: bytes, service: int, blocks: List[int]) -> bytes:
    return _frame(0x06, idm + b"\x01" + _le16(service) + bytes([len(blocks)])
                  + b"".join(block_element(b) for b in blocks))


def _body(resp: Optional[bytes], code: int):
    if not resp or len(resp) < 2:
        return None
    off = 1 if resp[0] in (len(resp), len(resp) - 1) else 0
    if off + 9 > len(resp) or resp[off] != code:
        return None
    return bytes(resp[off + 1:off + 9]), bytes(resp[off + 9:])


def attribute(code: int) -> int:
    return code & 0x3F


def is_area(code: int) -> bool:
    return attribute(code) & 0x3E == 0


def readable_without_key(code: int) -> bool:
    return not is_area(code) and attribute(code) & 1 == 1


KINDS = {0x00: "area", 0x08: "random-rw", 0x0A: "random-ro", 0x0C: "cyclic-rw", 0x0E: "cyclic-ro",
         0x10: "purse-direct", 0x12: "purse-cashback", 0x14: "purse-decrement", 0x16: "purse-ro"}


def service_kind(code: int) -> str:
    return KINDS.get(attribute(code) & 0x3E, "unknown")


def response_time_ms(param: int, n: int = 1) -> float:
    a, b, e = param & 7, (param >> 3) & 7, (param >> 6) & 3
    return T_MS * ((b + 1) + n * (a + 1)) * (4 ** e)


def decode_pmm(pmm_hex: Optional[str]):
    if not pmm_hex or len(pmm_hex) != 16:
        return None
    p = bytes.fromhex(pmm_hex)
    return {"rom_type": p[0], "ic_type": p[1],
            "max_response_ms": {k: round(response_time_ms(p[2 + i]), 3) for i, k in enumerate(PMM_CLASSES)}}


def code4(v: Optional[int]) -> Optional[str]:
    return None if v is None else "0x%04x" % v


# ---- analyzer ---------------------------------------------------------------

class Analyzer:
    def __init__(self, transceive: Callable[[bytes], bytes], max_blocks: int = 64, max_search: int = 512):
        self.tx = transceive
        self.max_blocks = max_blocks
        self.max_search = max_search
        self.log: List[Dict] = []

    def _x(self, name: str, cmd: bytes) -> Optional[bytes]:
        t0 = time.perf_counter()
        try:
            resp = bytes(self.tx(cmd) or b"")
            self.log.append({"name": name, "command": cmd.hex(), "response": resp.hex(),
                             "ms": round((time.perf_counter() - t0) * 1000, 3), "error": None})
            return resp
        except TagLost as e:
            self.log.append({"name": name, "command": cmd.hex(), "response": None,
                             "ms": round((time.perf_counter() - t0) * 1000, 3), "error": str(e) or "tag lost"})
            raise
        except Exception as e:  # noqa: BLE001 - record and keep going
            self.log.append({"name": name, "command": cmd.hex(), "response": None,
                             "ms": round((time.perf_counter() - t0) * 1000, 3), "error": str(e) or type(e).__name__})
            return None

    def analyze(self, tag_idm: Optional[bytes], tool: str) -> Dict:
        polls, system_codes, systems = [], [], []
        idm, pmm, error = tag_idm, None, None
        try:
            for sc in (0xFFFF, 0x0003, 0xFE00):
                for rc in (1, 2):
                    b = _body(self._x("polling %04x rc=%d" % (sc, rc), polling(sc, rc)), 0x01)
                    p_idm = p_pmm = rd = None
                    if b and len(b[1]) >= 8:
                        p_idm, p_pmm = b[0], b[1][:8]
                        rd = (b[1][8] << 8 | b[1][9]) if len(b[1]) >= 10 else None
                        if pmm is None:
                            idm, pmm = p_idm, p_pmm
                    polls.append({"system_code": code4(sc), "request_code": rc,
                                  "idm": p_idm.hex() if p_idm else None,
                                  "pmm": p_pmm.hex() if p_pmm else None, "request_data": code4(rd)})
            if idm is None:
                raise TagLost("no IDm: card did not answer polling")
            b = _body(self._x("request system code", request_system_code(idm)), 0x0D)
            if b and b[1]:
                n = b[1][0]
                system_codes = [b[1][1 + 2 * i] << 8 | b[1][2 + 2 * i] for i in range(n)]
            for sc in system_codes or [0xFFFF]:
                systems.append(self._system(sc, idm))
        except TagLost as e:
            error = "Tag lost: %s" % (e or "I/O error")
        return {
            "schema": SCHEMA, "tool": tool,
            "created_at": _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "idm": idm.hex() if idm else None, "pmm": pmm.hex() if pmm else None,
            "pmm_decoded": decode_pmm(pmm.hex() if pmm else None),
            "polls": polls, "system_codes": [code4(c) for c in system_codes],
            "systems": systems, "log": self.log, "error": error,
        }

    def _system(self, sc: int, fallback_idm: bytes) -> Dict:
        b = _body(self._x("polling %04x" % sc, polling(sc)), 0x01)
        idm = b[0] if b else fallback_idm
        sys_pmm = b[1][:8].hex() if b and len(b[1]) >= 8 else None
        m = _body(self._x("request response", request_response(idm)), 0x05)
        mode = m[1][0] if m and m[1] else None
        areas, codes, stopped = [], [], None
        for i in range(self.max_search):
            r = _body(self._x("search service #%d" % i, search_service_code(idm, i)), 0x0B)
            if not r or len(r[1]) < 2:
                stopped = i
                break
            c = r[1][0] | r[1][1] << 8
            if c == 0xFFFF:
                break
            if len(r[1]) >= 4:
                areas.append({"code": code4(c), "end_code": code4(r[1][2] | r[1][3] << 8)})
            else:
                codes.append(c)
        kv: Dict[int, int] = {}
        for s in range(0, len(codes), 32):
            batch = codes[s:s + 32]
            r = _body(self._x("request service x%d" % len(batch), request_service(idm, batch)), 0x03)
            if r and r[1]:
                for i in range(min(r[1][0], len(batch))):
                    kv[batch[i]] = r[1][1 + 2 * i] | r[1][2 + 2 * i] << 8
        services = []
        for c in codes:
            readable = readable_without_key(c)
            blocks: List[str] = []
            attempts = []
            if readable:
                for chunk in CHUNK_SIZES:
                    attempt, got = self._sweep(idm, c, chunk)
                    attempts.append(attempt)
                    if len(got) > len(blocks):
                        blocks = [g.hex() for g in got]
            services.append({"code": code4(c), "kind": service_kind(c), "readable": readable,
                             "key_version": code4(kv.get(c)), "blocks": blocks, "attempts": attempts})
        return {"system_code": code4(sc), "idm": idm.hex(), "pmm": sys_pmm, "mode": mode,
                "search_stopped_at": stopped, "areas": areas, "services": services}

    def _sweep(self, idm: bytes, code: int, chunk: int):
        got: List[bytes] = []
        i, commands, total = 0, 0, 0.0
        sf1 = sf2 = err = stopped = None
        while i < self.max_blocks:
            n = min(chunk, self.max_blocks - i)
            before = len(self.log)
            resp = self._x("read %04x [%d..%d]" % (code, i, i + n - 1),
                           read_without_encryption(idm, code, list(range(i, i + n))))
            commands += 1
            total += self.log[before]["ms"] if len(self.log) > before else 0.0
            b = _body(resp, 0x07)
            if not b or len(b[1]) < 2:
                err, stopped = (self.log[-1]["error"] or "invalid response"), i
                break
            d = b[1]
            if d[0] or d[1]:
                sf1, sf2, stopped = d[0], d[1], i
                break
            cnt = d[2] if len(d) > 2 else 0
            blocks = [d[3 + 16 * k:19 + 16 * k] for k in range(cnt) if 19 + 16 * k <= len(d)]
            got.extend(blocks)
            if len(blocks) < n:
                err, stopped = "short read: %d of %d" % (len(blocks), n), i + len(blocks)
                break
            i += n
        return ({"chunk_size": chunk, "blocks_read": len(got), "stopped_at": stopped, "sf1": sf1, "sf2": sf2,
                 "error": err, "commands": commands, "avg_ms": round(total / commands, 3) if commands else 0.0},
                got)


def redact(dump: Dict) -> Dict:
    """Replace every IDm (top level, polls, systems and inside raw frames)."""
    idms = {x for x in [dump.get("idm")] + [p["idm"] for p in dump["polls"]] + [s["idm"] for s in dump["systems"]] if x}
    out = json.loads(json.dumps(dump))
    for obj in [out] + out["polls"] + out["systems"]:
        if obj.get("idm"):
            obj["idm"] = REDACTED
    for entry in out["log"]:
        for k in ("command", "response"):
            if entry[k]:
                for i in idms:
                    entry[k] = entry[k].replace(i, "x" * len(i))
    return out


# ---- demo card (same behaviour as the Android unit-test fake) -----------------

class DemoCard:
    IDM = bytes.fromhex("0101123456789abc")
    PMM = bytes.fromhex("100b4b42848510ff")
    NODES = [(0x0000, 0xFFFE), (0x090F,), (0x008B,), (0x1008,)]
    SIZES = {0x090F: 20, 0x008B: 1}

    def _reply(self, code: int, data: bytes) -> bytes:
        return bytes([len(data) + 10, code]) + self.IDM + data

    def __call__(self, c: bytes) -> bytes:
        cmd = c[1]
        if cmd == 0x00:
            sc = c[2] << 8 | c[3]
            if sc not in (0xFFFF, 0x0003):
                return b""
            rd = {1: b"\x00\x03", 2: b"\x00\x83"}.get(c[4], b"")
            body = self.IDM + self.PMM + rd
            return bytes([len(body) + 2, 0x01]) + body
        if cmd == 0x0C:
            return self._reply(0x0D, b"\x01\x00\x03")
        if cmd == 0x04:
            return self._reply(0x05, b"\x00")
        if cmd == 0x0A:
            idx = c[10] | c[11] << 8
            if idx >= len(self.NODES):
                return self._reply(0x0B, b"\xff\xff")
            return self._reply(0x0B, b"".join(_le16(v) for v in self.NODES[idx]))
        if cmd == 0x02:
            return self._reply(0x03, bytes([c[10]]) + b"\x00\x00" * c[10])
        if cmd == 0x06:
            svc, n, off, blocks = c[11] | c[12] << 8, c[13], 14, []
            for _ in range(n):
                if c[off] & 0x80:
                    blocks.append(c[off + 1]); off += 2
                else:
                    blocks.append(c[off + 1] | c[off + 2] << 8); off += 3
            size = self.SIZES.get(svc)
            if size is None:
                return self._reply(0x07, b"\x01\xa6")
            if n > 4:
                return self._reply(0x07, b"\x01\xa2")
            if any(b >= size for b in blocks):
                return self._reply(0x07, b"\x01\xa8")
            return self._reply(0x07, b"\x00\x00" + bytes([n]) + b"".join(bytes([b]) + bytes([svc & 0xFF]) * 15 for b in blocks))
        return b""


def analyze_with_nfcpy(device: str = "usb") -> Dict:
    try:
        import nfc  # type: ignore
        import nfc.tag.tt3 as tt3  # type: ignore
    except ImportError as e:
        raise SystemExit("nfcpy is required for live reads: pip install nfcpy (%s)" % e)
    holder: List[Optional[Dict]] = [None]

    def on_connect(tag):
        if not isinstance(tag, tt3.Type3Tag):
            print("not a FeliCa (Type3) tag", file=sys.stderr)
            return False

        def tx(frame: bytes) -> bytes:
            try:
                return bytes(tag.clf.exchange(bytearray(frame), timeout=1.0))
            except nfc.clf.CommunicationError as e:
                raise TagLost(str(e))

        holder[0] = Analyzer(tx).analyze(bytes(tag.idm), "SquirrelSuicaAnalysis desktop (nfcpy)")
        return False

    with nfc.ContactlessFrontend(device) as clf:
        print("Hold a FeliCa card on the reader…", file=sys.stderr)
        clf.connect(rdwr={"targets": ["212F", "424F"], "on-connect": on_connect})
    if holder[0] is None:
        raise SystemExit("no card read")
    return holder[0]


# ---- history (desktop twin of the Android History tab) -------------------------

SVC_SUICA_HIST, SVC_SUICA_INFO = 0x090F, 0x008B
SVC_NANACO_BAL, SVC_NANACO_HIST = 0x5597, 0x564F
SVC_EDY_BAL, SVC_EDY_HIST = 0x1317, 0x170F
SVC_WAON_BAL = 0x6817


def load_felica_parse():
    """SquirrelSuica's reference parser (../squirrelsuica/felica_parse.py), or None.

    Loaded by file path so nothing else from that folder is imported. The release
    zip ships it at the same relative path.
    """
    import importlib.util
    import os
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "squirrelsuica", "felica_parse.py")
    if not os.path.isfile(path):
        return None
    spec = importlib.util.spec_from_file_location("squirrel_felica_parse", path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod  # dataclasses look the module up by name
    spec.loader.exec_module(mod)
    return mod


def blocks_of(dump: Dict, code: int) -> Optional[List[bytes]]:
    """Raw blocks of the first readable service with this code, across all systems."""
    want = code4(code)
    for system in dump.get("systems") or []:
        for svc in system.get("services") or []:
            if (svc.get("code") or "").lower() == want and svc.get("readable") and svc.get("blocks"):
                try:
                    return [bytes.fromhex(b) for b in svc["blocks"]]
                except ValueError:
                    return None
    return None


def history_line(e) -> str:
    fare = ""
    if e.fare_yen:
        fare = ("+" if e.kind == "charge" else "-") + "¥{:,}".format(e.fare_yen)
    route = " → ".join(c for c in (e.enter_code, e.exit_code) if c)
    out = "%s %-6s %8s  bal ¥{:,}".format(e.balance_yen) % (e.date, e.kind, fare)
    out += "  t%02x p%02x" % (e.terminal & 0xFF, e.process & 0xFF)
    note = e.note or ""
    if route:
        out += "  " + route
        # The note repeats the route; keep its label and add names only if resolved.
        names = e.station_summary()
        note = note.split(" · ")[0] + (" · " + names if names and names != route else "")
    if note:
        out += "  · " + note
    return out.rstrip()


def history_text(dump: Dict, fp=None) -> str:
    """Balances and history parsed from a dump's raw blocks, like the app's History tab.

    Rail entries show the raw line/station codes (as on Android) plus SquirrelSuica's
    note, which names the stations when its station table knows them.
    """
    fp = fp if fp is not None else load_felica_parse()
    out: List[str] = []
    if fp is None:
        return "History needs ../squirrelsuica/felica_parse.py next to this tool.\n"

    def first(code: int) -> Optional[bytes]:
        b = blocks_of(dump, code)
        return b[0] if b else None

    def section(title: str, hist: Optional[List[bytes]], bal_blocks, balance, parse) -> None:
        if hist is None and not any(b is not None for b in bal_blocks):
            return
        out.append("== " + title)
        bal = None
        try:
            bal = balance(*bal_blocks)
        except (ValueError, IndexError, TypeError):
            bal = None
        out.append("   Balance ¥{:,}".format(bal) if bal is not None else "   Balance: not readable")
        if hist is not None:
            rows = parse(hist)
            if not rows:
                out.append("   No history entries in %d block(s)" % len(hist))
            for i, e in enumerate(rows):
                out.append("   %02d %s" % (i, history_line(e)))
        out.append("")

    suica = blocks_of(dump, SVC_SUICA_HIST)
    section("Suica / PASMO (svc 090f)", suica, (suica[0] if suica else None, first(SVC_SUICA_INFO)),
            fp.suica_balance, fp.parse_history_blocks)
    section("nanaco (svc 564f)", blocks_of(dump, SVC_NANACO_HIST), (first(SVC_NANACO_BAL),),
            lambda b: fp.nanaco_balance(b) if b else None, fp.parse_nanaco_history_blocks)
    section("Edy (svc 170f)", blocks_of(dump, SVC_EDY_HIST), (first(SVC_EDY_BAL),),
            lambda b: fp.edy_balance(b) if b else None, fp.parse_edy_history_blocks)
    section("WAON (svc 6817)", None, (first(SVC_WAON_BAL),),
            lambda b: fp.waon_balance(b) if b else None, None)
    if not out:
        out.append("No Suica, nanaco, Edy or WAON services were readable on this card.")
    return "\n".join(out).rstrip("\n") + "\n"


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--demo", action="store_true", help="analyse the built-in fake card")
    ap.add_argument("--device", default="usb", help="nfcpy device path (default: usb)")
    ap.add_argument("--redact", action="store_true", help="replace the IDm everywhere")
    ap.add_argument("-o", "--output", help="write JSON here instead of stdout")
    ap.add_argument("--history", action="store_true",
                    help="print parsed balances and history instead of JSON")
    ap.add_argument("-i", "--input", metavar="DUMP",
                    help="read a saved dump (desktop or Android export) instead of scanning")
    a = ap.parse_args(argv)
    if a.input:
        with open(a.input, encoding="utf-8") as f:
            dump = json.load(f)
        if dump.get("schema") != SCHEMA:
            print("warning: %s is not a %s dump" % (a.input, SCHEMA), file=sys.stderr)
    elif a.demo:
        dump = Analyzer(DemoCard(), max_blocks=24).analyze(None, "SquirrelSuicaAnalysis desktop (demo)")
    else:
        dump = analyze_with_nfcpy(a.device)
    if a.redact:
        dump = redact(dump)
    if a.history:
        text = history_text(dump)
    else:
        text = json.dumps(dump, indent=2, ensure_ascii=False) + "\n"
    if a.output:
        with open(a.output, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
