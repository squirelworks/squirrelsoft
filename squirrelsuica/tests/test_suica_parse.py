#!/usr/bin/env python3
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from suica_parse import demo_card, parse_balance_block, parse_history_block


def main() -> int:
    fails = 0

    def expect(cond, msg):
        nonlocal fails
        if not cond:
            print("FAIL:", msg)
            fails += 1

    bal = parse_balance_block(bytes([0xB0, 0x09]) + bytes(14))
    expect(bal == 2480, f"balance 2480 got {bal}")

    card = demo_card()
    expect(card.balance_yen == 2480, "demo balance")
    expect(len(card.history) >= 1, "demo history")
    expect(len(card.idm) == 16, "idm hex length")

    empty = parse_history_block(bytes(16))
    expect(empty is None, "empty history")

    if fails:
        print(fails, "failure(s)")
        return 1
    print("suica_parse tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
