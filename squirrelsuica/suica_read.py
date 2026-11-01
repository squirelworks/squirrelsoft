#!/usr/bin/env python3
"""Read Suica / nanaco / Edy / WAON via nfcpy, or --demo fixtures."""

from __future__ import annotations

import argparse
import json
import sys
from typing import List, Optional

from felica_parse import (
    FelicaBrand,
    FelicaCard,
    brand_label,
    demo_edy,
    demo_nanaco,
    demo_suica,
    demo_waon,
    edy_balance,
    nanaco_balance,
    parse_history_blocks,
    suica_balance,
    waon_balance,
)

SYS_TRANSIT = 0x0003
SYS_COMMON = 0xFE00
SYS_NANACO = 0x04C7
SVC_SUICA_BAL = 0x090F
SVC_SUICA_INFO = 0x008B
SVC_SUICA_HIST = 0x010F
SVC_NANACO_BAL = 0x5597
SVC_EDY_BAL = 0x1317
SVC_WAON_BAL = 0x6817


def _read_block(tag, service: int, index: int = 0):
    import nfc.tag.tt3 as tt3

    sc = tt3.ServiceCode(service >> 6, service & 0x3F)
    bc = tt3.BlockCode(index, service=0)
    blocks = tag.read_without_encryption([sc], [bc])
    return bytes(blocks[0]) if blocks else None


def read_with_nfcpy(device: Optional[str] = None) -> FelicaCard:
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
    card_holder: List[Optional[FelicaCard]] = [None]

    def on_connect(tag):
        if not isinstance(tag, tt3.Type3Tag):
            print("not a FeliCa Type3 tag", file=sys.stderr)
            return False

        idm = "".join(f"{b:02x}" for b in tag.idm)
        pmm = "".join(f"{b:02x}" for b in getattr(tag, "pmm", b"") or b"")

        # Transit
        try:
            b090f = _read_block(tag, SVC_SUICA_BAL)
            b008b = None
            try:
                b008b = _read_block(tag, SVC_SUICA_INFO)
            except Exception:
                pass
            bal = suica_balance(b090f, b008b)
            if bal is not None:
                hist: List[bytes] = []
                try:
                    sc = tt3.ServiceCode(SVC_SUICA_HIST >> 6, SVC_SUICA_HIST & 0x3F)
                    bcs = [tt3.BlockCode(i, service=0) for i in range(5)]
                    bl = tag.read_without_encryption([sc], bcs)
                    hist = [bytes(x) for x in bl] if bl else []
                except Exception:
                    pass
                card_holder[0] = FelicaCard(
                    brand=FelicaBrand.SUICA_FAMILY.value,
                    brand_label=brand_label(FelicaBrand.SUICA_FAMILY),
                    idm=idm,
                    pmm=pmm,
                    system_code=f"{SYS_TRANSIT:04x}",
                    balance_yen=bal,
                    history=parse_history_blocks(hist),
                    note="Live transit IC",
                )
                return False
        except Exception as ex:
            print(f"transit probe: {ex}", file=sys.stderr)

        # Common-area purses
        for brand, svc, parser, label_note in (
            (FelicaBrand.NANACO, SVC_NANACO_BAL, nanaco_balance, "Live nanaco"),
            (FelicaBrand.EDY, SVC_EDY_BAL, edy_balance, "Live Edy"),
            (FelicaBrand.WAON, SVC_WAON_BAL, waon_balance, "Live WAON"),
        ):
            try:
                block = _read_block(tag, svc)
                if block:
                    card_holder[0] = FelicaCard(
                        brand=brand.value,
                        brand_label=brand_label(brand),
                        idm=idm,
                        pmm=pmm,
                        system_code=f"{SYS_COMMON:04x}",
                        balance_yen=parser(block),
                        history=[],
                        note=label_note,
                    )
                    return False
            except Exception:
                continue

        card_holder[0] = FelicaCard(
            brand=FelicaBrand.UNKNOWN.value,
            brand_label=brand_label(FelicaBrand.UNKNOWN),
            idm=idm,
            pmm=pmm,
            system_code="????",
            balance_yen=None,
            history=[],
            note="Unsupported purse — demos/fixtures cover known layouts",
        )
        return False

    clf.connect(rdwr={"on-connect": on_connect})
    clf.close()
    if not card_holder[0]:
        raise SystemExit("no card data")
    return card_holder[0]


def format_text(card: FelicaCard) -> str:
    bal = f"¥{card.balance_yen}" if card.balance_yen is not None else "(unknown)"
    lines = [
        f"Brand {card.brand_label}",
        f"IDm {card.idm}",
        f"PMm {card.pmm}",
        f"System {card.system_code}",
        f"Balance {bal}",
        f"Note {card.note}",
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
    ap = argparse.ArgumentParser(description="SquirrelSuica multi-brand FeliCa reader")
    ap.add_argument(
        "--demo",
        choices=["suica", "nanaco", "edy", "waon"],
        nargs="?",
        const="suica",
        help="synthetic demo purse (default suica)",
    )
    ap.add_argument("--json", action="store_true", help="print JSON")
    ap.add_argument("--device", default=None, help="nfcpy device path (default usb)")
    args = ap.parse_args(argv)

    if args.demo:
        card = {
            "suica": demo_suica,
            "nanaco": demo_nanaco,
            "edy": demo_edy,
            "waon": demo_waon,
        }[args.demo]()
    else:
        try:
            card = read_with_nfcpy(args.device)
        except SystemExit:
            raise
        except Exception as e:
            print(
                f"Hardware read failed ({e}). Try: python3 suica_read.py --demo nanaco",
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
