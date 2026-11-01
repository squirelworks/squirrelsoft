"""Parse Suica/PASMO-class FeliCa balance and history blocks (read-only)."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import List, Optional


@dataclass
class HistoryEntry:
    date: str
    kind: str
    fare_yen: int
    balance_yen: int
    note: str
    raw_hex: str


@dataclass
class SuicaCard:
    idm: str
    pmm: str
    system_code: str
    balance_yen: int
    history: List[HistoryEntry]

    def to_dict(self):
        return asdict(self)


def _u16le(b: bytes, off: int = 0) -> int:
    return b[off] | (b[off + 1] << 8)


def parse_balance_block(block: bytes) -> int:
    """Balance service 0x090f block 0: yen as little-endian uint16 at [0:2]."""
    if len(block) < 2:
        raise ValueError("balance block too short")
    return _u16le(block, 0)


def _bcd_byte(v: int) -> int:
    return ((v >> 4) & 0xF) * 10 + (v & 0xF)


def parse_history_block(block: bytes) -> Optional[HistoryEntry]:
    """
    Best-effort decode of a 16-byte Suica history-style record.

    Layout follows commonly published open-reader notes (station codes omitted
    as region-specific tables). Empty/zero blocks return None.
    """
    if len(block) < 16:
        return None
    if all(x == 0 for x in block):
        return None

    # Date often packed in bytes 4..5 (region varies); keep robust fallbacks.
    raw_hex = block.hex()
    year = 2000 + (block[4] >> 1)
    month = ((block[4] & 1) << 3) | (block[5] >> 5)
    day = block[5] & 0x1F
    if not (1 <= month <= 12 and 1 <= day <= 31 and 2000 <= year <= 2099):
        # alternate: BCD date at 4..6
        try:
            year = 2000 + _bcd_byte(block[4])
            month = _bcd_byte(block[5])
            day = _bcd_byte(block[6])
        except Exception:
            year, month, day = 0, 0, 0
    date = f"{year:04d}-{month:02d}-{day:02d}" if year else "????-??-??"

    process = block[0]
    if process == 0x02:
        kind, note = "charge", "チャージ"
    elif process in (0x01, 0x0F, 0x46, 0x49):
        kind, note = "train", "入場/出場"
    elif process in (0x5C, 0xC7, 0xC8):
        kind, note = "bus", "バス"
    else:
        kind, note = "other", f"proc=0x{process:02x}"

    # Fare / balance commonly near end of block (LE uint16).
    fare = _u16le(block, 10)
    bal = _u16le(block, 12)
    # Guard nonsense
    if fare > 20000:
        fare = 0
    if bal > 100000:
        bal = parse_balance_block(block[0:2]) if False else bal

    return HistoryEntry(
        date=date, kind=kind, fare_yen=fare, balance_yen=bal, note=note, raw_hex=raw_hex
    )


def parse_history_blocks(blocks: List[bytes]) -> List[HistoryEntry]:
    out: List[HistoryEntry] = []
    for b in blocks:
        e = parse_history_block(b)
        if e:
            out.append(e)
    return out


def _pack_date(year: int, month: int, day: int) -> tuple[int, int]:
    """Pack into Suica-style date bytes (year offset from 2000)."""
    y = year - 2000
    b4 = ((y << 1) | ((month >> 3) & 1)) & 0xFF
    b5 = (((month & 7) << 5) | (day & 0x1F)) & 0xFF
    return b4, b5


def demo_card() -> SuicaCard:
    bal_block = bytes([0xB0, 0x09, 0x00, 0x00] + [0] * 12)  # 2480 yen

    def hist(process: int, y: int, m: int, d: int, fare: int, bal: int) -> bytes:
        b4, b5 = _pack_date(y, m, d)
        return bytes(
            [
                process & 0xFF,
                0,
                0,
                0,
                b4,
                b5,
                0,
                0,
                0,
                0,
                fare & 0xFF,
                (fare >> 8) & 0xFF,
                bal & 0xFF,
                (bal >> 8) & 0xFF,
                0,
                0,
            ]
        )

    h1 = hist(0x01, 2026, 10, 28, 168, 2480)
    h2 = hist(0x01, 2026, 10, 27, 210, 2648)
    h3 = hist(0x02, 2026, 10, 26, 0, 2858)
    return SuicaCard(
        idm="0101020304050607",
        pmm="0000000000000000",
        system_code="0003",
        balance_yen=parse_balance_block(bal_block),
        history=parse_history_blocks([h1, h2, h3]),
    )
