# Suica station-code table

`StationCode.csv` maps Saibane (サイバネ) region/line/station codes found in
Suica/PASMO-class FeliCa history blocks to Japanese station names, with
optional notes (transfer codes, former codes, temporary stations).

## Columns

| Column | Meaning |
|--------|---------|
| `area` | Region / 地区コード (hex): 0 JR+Kanto private, 1 Chubu, 2 Kansai/Okinawa, 3 other |
| `line` | Line / 線区コード (hex) |
| `station` | Station order / 駅順コード (hex) |
| `company` | Operator name |
| `line_name` | Line name |
| `station_name` | Station name |
| `note` | Optional remark (連絡駅 / 旧コード / 臨時駅); empty when unused |

## Provenance and licence

Adapted from
[soltia48/suica-viewer](https://github.com/soltia48/suica-viewer)
`assets/station_codes.csv` at commit
`df256abd66b62c2d9681dbb29519eb9315205f84`, which that project publishes under
the **MIT License** (Copyright (c) 2025 KIRISHIKI Yudai). MIT permits use,
copy, modification, and redistribution (including commercial) provided the
copyright notice and permission notice are retained.

Headers are normalized to the English names above; the upstream `備考` field
is kept as `note` (ASCII commas inside notes are folded to `、` so the file
stays unquoted). Deduplicated on `(area, line, station)`; the first row wins.
Not an official JR/Pasmo publication; names are used as-is for read-only
display.

The previous SFCardFan / denno.net community dump is **not** used here: its
maintainers disallow paid redistribution, so it cannot ship in this repo.

## Layout note

History service `0x090f` 16-byte blocks carry enter line/station at bytes 6–7,
exit line/station at 8–9, and a packed region byte at 15 (bits 7–6 enter,
bits 5–4 exit). JR lines (`line < 0x80`) always look up under area `0`.

Bus rows (terminal type `0x05`, or process `0x0D` / `0x0F` / `0x1F` / `0x23`)
use bytes 6–9 as a 16-bit operator code and 16-bit stop code instead; they are
never looked up in this station table.

## Issuer labels (attributes `0x008B`)

Attributes service `0x008B` byte 8 is publicly documented as card-type (high
nibble) and region (low nibble): type `3` → ICOCA, type `0` → IruCa, type `2`
→ Suica / PASMO / TOICA / PiTaPa. History settlement byte 2 can refine type-2
cards (`0x0D` PASMO, `0x3F` モバイルSuica, `0x0B` PiTaPa, `0x02` Suica). These
are display labels only; the brand id stays `suica_family`.
