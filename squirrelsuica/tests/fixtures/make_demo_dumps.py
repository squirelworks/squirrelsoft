#!/usr/bin/env python3
"""Write the made-up debugging dumps in this folder.

Real card data never goes into this repo, in any form. These dumps are
invented from scratch: the IDm, PMm, dates, fares, balances, stations, shops,
terminal IDs and every other byte come from the rules below and a fixed random
seed. Station codes are taken from the public table in data/StationCode.csv so
that names resolve.

Each card is a simulated FeliCa card (the same command set a phone or PaSoRi
sends), walked by SquirrelSuicaAnalysis's own Analyzer, so the files have
exactly the squirrel-felica-dump/1 shape the app exports.

Usage (from squirrelsuica/):
  python3 -I tests/fixtures/make_demo_dumps.py            rewrite the dumps
  python3 -I tests/fixtures/make_demo_dumps.py --check    fail if they are stale
"""
import datetime
import importlib.util
import json
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent.parent
ROOT = APP.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


fp = _load("felica_parse", APP / "felica_parse.py")
sa = _load("suica_analyze", ROOT / "squirrelsuica-analysis" / "suica_analyze.py")


def le16(v):
    return bytes([v & 0xFF, v >> 8 & 0xFF])


class SyntheticCard:
    """A FeliCa card answering Polling, Request System Code, Search Service
    Code, Request Service, Request Response and Read Without Encryption.
    systems: list of (system_code, idm, nodes, blocks) where nodes is the
    search order (areas as (code, end), services as (code,)) and blocks maps a
    readable service to its 16-byte blocks. max_read caps blocks per Read."""

    def __init__(self, pmm, systems, max_read):
        self.pmm, self.systems, self.max_read = pmm, systems, max_read

    def _find(self, idm):
        return next((s for s in self.systems if s[1] == idm), None)

    def _reply(self, code, idm, data):
        return bytes([len(data) + 10, code]) + idm + data

    def __call__(self, c):
        cmd = c[1]
        if cmd == 0x00:
            want = c[2] << 8 | c[3]
            for sc, idm, _, _ in self.systems:
                if want == 0xFFFF or want == sc:
                    rd = {1: bytes([sc >> 8, sc & 0xFF]), 2: b"\x00\x83"}.get(c[4], b"")
                    body = idm + self.pmm + rd
                    return bytes([len(body) + 2, 0x01]) + body
            return b""
        s = self._find(bytes(c[2:10]))
        if s is None:
            return b""
        sc, idm, nodes, blocks = s
        if cmd == 0x0C:
            return self._reply(0x0D, idm, bytes([len(self.systems)]) +
                               b"".join(bytes([x[0] >> 8, x[0] & 0xFF]) for x in self.systems))
        if cmd == 0x04:
            return self._reply(0x05, idm, b"\x00")
        if cmd == 0x0A:
            i = c[10] | c[11] << 8
            if i >= len(nodes):
                return self._reply(0x0B, idm, b"\xff\xff")
            return self._reply(0x0B, idm, b"".join(le16(v) for v in nodes[i]))
        if cmd == 0x02:
            n = c[10]
            codes = [c[11 + 2 * k] | c[12 + 2 * k] << 8 for k in range(n)]
            known = {x[0] for x in nodes if len(x) == 1}
            return self._reply(0x03, idm, bytes([n]) + b"".join(
                le16(0x0001 if code in known else 0xFFFF) for code in codes))
        if cmd == 0x06:
            svc, n, off, idx = c[11] | c[12] << 8, c[13], 14, []
            for _ in range(n):
                if c[off] & 0x80:
                    idx.append(c[off + 1]); off += 2
                else:
                    idx.append(c[off + 1] | c[off + 2] << 8); off += 3
            data = blocks.get(svc)
            if data is None:
                return self._reply(0x07, idm, b"\x01\xa6")
            if n > self.max_read:
                return self._reply(0x07, idm, b"\x01\xa2")
            if any(i >= len(data) for i in idx):
                return self._reply(0x07, idm, b"\x01\xa8")
            return self._reply(0x07, idm, b"\x00\x00" + bytes([n]) + b"".join(data[i] for i in idx))
        return b""


def dump(card, tool):
    d = sa.Analyzer(card, max_blocks=24).analyze(None, tool)
    # Deterministic output: no wall-clock values.
    d["created_at"] = "2024-01-01T00:00:00Z"
    for e in d["log"]:
        e["ms"] = 2.0
    for s in d["systems"]:
        for v in s["services"]:
            for a in v["attempts"]:
                a["avg_ms"] = 2.0 if a["commands"] else 0.0
    return d


# ---------------------------------------------------------------- Suica ----

def station_pool(rnd):
    """Rail codes from the public table: JR (area 0, line < 0x80) and one
    private line (line >= 0x80) with its area, as (area, line, station)."""
    table = fp._station_table()
    jr_lines = sorted({k[1] for k in table if k[0] == 0 and 0x01 <= k[1] < 0x80})
    line = rnd.choice(jr_lines)
    jr = sorted(k for k in table if k[0] == 0 and k[1] == line and table[k][2])
    private = sorted(k for k in table if k[1] >= 0x80 and table[k][2])
    pline = rnd.choice(sorted({k[:2] for k in private}))
    return jr, sorted(k for k in private if k[:2] == pline)


def suica_history(rnd):
    """20 records, oldest first, over two weeks of an invented 2024 month."""
    jr, priv = station_pool(rnd)
    day = datetime.date(2024, 3, 4)
    bal = 3000
    out = []

    def rec(term, proc, d, b6, b7, b8, b9, region=0):
        w = ((d.year - 2000) << 9) | (d.month << 5) | d.day
        return bytes([term, proc, 0, 0, w >> 8, w & 0xFF, b6, b7, b8, b9,
                      bal & 0xFF, bal >> 8, 0, 0, len(out) + 0x40, region])

    plan = (["train"] * 8 + ["shop_c7", "shop_c8", "shop_ca", "shop_ca", "shop_ca"] +
            ["bus", "bus", "private", "private", "charge", "charge", "vending"])
    rnd.shuffle(plan)
    plan[0] = "charge"  # start the month with a top-up
    for kind in plan:
        if rnd.random() < 0.45:
            day += datetime.timedelta(days=1)
        if kind == "charge":
            bal += rnd.choice([1000, 2000, 3000, 5000])
            a = rnd.choice(jr)
            out.append(rec(0x08, 0x02, day, a[1], a[2], 0, 0))
        elif kind == "train":
            a, b = rnd.sample(jr, 2)
            bal -= rnd.choice([146, 157, 168, 178, 199, 208, 220])
            out.append(rec(0x16, 0x01, day, a[1], a[2], b[1], b[2]))
        elif kind == "private":
            a, b = rnd.sample(priv, 2)
            region = a[0] << 6 | b[0] << 4
            bal -= rnd.choice([150, 180, 210, 240])
            out.append(rec(0x16, 0x01, day, a[1], a[2], b[1], b[2], region))
        elif kind == "bus":
            bal -= rnd.choice([180, 210, 230])
            out.append(rec(0x05, 0x0D, day, rnd.randrange(256), rnd.randrange(256),
                           rnd.randrange(256), rnd.randrange(256)))
        else:
            term = {"shop_c7": 0xC7, "shop_c8": 0xC8, "shop_ca": 0xCA, "vending": 0xC8}[kind]
            bal -= rnd.randrange(110, 1600, 10) if kind != "vending" else rnd.choice([130, 150, 160])
            hh, mm = rnd.randrange(7, 23), rnd.randrange(60)
            tw = hh << 11 | mm << 5
            out.append(rec(term, 0x46, day, tw >> 8, tw & 0xFF, rnd.randrange(256), rnd.randrange(256)))
        assert 0 < bal < 20000
    return out[::-1], bal  # newest first, current balance


def suica_card(rnd):
    history, bal = suica_history(rnd)
    attrs = bytes(8) + b"\x20\x00\x00" + le16(bal) + b"\x00" + le16(0x0040 + len(history))
    idm_t = bytes.fromhex("01145a1b2c3d4e5f")
    idm_c = bytes.fromhex("11145a1b2c3d4e5f")  # FeliCa gives each system its own IDm
    transit = (0x0003, idm_t,
               [(0x0000, 0xFFFE), (0x0080, 0x00BF), (0x0088,), (0x008B,), (0x0900, 0x093F), (0x090C,), (0x090F,),
                (0x1000, 0x103F), (0x1008,), (0x1080, 0x10BF), (0x108C,), (0x108F,)],
               {0x008B: [attrs], 0x090F: history,
                0x108F: [bytes([rnd.randrange(256) for _ in range(16)]) for _ in range(3)]})
    common = (0xFE00, idm_c, [(0x0000, 0xFFFE), (0x2000, 0x203F), (0x2008,)], {})
    return SyntheticCard(bytes.fromhex("0118344455667788"), [transit, common], max_read=12)


# --------------------------------------------------------------- nanaco ----

def nanaco_card(rnd):
    t = datetime.datetime(2024, 5, 6, 8, 0)
    bal = 1500
    recs = []

    def rec(typ, amount):
        p = ((t.year - 2000) << 21) | (t.month << 17) | (t.day << 12) | (t.hour << 6) | t.minute
        return (bytes([typ]) + amount.to_bytes(4, "big") + bal.to_bytes(4, "big") +
                p.to_bytes(4, "big") + le16(0x0100 + len(recs)) + b"\x00")

    for typ in (0x6F, 0x47, 0x47, 0x83, 0x47):  # charge, two purchases, points exchange, purchase
        t += datetime.timedelta(hours=rnd.randrange(5, 40), minutes=rnd.randrange(60))
        if typ == 0x6F:
            amount = 5000
            bal += amount
        elif typ == 0x47:
            amount = rnd.randrange(98, 1800)
            bal -= amount
        else:
            amount = 300  # points turned into balance
            bal += amount
        recs.append(rec(typ, amount))
    balance = bal.to_bytes(4, "little") + bytes(12)
    idm_n = bytes.fromhex("01275f6e7d8c9bab")
    idm_c = bytes.fromhex("11275f6e7d8c9bab")
    nanaco_sys = (0x04C7, idm_n, [(0x0000, 0xFFFE), (0x0100, 0x01BF), (0x0108,)], {})
    common = (0xFE00, idm_c,
              [(0x0000, 0xFFFE), (0x5580, 0x56BF), (0x5588,), (0x5590,), (0x5597,), (0x564C,), (0x564F,)],
              {0x5597: [balance], 0x564F: recs[::-1]})
    return SyntheticCard(bytes.fromhex("01a1b2c3d4e5f6ff"), [nanaco_sys, common], max_read=4)


def main():
    rnd = random.Random(20240304)
    files = {
        "demo-suica.json": dump(suica_card(rnd), "make_demo_dumps.py (made-up Suica)"),
        "demo-nanaco.json": dump(nanaco_card(rnd), "make_demo_dumps.py (made-up nanaco)"),
    }
    stale = []
    for name, d in files.items():
        text = json.dumps(d, indent=1, ensure_ascii=False) + "\n"
        path = HERE / name
        if "--check" in sys.argv:
            if not path.is_file() or path.read_text(encoding="utf-8") != text:
                stale.append(name)
        else:
            path.write_text(text, encoding="utf-8")
    if stale:
        print("stale (run make_demo_dumps.py):", ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
