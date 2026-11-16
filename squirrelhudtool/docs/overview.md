# SquirrelHUDTool — design note

**Lane:** Soft tooling only (Steve). Display hardware + page requirements SoT
live in ISM (`fighter-jet/avionics/hmi-hud.md`, issue #82). Soft does not design
weapons or SMS — HUD may show stores station flags as symbology only.

## Purpose

Author HUD and MFD/LAD page layouts and export a stable interchange
(`.sqhud` text + JSON) that mission-computer stubs and Flight Lab can load.

## Architecture

```
ISM hmi-hud.md (crew interface assumptions)
        │
        ▼
ht_page   templates (new-hud / new-mfd) + SQHUD parse
        │
        ├─ ht_export  JSON / text
        └─ main.c     CLI: new-hud, new-mfd, export, demo
```

## Interchange

- Text header `SQHUD page 1` with `type`, `name`, `width`, `height`, then
  `element id: kind=… x=… y=… w=… h=…` (optional `station=` / `state=`).
- JSON: `{ "format": "sqhud", "version": 1, "type", "name", "width", "height",
  "elements": [ … ] }`.

## Fixtures vs SoT

CI fixtures under `fixtures/pages/` are Soft authoring samples aligned to the
ISM HMI note. Refresh when Victor freezes page requirements.
