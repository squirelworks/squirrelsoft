#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from felica_parse import (
    demo_nanaco,
    demo_suica,
    demo_suica_attrs,
    demo_waon,
    hist_waon,
    lookup_station,
    nanaco_balance,
    parse_history_block,
    parse_history_blocks,
    parse_nanaco_history_blocks,
    parse_waon_history_block,
    parse_waon_history_blocks,
    suica_balance,
    suica_balance_090f,
    suica_family_label,
)


def main() -> int:
    fails = 0

    def expect(cond, msg):
        nonlocal fails
        if not cond:
            print("FAIL:", msg)
            fails += 1

    # Balance is at bytes 10-11 LE of a 0x090f history block.
    from felica_parse import hist_rail, parse_nanaco_history_block

    rail = hist_rail(0x16, 0x01, 2026, 10, 28, 0xE3, 0x59, 0xE3, 0x5E, 2480)
    bal = suica_balance_090f(rail)
    expect(bal == 2480, f"balance 2480 got {bal}")

    # Prefer 008b when history balance is ~10x larger (guard).
    inflated = hist_rail(0x16, 0x01, 2026, 1, 1, 1, 1, 1, 2, 50945)
    info = bytearray(16)
    info[11] = 0xE6
    info[12] = 0x13
    picked = suica_balance(bytes(inflated), bytes(info))
    expect(picked == 5094, f"prefer 008b when 090f ~10x, got {picked}")

    expect(nanaco_balance(bytes([0x50, 0x14, 0x00, 0x00]) + bytes(12)) == 5200, "nanaco")

    card = demo_suica()
    expect(card.balance_yen == 2480, "demo suica balance")
    expect(len(card.history) >= 1, "demo history")
    expect(demo_nanaco().balance_yen == 5200, "demo nanaco")
    expect(demo_nanaco().balance_yen < 10000, "demo not inflated")

    empty = parse_history_block(bytes(16))
    expect(empty is None, "empty history")

    expect(lookup_station(0, 0xE3, 0x59) == "乃木坂", "station 乃木坂")
    expect(lookup_station(0, 0xE3, 0x5E) == "代々木上原", "station 代々木上原")

    # Qiita public fixture
    hex_s = "160100042499e359e35e710b001b0800"
    block = bytes(int(hex_s[i : i + 2], 16) for i in range(0, len(hex_s), 2))
    e = parse_history_block(block)
    expect(e is not None, "qiita parse")
    expect(e.date == "2018-04-25", f"qiita date {e.date if e else None}")
    expect(e.balance_yen == 2929, f"qiita bal {e.balance_yen if e else None}")
    expect(e.enter_station == "乃木坂", f"qiita enter {e.enter_station if e else None}")
    expect(e.exit_station == "代々木上原", f"qiita exit {e.exit_station if e else None}")

    expect(card.history[0].enter_station == "乃木坂", "demo enter name")
    expect(card.history[0].exit_station == "代々木上原", "demo exit name")
    expect(card.history[0].fare_yen == 168, f"demo fare {card.history[0].fare_yen}")
    expect(card.history[2].kind == "charge", "demo charge kind")
    expect(card.history[2].fare_yen == 2000, f"demo charge amount {card.history[2].fare_yen}")
    expect(len(parse_history_blocks([block])) == 1, "history list")

    # S1.3: a synthetic shop sale on terminal type CA (process 46), the layout real cards use;
    # bytes 6-7 are the time and 8-9 a terminal ID, not stations.
    ca = bytes.fromhex("ca4600003264" "70a0" "2a5c" "d007" "00012c" "00")
    e = parse_history_block(ca)
    expect(e is not None and e.kind == "sale", f"CA sale kind {e.kind if e else None}")
    expect(e.enter_code is None and e.exit_code is None, f"CA sale codes {e.enter_code} {e.exit_code}")
    expect("14:05" in e.note, f"CA sale time in note: {e.note}")
    expect(e.balance_yen == 2000, f"CA sale balance {e.balance_yen}")

    # nanaco history 0x564f
    nc = demo_nanaco()
    expect(len(nc.history) >= 2, "nanaco history")
    expect(nc.history[0].kind == "sale", "nanaco sale")
    expect(nc.history[0].fare_yen == 480, f"nanaco fare {nc.history[0].fare_yen}")
    expect(nc.history[0].balance_yen == 5200, "nanaco hist bal")

    # Chunked history read: a card that refuses >4 blocks or past block 19.
    from suica_read import read_blocks_chunked

    calls = []

    def fake_card(limit, total):
        def read_fn(idx):
            calls.append(list(idx))
            if len(idx) > limit or any(i >= total for i in idx):
                raise IOError("card refused")
            return [bytes([i]) + bytes(15) for i in idx]
        return read_fn

    got = read_blocks_chunked(fake_card(4, 20), 20)
    expect(len(got) == 20, f"chunked read 20 blocks, got {len(got)}")
    expect([b[0] for b in got] == list(range(20)), "chunked read order")
    expect(max(len(c) for c in calls) <= 4, "chunks are <= 4 blocks")
    got = read_blocks_chunked(fake_card(1, 20), 20)
    expect(len(got) == 20, f"single-block fallback 20 blocks, got {len(got)}")
    got = read_blocks_chunked(fake_card(4, 6), 20)
    expect(len(got) == 6, f"short service stops at 6, got {len(got)}")

    # nfcpy returns one bytearray of n*16 bytes, not a list of blocks.
    from suica_read import split_blocks

    def nfcpy_like(limit, total):
        def read_fn(idx):
            if len(idx) > limit or any(i >= total for i in idx):
                raise IOError("card refused")
            return bytearray(b"".join(bytes([i]) + bytes(15) for i in idx))
        return read_fn

    got = read_blocks_chunked(nfcpy_like(4, 20), 20)
    expect(len(got) == 20, f"nfcpy bytearray read 20 blocks, got {len(got)}")
    expect(all(len(b) == 16 for b in got), "nfcpy blocks are 16 bytes")
    expect([b[0] for b in got] == list(range(20)), "nfcpy block order")
    expect(split_blocks(bytearray(32)) == [bytes(16), bytes(16)], "split 2 blocks")
    expect(split_blocks([b"\x01" * 16]) == [b"\x01" * 16], "list passes through")
    expect(split_blocks(None) == [], "empty read")

    # Made-up debugging dumps (tests/fixtures/make_demo_dumps.py; no real card data).
    import json
    fx = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")

    def service_blocks(name, system, code):
        with open(os.path.join(fx, name), encoding="utf-8") as f:
            d = json.load(f)
        sy = next(x for x in d["systems"] if x["system_code"] == system)
        return [bytes.fromhex(b) for b in next(v for v in sy["services"] if v["code"] == code)["blocks"]]

    hist = service_blocks("demo-suica.json", "0x0003", "0x090f")
    attrs = service_blocks("demo-suica.json", "0x0003", "0x008b")
    entries = parse_history_blocks(hist)
    expect(len(entries) == 20, f"demo suica: {len(entries)} entries")
    expect(suica_balance(hist[0], attrs[0]) == entries[0].balance_yen == 5456, "demo suica balance")
    kinds = [e.kind for e in entries]
    expect((kinds.count("train"), kinds.count("sale"), kinds.count("bus"), kinds.count("charge")) == (9, 6, 2, 3),
           f"demo suica kinds {kinds}")
    rides = [e for e in entries if e.kind == "train"]
    expect(all(e.enter_station and e.exit_station and "/" not in e.enter_station + e.exit_station for e in rides),
           "demo suica: every ride resolves to station names (JR and private line)")
    for newer, older in zip(entries, entries[1:]):
        if newer.fare_yen:
            expect(abs(older.balance_yen - newer.balance_yen) == newer.fare_yen, f"demo suica chain at {newer.date}")
    expect(all(e.enter_code is None for e in entries if e.kind == "bus"), "demo suica: buses carry no rail codes")
    buses = [e for e in entries if e.kind == "bus"]
    expect(len(buses) == 2, f"demo suica bus count {len(buses)}")
    expect(all(e.bus_operator is not None and e.bus_stop is not None for e in buses),
           "demo suica: buses carry operator/stop codes")
    expect(all("事業者" in e.note and "停留所" in e.note for e in buses),
           "demo suica: bus notes name operator/stop")

    # Issuer labels from synthetic attributes 0x008B (public type/region nibbles).
    expect(suica_family_label(demo_suica_attrs(3, 0)) == "ICOCA", "ICOCA label")
    expect(suica_family_label(demo_suica_attrs(0, 0)) == "IruCa", "IruCa label")
    expect(suica_family_label(demo_suica_attrs(2, 0), 0x0D) == "PASMO", "PASMO from settlement")
    expect(suica_family_label(demo_suica_attrs(2, 0), 0x3F) == "モバイルSuica", "mobile Suica")
    expect(suica_family_label(demo_suica_attrs(2, 2)) == "PiTaPa", "PiTaPa from region")
    expect(demo_suica().brand_label == "Suica / PASMO", "demo suica label")

    # Explicit bus block: terminal 0x05, process 0x0D, operator/stop BE, no rail codes.
    bus = bytes.fromhex("050d00003542" "0c4a" "0012" "e803" "000000" "00")
    be = parse_history_block(bus)
    expect(be is not None and be.kind == "bus", f"bus kind {be.kind if be else None}")
    expect(be.enter_code is None and be.exit_code is None, "bus has no rail codes")
    expect(be.bus_operator == 0x0C4A and be.bus_stop == 0x0012, f"bus codes {be.bus_operator} {be.bus_stop}")
    expect("事業者 0C4A" in be.note and "停留所 0012" in be.note, f"bus note {be.note}")

    # WAON history parity with Kotlin layout (synthetic 32-byte record).
    wr = hist_waon(0x04, 2026, 10, 28, 19, 10, 4584, 620, 0)
    we = parse_waon_history_block(wr)
    expect(we is not None and we.kind == "sale", "waon sale")
    expect(we.fare_yen == 620 and we.balance_yen == 4584, f"waon amounts {we.fare_yen} {we.balance_yen}")
    expect(we.date == "2026-10-28" and "19:10" in we.note, f"waon date/note {we.date} {we.note}")
    expect(len(parse_waon_history_blocks([wr[:16], wr[16:]])) == 1, "waon paired blocks")
    expect(demo_waon().history and demo_waon().history[0].fare_yen == 620, "demo waon history")

    nh = service_blocks("demo-nanaco.json", "0xfe00", "0x564f")
    nb = service_blocks("demo-nanaco.json", "0xfe00", "0x5597")
    ne = parse_nanaco_history_blocks(nh)
    expect(nanaco_balance(nb[0]) == ne[0].balance_yen == 3030, f"demo nanaco balance {nanaco_balance(nb[0])}")
    expect([e.kind for e in ne] == ["sale", "other", "sale", "sale", "charge"], f"demo nanaco kinds {[e.kind for e in ne]}")

    if fails:
        print(fails, "failure(s)")
        return 1
    print("suica_parse tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
