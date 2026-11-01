#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from felica_parse import (
    demo_nanaco,
    demo_suica,
    nanaco_balance,
    parse_history_block,
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

    if fails:
        print(fails, "failure(s)")
        return 1
    print("suica_parse tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
