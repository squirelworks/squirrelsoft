#!/usr/bin/env python3
"""Read Suica/PASMO-class FeliCa cards (balance + history) via nfcpy or --demo."""

from __future__ import annotations

import argparse
import json
import sys
from typing import List, Optional

from suica_parse import SuicaCard, demo_card, parse_balance_block, parse_history_blocks

SYSTEM_CODE = 0x0003
SERVICE_BALANCE = 0x090F
HISTORY_SERVICES = (0x010F, 0x090F, 0x008B)


def read_with_nfcpy(device: Optional[str] = None) -> SuicaCard:
    try:
        import nfc
        import nfc.tag.tt3 as tt3
    except ImportError as e:
        raise SystemExit(
            "nfcpy is required for hardware reads. Install: python3 -m pip install --user nfcpy"
        ) from e

    path = device or "usb"
    clf = nfc.ContactlessFrontend(path)
    if not clf:
        raise SystemExit(f"no NFC reader on '{path}'")

    print(f"Waiting for FeliCa card on {path}…", file=sys.stderr)
    card_holder: List[Optional[SuicaCard]] = [None]

    def on_connect(tag):
        if not isinstance(tag, tt3.Type3Tag):
            print("not a FeliCa Type3 tag", file=sys.stderr)
            return False

        idm = tag.idm
        pmm = getattr(tag, "pmm", b"")
        bal = 0
        hist_blocks: List[bytes] = []

        try:
            sc = tt3.ServiceCode(SERVICE_BALANCE >> 6, SERVICE_BALANCE & 0x3F)
            bc = tt3.BlockCode(0, service=0)
            blocks = tag.read_without_encryption([sc], [bc])
            if blocks:
                bal = parse_balance_block(bytes(blocks[0]))
        except Exception as ex:
            print(f"balance read warning: {ex}", file=sys.stderr)

        for svc in HISTORY_SERVICES:
            try:
                sc = tt3.ServiceCode(svc >> 6, svc & 0x3F)
                bcs = [tt3.BlockCode(i, service=0) for i in range(20)]
                bl = tag.read_without_encryption([sc], bcs)
                if bl:
                    hist_blocks = [bytes(x) for x in bl]
                    if any(any(b) for b in hist_blocks):
                        break
            except Exception:
                continue

        card_holder[0] = SuicaCard(
            idm="".join(f"{b:02x}" for b in idm),
            pmm="".join(f"{b:02x}" for b in pmm) if pmm else "",
            system_code=f"{SYSTEM_CODE:04x}",
            balance_yen=bal,
            history=parse_history_blocks(hist_blocks),
        )
        return False

    clf.connect(rdwr={"on-connect": on_connect})
    clf.close()
    if not card_holder[0]:
        raise SystemExit("no card data")
    return card_holder[0]


def format_text(card: SuicaCard) -> str:
    lines = [
        f"IDm {card.idm}",
        f"PMm {card.pmm}",
        f"System {card.system_code}",
        f"Balance ¥{card.balance_yen}",
        "History:",
    ]
    if not card.history:
        lines.append("  (none)")
    for i, h in enumerate(card.history, 1):
        lines.append(
            f"  {i}. {h.date}  {h.kind:6}  fare ¥{h.fare_yen}  bal ¥{h.balance_yen}  {h.note}"
        )
    return "\n".join(lines)


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="SquirrelSuica FeliCa reader")
    ap.add_argument("--demo", action="store_true", help="use canned demo card")
    ap.add_argument("--json", action="store_true", help="print JSON")
    ap.add_argument("--device", default=None, help="nfcpy device path (default usb)")
    args = ap.parse_args(argv)

    if args.demo:
        card = demo_card()
    else:
        try:
            card = read_with_nfcpy(args.device)
        except SystemExit:
            raise
        except Exception as e:
            print(
                f"Hardware read failed ({e}). Try: python3 suica_read.py --demo",
                file=sys.stderr,
            )
            return 1

    if args.json:
        print(json.dumps(card.to_dict(), ensure_ascii=False, indent=2))
    else:
        print(format_text(card))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
