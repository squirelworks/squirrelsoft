"""Multi-brand FeliCa purse parsers (fixture-testable, no hardware required)."""

from __future__ import annotations

import csv
from dataclasses import asdict, dataclass
from enum import Enum
from functools import lru_cache
from pathlib import Path
from typing import Dict, List, Optional, Tuple


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
    enter_station: Optional[str] = None
    exit_station: Optional[str] = None
    enter_code: Optional[str] = None
    exit_code: Optional[str] = None
    terminal: int = 0
    process: int = 0

    def station_summary(self) -> str:
        enter, exit_ = self.enter_station, self.exit_station
        if enter and exit_ and enter != exit_:
            return f"{enter} → {exit_}"
        return enter or exit_ or ""


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
    """Balance from Suica history block at bytes 10-11 LE (not 0-1)."""
    if len(block) < 12:
        raise ValueError("short")
    return _u16le(block, 10)


def suica_balance_008b(block: bytes) -> int:
    if len(block) < 13:
        raise ValueError("short")
    return _u16le(block, 11)


def suica_balance(block_090f: Optional[bytes], block_008b: Optional[bytes]) -> Optional[int]:
    a = suica_balance_090f(block_090f) if block_090f and len(block_090f) >= 12 else None
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


def _format_code(line: int, station: int) -> str:
    return f"{line & 0xFF:02X}/{station & 0xFF:02X}"


def _area_code(region_byte: int, line: int, for_exit: bool) -> int:
    if (line & 0xFF) < 0x80:
        return 0
    shift = 4 if for_exit else 6
    return (region_byte >> shift) & 0x3


@lru_cache(maxsize=1)
def _station_table() -> Dict[Tuple[int, int, int], Tuple[str, str, str]]:
    """Load SFCardFan-style CSV shipped under squirrelsuica/data/."""
    here = Path(__file__).resolve().parent
    path = here / "data" / "StationCode.csv"
    out: Dict[Tuple[int, int, int], Tuple[str, str, str]] = {}
    if not path.is_file():
        return out
    with path.open(encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            try:
                area = int(row["area"], 16)
                line = int(row["line"], 16)
                station = int(row["station"], 16)
            except (KeyError, ValueError):
                continue
            key = (area, line, station)
            if key in out:
                continue
            out[key] = (
                (row.get("company") or "").strip(),
                (row.get("line_name") or "").strip(),
                (row.get("station_name") or "").strip(),
            )
    return out


def lookup_station(area: int, line: int, station: int) -> Optional[str]:
    if line == 0 and station == 0:
        return None
    row = _station_table().get((area & 0xFF, line & 0xFF, station & 0xFF))
    return row[2] if row else None


def resolve_rail(region_byte: int, line: int, station: int, for_exit: bool) -> Optional[str]:
    if line == 0 and station == 0:
        return None
    area = _area_code(region_byte, line, for_exit)
    name = lookup_station(area, line, station)
    if name:
        return name
    if area != 0:
        return lookup_station(0, line, station)
    return None


def _process_label(process: int) -> str:
    return {
        0x01: "運賃支払",
        0x02: "チャージ",
        0x03: "券購",
        0x04: "精算",
        0x05: "入場精算",
        0x06: "窓出",
        0x0D: "バス",
        0x0F: "バス",
        0x13: "新幹線",
        0x14: "入A",
        0x15: "出A",
        0x46: "物販",
        0x48: "特典チャージ",
    }.get(process, f"proc=0x{process:02x}")


def parse_history_block(block: bytes) -> Optional[HistoryEntry]:
    if len(block) < 16 or all(x == 0 for x in block):
        return None
    terminal = block[0]
    process = block[1]
    date_word = (block[4] << 8) | block[5]
    year = 2000 + (date_word >> 9)
    month = (date_word >> 5) & 0xF
    day = date_word & 0x1F
    if not (1 <= month <= 12 and 1 <= day <= 31 and 2000 <= year <= 2099):
        try:
            year = 2000 + _bcd_byte(block[4])
            month = _bcd_byte(block[5])
            day = _bcd_byte(block[6])
        except Exception:
            year, month, day = 0, 0, 0
    date = f"{year:04d}-{month:02d}-{day:02d}" if year else "????-??-??"

    is_product = terminal in (0xC7, 0xC8)
    is_bus = terminal == 0x05 or process in (0x0D, 0x0F, 0x1F, 0x23)
    is_charge = process in (0x02, 0x48)

    if is_charge:
        kind, base_note = "charge", "チャージ"
    elif is_bus:
        kind, base_note = "bus", _process_label(process)
    elif is_product or process in (0x46, 0x49, 0x4A, 0x4B, 0xC6, 0xCB):
        kind, base_note = "sale", _process_label(process)
    elif process in (0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x13, 0x14, 0x15, 0x84, 0x85):
        kind, base_note = "train", _process_label(process)
    else:
        kind, base_note = "other", f"proc=0x{process:02x}"

    enter_name = exit_name = enter_code = exit_code = None
    note = base_note
    if not is_product and not is_bus:
        region = block[15]
        in_line, in_sta = block[6], block[7]
        out_line, out_sta = block[8], block[9]
        if in_line or in_sta:
            enter_code = _format_code(in_line, in_sta)
            enter_name = resolve_rail(region, in_line, in_sta, for_exit=False) or enter_code
        if not is_charge and (out_line or out_sta):
            exit_code = _format_code(out_line, out_sta)
            exit_name = resolve_rail(region, out_line, out_sta, for_exit=True) or exit_code
        route = None
        if enter_name and exit_name:
            route = f"{enter_name} → {exit_name}"
        elif enter_name:
            route = enter_name
        elif exit_name:
            route = exit_name
        if route:
            note = f"{base_note} · {route}"
    elif is_product:
        time_word = (block[6] << 8) | block[7]
        hh = time_word >> 11
        mm = (time_word >> 5) & 0x3F
        if 0 <= hh <= 23 and 0 <= mm <= 59:
            note = f"{base_note} · {hh:02d}:{mm:02d}"

    bal = _u16le(block, 10)
    if bal > 100_000:
        bal = 0
    return HistoryEntry(
        date,
        kind,
        0,
        bal,
        note,
        block.hex(),
        enter_name,
        exit_name,
        enter_code,
        exit_code,
        terminal,
        process,
    )


def parse_history_blocks(blocks: List[bytes]) -> List[HistoryEntry]:
    parsed = [e for b in blocks if (e := parse_history_block(b))]
    out: List[HistoryEntry] = []
    for i, e in enumerate(parsed):
        older = parsed[i + 1] if i + 1 < len(parsed) else None
        fare = 0
        if older is not None:
            delta = older.balance_yen - e.balance_yen
            if 1 <= delta <= 20_000:
                fare = delta
        out.append(
            HistoryEntry(
                e.date,
                e.kind,
                fare,
                e.balance_yen,
                e.note,
                e.raw_hex,
                e.enter_station,
                e.exit_station,
                e.enter_code,
                e.exit_code,
                e.terminal,
                e.process,
            )
        )
    return out



def _u32be(b: bytes, off: int = 0) -> int:
    return (b[off] << 24) | (b[off + 1] << 16) | (b[off + 2] << 8) | b[off + 3]


def parse_nanaco_history_block(block: bytes) -> Optional[HistoryEntry]:
    if len(block) < 16 or all(x == 0 for x in block):
        return None
    typ = block[0]
    if typ == 0:
        return None
    amount = _u32be(block, 1)
    bal = max(0, min(500_000, _u32be(block, 5)))
    packed = (block[9] << 24) | (block[10] << 16) | (block[11] << 8) | block[12]
    year = 2000 + ((packed >> 21) & 0x7FF)
    month = (packed >> 17) & 0xF
    day = (packed >> 12) & 0x1F
    hour = (packed >> 6) & 0x3F
    minute = packed & 0x3F
    date = f"{year:04d}-{month:02d}-{day:02d}" if 1 <= month <= 12 and 1 <= day <= 31 else "????-??-??"
    if typ == 0x47:
        kind, base = "sale", "支払"
    elif typ in (0x5C, 0x6F, 0x70):
        kind, base = "charge", "チャージ"
    elif typ == 0x83:
        kind, base = "other", "ポイント交換"
    elif typ == 0x35:
        kind, base = "other", "転送"
    else:
        kind, base = "other", f"type=0x{typ:02x}"
    note = f"{base} · {hour:02d}:{minute:02d} · ¥{amount}" if hour <= 23 and minute <= 59 else f"{base} · ¥{amount}"
    return HistoryEntry(date, kind, amount if kind == "sale" and 1 <= amount <= 100_000 else 0, bal, note, block.hex(), terminal=typ, process=typ)


def parse_nanaco_history_blocks(blocks: List[bytes]) -> List[HistoryEntry]:
    return [e for b in blocks if (e := parse_nanaco_history_block(b))]


def parse_edy_history_block(block: bytes) -> Optional[HistoryEntry]:
    if len(block) < 16 or all(x == 0 for x in block):
        return None
    typ = block[0]
    if typ == 0:
        return None
    ts = _u32be(block, 4)
    days = ts >> 17
    seconds = ts & 0x1FFFF
    # days since 2000-01-01
    from datetime import date, timedelta
    try:
        d = date(2000, 1, 1) + timedelta(days=days)
        date_s = d.isoformat()
    except Exception:
        date_s = "????-??-??"
    hour = seconds // 3600
    minute = (seconds % 3600) // 60
    amount = _u32be(block, 8)
    bal = max(0, min(500_000, _u32be(block, 12)))
    if typ == 0x20:
        kind, base = "sale", "支払"
    elif typ == 0x02:
        kind, base = "charge", "チャージ"
    elif typ == 0x04:
        kind, base = "charge", "ギフト"
    else:
        kind, base = "other", f"type=0x{typ:02x}"
    note = f"{base} · {hour:02d}:{minute:02d} · ¥{amount}"
    return HistoryEntry(date_s, kind, amount if kind == "sale" and 1 <= amount <= 100_000 else 0, bal, note, block.hex(), terminal=typ, process=typ)


def parse_edy_history_blocks(blocks: List[bytes]) -> List[HistoryEntry]:
    return [e for b in blocks if (e := parse_edy_history_block(b))]


def hist_rail(
    terminal: int,
    process: int,
    y: int,
    m: int,
    d: int,
    in_line: int,
    in_sta: int,
    out_line: int,
    out_sta: int,
    bal: int,
    region: int = 0,
) -> bytes:
    date_word = (((y - 2000) & 0x7F) << 9) | ((m & 0xF) << 5) | (d & 0x1F)
    return bytes(
        [
            terminal & 0xFF,
            process & 0xFF,
            0,
            0,
            (date_word >> 8) & 0xFF,
            date_word & 0xFF,
            in_line & 0xFF,
            in_sta & 0xFF,
            out_line & 0xFF,
            out_sta & 0xFF,
            bal & 0xFF,
            (bal >> 8) & 0xFF,
            0,
            0,
            0,
            region & 0xFF,
        ]
    )


def demo_suica() -> FelicaCard:
    return FelicaCard(
        brand=FelicaBrand.SUICA_FAMILY.value,
        brand_label=brand_label(FelicaBrand.SUICA_FAMILY),
        idm="0101020304050607",
        pmm="0000000000000000",
        system_code="0003",
        balance_yen=2480,
        history=parse_history_blocks(
            [
                hist_rail(0x16, 0x01, 2026, 10, 28, 0xE3, 0x59, 0xE3, 0x5E, 2480),
                hist_rail(0x16, 0x01, 2026, 10, 27, 0x25, 0x07, 0x25, 0x0A, 2648),
                hist_rail(0x16, 0x02, 2026, 10, 26, 0x01, 0x01, 0, 0, 2858),
            ]
        ),
        note="Demo Suica (synthetic)",
    )


def _hist_nanaco(typ: int, amount: int, bal: int, y: int, m: int, d: int, hh: int, mm: int) -> bytes:
    packed = (((y - 2000) & 0x7FF) << 21) | ((m & 0xF) << 17) | ((d & 0x1F) << 12) | ((hh & 0x3F) << 6) | (mm & 0x3F)
    out = bytearray(16)
    out[0] = typ & 0xFF
    out[1:5] = amount.to_bytes(4, "big")
    out[5:9] = bal.to_bytes(4, "big")
    out[9:13] = packed.to_bytes(4, "big")
    return bytes(out)


def demo_nanaco() -> FelicaCard:
    hist = [
        _hist_nanaco(0x47, 480, 5200, 2026, 10, 28, 18, 42),
        _hist_nanaco(0x6F, 3000, 5680, 2026, 10, 27, 12, 5),
    ]
    return FelicaCard(
        brand=FelicaBrand.NANACO.value,
        brand_label=brand_label(FelicaBrand.NANACO),
        idm="02fe0004c7000001",
        pmm="0000000000000000",
        system_code="fe00",
        balance_yen=nanaco_balance(bytes([0x50, 0x14, 0x00, 0x00]) + bytes(12)),
        history=parse_nanaco_history_blocks(hist),
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
