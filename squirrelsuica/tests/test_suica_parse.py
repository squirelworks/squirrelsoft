#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from felica_parse import (
    demo_nanaco,
    demo_suica,
    lookup_station,
    nanaco_balance,
    parse_history_block,
    parse_history_blocks,
    suica_balance,
    suica_balance_090f,
)


def main() -> int:
    fails = 0

    def expect(cond, msg):
        nonlocal fails
        if not cond:
            print("FAIL:", msg)
            fails += 1

    bal = suica_balance_090f(bytes([0xB0, 0x09]) + bytes(14))
    expect(bal == 2480, f"balance 2480 got {bal}")

    # Field report: 090f looked like ¥50945 while 008b had ~¥5094
    inflated = bytes([0xE1, 0xC6]) + bytes(14)
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
    expect(len(parse_history_blocks([block])) == 1, "history list")

    if fails:
        print(fails, "failure(s)")
        return 1
    print("suica_parse tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
