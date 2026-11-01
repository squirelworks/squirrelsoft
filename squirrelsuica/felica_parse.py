"""Multi-brand FeliCa purse parsers (fixture-testable, no hardware required)."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
from typing import List, Optional


class FelicaBrand(str, Enum):
    SUICA_FAMILY = "suica_family"
    NANACO = "nanaco"
    EDY = "edy"
    WAON = "waon"
    UNKNOWN = "unknown"


@dataclass
class HistoryEntry:
    date: str
    kind: str
    fare_yen: int
    balance_yen: int
    note: str
    raw_hex: str


@dataclass
class FelicaCard:
    brand: str
    brand_label: str
    idm: str
    pmm: str
    system_code: str
    balance_yen: Optional[int]
    history: List[HistoryEntry]
    note: str = ""

    def to_dict(self):
        return asdict(self)


def _u16le(b: bytes, off: int = 0) -> int:
    return b[off] | (b[off + 1] << 8)


def _u32le(b: bytes, off: int = 0) -> int:
    return b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)


def suica_balance_090f(block: bytes) -> int:
    if len(block) < 2:
        raise ValueError("short")
    return _u16le(block, 0)


def suica_balance_008b(block: bytes) -> int:
    if len(block) < 13:
        raise ValueError("short")
    return _u16le(block, 11)


def suica_balance(block_090f: Optional[bytes], block_008b: Optional[bytes]) -> Optional[int]:
    a = suica_balance_090f(block_090f) if block_090f and len(block_090f) >= 2 else None
    b = suica_balance_008b(block_008b) if block_008b and len(block_008b) >= 13 else None

    def ok(v: int) -> bool:
        return 0 <= v <= 50_000

    if a is not None and b is not None and ok(a) and ok(b) and a > b * 2 and b > 0:
        return b
    if a is not None and ok(a):
        return a
    if b is not None and ok(b):
        return b
    return a if a is not None else b


def nanaco_balance(block: bytes) -> int:
    return max(0, min(500_000, _u32le(block, 0)))


def edy_balance(block: bytes) -> int:
    return max(0, min(500_000, _u32le(block, 0)))


def waon_balance(block: bytes) -> int:
    return max(0, min(500_000, _u16le(block, 0)))


def brand_label(brand: FelicaBrand) -> str:
    return {
        FelicaBrand.SUICA_FAMILY: "Suica / PASMO",
        FelicaBrand.NANACO: "nanaco",
        FelicaBrand.EDY: "楽天Edy",
        FelicaBrand.WAON: "WAON",
        FelicaBrand.UNKNOWN: "Unknown FeliCa",
    }[brand]


def _bcd_byte(v: int) -> int:
    return ((v >> 4) & 0xF) * 10 + (v & 0xF)


def parse_history_block(block: bytes) -> Optional[HistoryEntry]:
    if len(block) < 16 or all(x == 0 for x in block):
        return None
    year = 2000 + (block[4] >> 1)
    month = ((block[4] & 1) << 3) | (block[5] >> 5)
    day = block[5] & 0x1F
    if not (1 <= month <= 12 and 1 <= day <= 31 and 2000 <= year <= 2099):
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
    fare = _u16le(block, 10)
    bal = _u16le(block, 12)
    if fare > 20000:
        fare = 0
    if bal > 100000:
        bal = 0
    return HistoryEntry(date, kind, fare, bal, note, block.hex())


def parse_history_blocks(blocks: List[bytes]) -> List[HistoryEntry]:
    return [e for b in blocks if (e := parse_history_block(b))]


def _pack_date(year: int, month: int, day: int) -> tuple[int, int]:
    y = year - 2000
    b4 = ((y << 1) | ((month >> 3) & 1)) & 0xFF
    b5 = (((month & 7) << 5) | (day & 0x1F)) & 0xFF
    return b4, b5


def _hist(process: int, y: int, m: int, d: int, fare: int, bal: int) -> bytes:
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


def demo_suica() -> FelicaCard:
    bal = bytes([0xB0, 0x09]) + bytes(14)
    return FelicaCard(
        brand=FelicaBrand.SUICA_FAMILY.value,
        brand_label=brand_label(FelicaBrand.SUICA_FAMILY),
        idm="0101020304050607",
        pmm="0000000000000000",
        system_code="0003",
        balance_yen=suica_balance_090f(bal),
        history=parse_history_blocks(
            [
                _hist(0x01, 2026, 10, 28, 168, 2480),
                _hist(0x01, 2026, 10, 27, 210, 2648),
                _hist(0x02, 2026, 10, 26, 0, 2858),
            ]
        ),
        note="Demo Suica (synthetic)",
    )


def demo_nanaco() -> FelicaCard:
    return FelicaCard(
        brand=FelicaBrand.NANACO.value,
        brand_label=brand_label(FelicaBrand.NANACO),
        idm="02fe0004c7000001",
        pmm="0000000000000000",
        system_code="fe00",
        balance_yen=nanaco_balance(bytes([0x50, 0x14, 0x00, 0x00]) + bytes(12)),
        history=[],
        note="Demo nanaco (synthetic)",
    )


def demo_edy() -> FelicaCard:
    return FelicaCard(
        brand=FelicaBrand.EDY.value,
        brand_label=brand_label(FelicaBrand.EDY),
        idm="02fe001317000001",
        pmm="0000000000000000",
        system_code="fe00",
        balance_yen=edy_balance(bytes([0x1C, 0x0C, 0x00, 0x00]) + bytes(12)),
        history=[],
        note="Demo Edy (synthetic)",
    )


def demo_waon() -> FelicaCard:
    return FelicaCard(
        brand=FelicaBrand.WAON.value,
        brand_label=brand_label(FelicaBrand.WAON),
        idm="02fe006817000001",
        pmm="0000000000000000",
        system_code="fe00",
        balance_yen=waon_balance(bytes([0xE8, 0x11]) + bytes(14)),
        history=[],
        note="Demo WAON (synthetic)",
    )
