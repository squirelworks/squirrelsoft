# Suica station-code table

`StationCode.csv` maps Saibane (サイバネ) region/line/station codes found in
Suica/PASMO-class FeliCa history blocks to Japanese station names.

## Columns

| Column | Meaning |
|--------|---------|
| `area` | Region / 地区コード (hex): 0 JR+Kanto private, 1 Chubu, 2 Kansai/Okinawa, 3 other |
| `line` | Line / 線区コード (hex) |
| `station` | Station order / 駅順コード (hex) |
| `company` | Operator name |
| `line_name` | Line name |
| `station_name` | Station name |

## Provenance

Community-maintained dump of the public SFCardFan サイバネ駅コード listing
(originally at denno.net/SFCardFan), mirrored at
[MasanoriYONO/StationCode](https://github.com/MasanoriYONO/StationCode).
Not an official JR/Pasmo publication. Deduplicated and header-normalized for
this repo; names are used as-is for read-only display.

## Layout note

History service `0x090f` 16-byte blocks carry enter line/station at bytes 6–7,
exit line/station at 8–9, and a packed region byte at 15 (bits 7–6 enter,
bits 5–4 exit). JR lines (`line < 0x80`) always look up under area `0`.
