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
    settlement: int = 0
    bus_operator: Optional[int] = None
    bus_stop: Optional[int] = None

    def station_summary(self) -> str:
        enter, exit_ = self.enter_station, self.exit_station
        if enter and exit_ and enter != exit_:
            return f"{enter} → {exit_}"
        return enter or exit_ or ""

    def bus_summary(self) -> str:
        if self.bus_operator is None and self.bus_stop is None:
            return ""
        op = f"{self.bus_operator:04X}" if self.bus_operator is not None else "----"
        st = f"{self.bus_stop:04X}" if self.bus_stop is not None else "----"
        return f"事業者 {op} · 停留所 {st}"


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


def suica_family_label(
    attrs_008b: Optional[bytes] = None,
    settlement: Optional[int] = None,
) -> str:
    """Transit-IC display name from attributes 0x008B issuer byte and optional history settlement.

    Public layout (jennychan / WDIC notes): attributes byte 8 is
    card-type (high nibble) | region (low nibble). Type 3 is ICOCA, type 0 is
    IruCa/せたまる, type 2 covers Suica/PASMO/TOICA/PiTaPa. Settlement byte 2 of
    a history block can refine type-2 cards (0x0D PASMO, 0x3F モバイルSuica,
    0x0B PiTaPa, 0x02 VIEW/Suica). Synthetic fixtures only — never real dumps.
    """
    card_type = region = None
    if attrs_008b and len(attrs_008b) > 8:
        b = attrs_008b[8]
        card_type = (b >> 4) & 0xF
        region = b & 0xF
    if card_type == 3:
        return "ICOCA"
    if card_type == 0:
        return "IruCa"
    settle = settlement if settlement is not None else None
    if settle == 0x0D:
        return "PASMO"
    if settle == 0x3F:
        return "モバイルSuica"
    if settle == 0x0B:
        return "PiTaPa"
    if settle == 0x02:
        return "Suica"
    if card_type == 2 and region == 2:
        return "PiTaPa"
    if card_type == 2:
        return "Suica / PASMO"
    return brand_label(FelicaBrand.SUICA_FAMILY)


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
        0x1F: "バスチャージ",
        0x23: "バス券購",
        0x46: "物販",
        0x48: "特典チャージ",
    }.get(process, f"proc=0x{process:02x}")


_SALE_PROCESSES = (0x46, 0x49, 0x4A, 0x4B, 0xC6, 0xCB)
_BUS_PROCESSES = (0x0D, 0x0F, 0x1F, 0x23)


def parse_history_block(block: bytes) -> Optional[HistoryEntry]:
    if len(block) < 16 or all(x == 0 for x in block):
        return None
    terminal = block[0]
    process = block[1]
    settlement = block[2]
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

    # Shop sales carry a time and a terminal ID in bytes 6-9, not stations. Seen
    # on terminal types C7/C8 and, in real Suica dumps, CA with process 46 (S1.3).
    # Bus (terminal 0x05 or bus process): bytes 6-9 are operator + stop codes, never rail.
    is_bus = terminal == 0x05 or process in _BUS_PROCESSES
    is_product = terminal in (0xC7, 0xC8) or (process in _SALE_PROCESSES and not is_bus)
    is_charge = process in (0x02, 0x48)

    if is_charge:
        kind, base_note = "charge", "チャージ"
    elif is_bus:
        kind, base_note = "bus", _process_label(process)
    elif is_product:
        kind, base_note = "sale", _process_label(process)
    elif process in (0x01, 0x03, 0x04, 0x05, 0x06, 0x08, 0x13, 0x14, 0x15, 0x84, 0x85):
        kind, base_note = "train", _process_label(process)
    else:
        kind, base_note = "other", f"proc=0x{process:02x}"

    enter_name = exit_name = enter_code = exit_code = None
    bus_operator = bus_stop = None
    note = base_note
    if is_bus:
        # Public layout: bytes 6-7 bus operator (BE), 8-9 stop (BE). No station table.
        bus_operator = (block[6] << 8) | block[7]
        bus_stop = (block[8] << 8) | block[9]
        note = f"{base_note} · 事業者 {bus_operator:04X} · 停留所 {bus_stop:04X}"
    elif not is_product:
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
        settlement,
        bus_operator,
        bus_stop,
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
            elif e.kind == "charge" and 1 <= (-delta) <= 50_000:
                fare = -delta
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
                e.settlement,
                e.bus_operator,
                e.bus_stop,
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


def parse_waon_history_block(record32: bytes) -> Optional[HistoryEntry]:
    """WAON history service 0x680b — one 32-byte record (two 16-byte FeliCa blocks).

    Public bit-pack from byte 18: year5(+2005) month4 day5 hour5 minute6
    balance18 usage18 charge17; type at byte 17.
    """
    if len(record32) < 32 or all(x == 0 for x in record32):
        return None
    typ = record32[17]
    if typ == 0:
        return None
    bit = 0

    def take(n: int) -> int:
        nonlocal bit
        v = 0
        for _ in range(n):
            byte_index = 18 + (bit // 8)
            bit_index = 7 - (bit % 8)
            b = record32[byte_index]
            v = (v << 1) | ((b >> bit_index) & 1)
            bit += 1
        return v

    year = 2005 + take(5)
    month = take(4)
    day = take(5)
    hour = take(5)
    minute = take(6)
    bal = max(0, min(500_000, take(18)))
    usage = take(18)
    charge = take(17)
    date = (
        f"{year:04d}-{month:02d}-{day:02d}"
        if 2005 <= year <= 2099 and 1 <= month <= 12 and 1 <= day <= 31
        else "????-??-??"
    )
    if typ == 0x04:
        kind, base, fare = "sale", "支払", usage
    elif typ in (0x0C, 0x10):
        kind, base, fare = "charge", "チャージ", charge
    else:
        kind, base, fare = "other", f"type=0x{typ:02x}", usage
    note = f"{base} · {min(hour, 23):02d}:{min(minute, 59):02d} · ¥{fare}"
    return HistoryEntry(
        date,
        kind,
        fare if kind == "sale" and 1 <= fare <= 100_000 else 0,
        bal,
        note,
        record32.hex(),
        terminal=typ,
        process=typ,
    )


def parse_waon_history_blocks(blocks: List[bytes]) -> List[HistoryEntry]:
    """Parse paired 16-byte FeliCa blocks from WAON service 0x680b into entries."""
    out: List[HistoryEntry] = []
    i = 0
    while i + 1 < len(blocks):
        rec = bytes(blocks[i]) + bytes(blocks[i + 1])
        if e := parse_waon_history_block(rec):
            out.append(e)
        i += 2
    return out


def hist_waon(
    typ: int,
    y: int,
    m: int,
    d: int,
    hh: int,
    mm: int,
    bal: int,
    usage: int,
    charge: int,
) -> bytes:
    """Build a synthetic 32-byte WAON history record (fixture helper)."""
    out = bytearray(32)
    out[17] = typ & 0xFF
    fields = [
        ((y - 2005) & 0x1F, 5),
        (m & 0xF, 4),
        (d & 0x1F, 5),
        (hh & 0x1F, 5),
        (mm & 0x3F, 6),
        (bal & 0x3FFFF, 18),
        (usage & 0x3FFFF, 18),
        (charge & 0x1FFFF, 17),
    ]
    bit = 0
    for value, width in fields:
        for b in range(width - 1, -1, -1):
            byte_index = 18 + (bit // 8)
            bit_index = 7 - (bit % 8)
            if (value >> b) & 1:
                out[byte_index] |= 1 << bit_index
            bit += 1
    return bytes(out)


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


def demo_suica_attrs(card_type: int = 2, region: int = 0, bal: int = 2480) -> bytes:
    """Synthetic attributes 0x008B block (issuer nibble + balance)."""
    out = bytearray(16)
    out[8] = ((card_type & 0xF) << 4) | (region & 0xF)
    out[11] = bal & 0xFF
    out[12] = (bal >> 8) & 0xFF
    return bytes(out)


def demo_suica() -> FelicaCard:
    hist = parse_history_blocks(
        [
            hist_rail(0x16, 0x01, 2026, 10, 28, 0xE3, 0x59, 0xE3, 0x5E, 2480),
            hist_rail(0x16, 0x01, 2026, 10, 27, 0x25, 0x07, 0x25, 0x0A, 2648),
            hist_rail(0x16, 0x02, 2026, 10, 26, 0x01, 0x01, 0, 0, 2858),
            hist_rail(0x16, 0x01, 2026, 10, 25, 0x25, 0x01, 0x25, 0x02, 858),
        ]
    )
    attrs = demo_suica_attrs(2, 0, 2480)
    settle = hist[0].settlement if hist else None
    return FelicaCard(
        brand=FelicaBrand.SUICA_FAMILY.value,
        brand_label=suica_family_label(attrs, settle),
        idm="0101020304050607",
        pmm="0000000000000000",
        system_code="0003",
        balance_yen=2480,
        history=hist,
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
    rec = hist_waon(0x04, 2026, 10, 28, 19, 10, 4584, 620, 0)
    return FelicaCard(
        brand=FelicaBrand.WAON.value,
        brand_label=brand_label(FelicaBrand.WAON),
        idm="02fe006817000001",
        pmm="0000000000000000",
        system_code="fe00",
        balance_yen=waon_balance(bytes([0xE8, 0x11]) + bytes(14)),
        history=parse_waon_history_blocks([rec[:16], rec[16:]]),
        note="Demo WAON (synthetic)",
    )
