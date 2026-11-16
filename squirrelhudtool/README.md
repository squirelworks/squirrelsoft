# SquirrelHUDTool

**Status: Scaffolded**

Soft-side symbology / page layout tool for International Squirrel Machines
fighter **HUD** and **MFD** displays (Victor). CLI v0 authors one HUD page and
one MFD-style page and exports JSON or SQHUD text for mission-computer stubs /
Flight Lab viewers.

Steve / Soft owns tooling only — not display hardware. ISM display requirements:

https://github.com/squirelworks/international-squirrel-machines/blob/main/fighter-jet/avionics/hmi-hud.md

Tracked as [ISM #82](https://github.com/squirelworks/international-squirrel-machines/issues/82).

```
make -C squirrelhudtool
make -C squirrelhudtool test
./build/squirrelhudtool demo
```

## Commands

| Command | Purpose |
|---------|---------|
| `new-hud [--name NAME] [--out FILE]` | Author a primary-flight HUD page |
| `new-mfd [--name NAME] [--out FILE]` | Author an MFD / LAD-style page |
| `export --in FILE [--format json\|text] [--out FILE]` | Export consumable JSON or SQHUD text |
| `demo` | Author HUD+MFD templates and print JSON |

## Scope (v0)

- Page layout + symbology kinds (attitude, airspeed, altitude, heading, AOA,
  gear, radar/EO panes, caution, synoptics).
- Stores station **flags** as display symbology only (`empty` / `loaded` /
  `selected` / `unsafe`). No weapons release, inventory, or SMS internals.
- Fixtures under `fixtures/pages/`.

## Planned (not in v0)

- Interactive GUI editor on `sqgui`
- Live feed from Flight Lab / mission-computer stubs
- Degraded-symbology presets from the ISM HMI note
