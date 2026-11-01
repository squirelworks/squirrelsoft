# SquirrelStation

Clean-room 3D design application in the MicroStation class: levels, cells,
precision snaps, and an original `.sqdgn` design file — not Bentley MicroStation
binaries.

## MicroStation / `.dgn` interchange

**Binary `.dgn` is not read directly** (proprietary V7/V8). The supported bridge
is **ASCII DXF**, which MicroStation imports and exports:

```text
MicroStation (.dgn)  --Export DXF-->  file.dxf  --import-dxf-->  design.sqdgn
design.sqdgn  --export-dxf-->  file.dxf  --Import DXF-->  MicroStation
```

In MicroStation: **File → Export → DXF/DWG** (choose DXF), then:

```bash
./build/squirrelstation import-dxf site.sqdgn site.dxf
./build/squirrelstation list site.sqdgn
./build/squirrelstation export-dxf site.sqdgn site_out.dxf
./build/squirrelstation probe something.dgn   # explains if binary DGN
```

Imported DXF entities: `LINE`, `CIRCLE`, `ARC`, `TEXT`/`MTEXT`, `LWPOLYLINE`
(as line segments), plus `LAYER` table → levels.

## Features (v0)

- 3D point / line / arc / text elements
- Named levels with display flags
- Cells (reusable element groups) and cell library stubs
- Accu-style endpoint / midpoint / intersection snaps
- Text `.sqdgn` + ASCII DXF interchange
- CLI: create, list, snap-query, DXF import/export

## Build

```bash
make
make test
./build/squirrelstation help
```

## Example

```bash
./build/squirrelstation new demo.sqdgn
./build/squirrelstation line demo.sqdgn 0,0,0 10,0,0
./build/squirrelstation arc demo.sqdgn 0,0,0 5 0 90
./build/squirrelstation import-dxf from_ms.sqdgn samples/microstation_bridge.dxf
./build/squirrelstation list from_ms.sqdgn
```
