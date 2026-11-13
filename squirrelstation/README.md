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

Import reads the `LAYER` table (colour, off, frozen) as levels, `BLOCKS` as
cells and `INSERT` as placed cells, `LINE`, `CIRCLE`, `ARC`, `TEXT`/`MTEXT`,
`LWPOLYLINE` and R12 `POLYLINE` as one polyline or shape each (chained
pieces past 24 vertices), polyface meshes and runs of `3DFACE` on one layer
as meshes (in pieces of at most 16 faces). Export writes cells as `BLOCKS`,
placements as `INSERT`, and meshes and solids as polyface meshes, so a design
comes back with its cells. Anything unread is counted as `skipped`.

## Features

- Elements: line, arc/circle, text, polyline, curve (Catmull-Rom through its
  points), shape (closed outline), mesh (triangles and quads), and box,
  cylinder and sphere solids; length, area and volume for each
- Levels with on/off and frozen flags (hidden and frozen levels are neither
  drawn nor snapped to)
- Cells: element groups placed with scale and rotation, cells of cells. The
  members become the definition: they are drawn only where the cell is placed
- Reference attachments: other designs shown and snapped to, moved and scaled
- Accu-style snaps: endpoint, midpoint, centre, intersection, keypoint
  (vertices, solid and cell origins) and nearest
- AccuDraw-style keyins: `xy=x,y[,z]`, `dl=dx,dy[,dz]`, `di=dist,angle`
- Files: text `.sqdgn` (v2, reads v1) and binary `.sqdgnb`, every record and
  length checked; `convert` between them; ASCII DXF import and export (solids
  and meshes export as 3DFACE, curves as sampled polylines)

## The window

```bash
./build/squirrelstation gui site.sqdgn --view iso      # or a .dxf: saves as site.sqdgn
./build/squirrelstation gui site.sqdgn --snapshot site.ppm   # off screen, for CI
```

One view at a time on `libs/sqgui` (SDL2; without SDL2 only `--snapshot`
works), with a toolbar and a `Keyin:` line.

- **Top**, **Front**, **Right** and **Iso** views of everything the design
  draws: levels, placed cells and references, solids as their edges. The
  wheel zooms about the pointer, dragging empty space pans, **Fit** shows
  it all. An axis triad sits in the corner.
- **Line** (a chain) and **Circle** (centre, then a point on it) take
  clicks or keyins. Points snap like AccuSnap: `sqs_snap` in Top view
  (intersections and nearest too), ends, middles, centres and vertices on
  screen in the others. Free points land on the view plane through the
  last point (in Iso, the horizontal plane).
- While a tool has a point, the **AccuDraw compass** sits on it: the
  pointer indexes to an axis when within a few pixels of it, **Ortho**
  locks to the nearer axis, `dl=dx,dy[,dz]` and `di=dist,angle` are taken
  from that point, `xy=` is absolute. The status line shows distance and
  angle.
- **Select** picks an element (a placed cell as a whole) and shows its
  type, level, length, area and volume. **Delete** removes it, **Undo**
  puts it back, **Save** writes `.sqdgn` or `.sqdgnb` by the extension.
- The keyin line also takes `LINE`, `CIRCLE`, `SELECT`, `TOP`, `FRONT`,
  `RIGHT`, `ISO`, `FIT`, `UNDO`, `SAVE`, `DELETE`, `LV=name` (the level new
  elements go on) and `QUIT`. Esc on an empty keyin line works as in the
  view.

## Build

```bash
make
make test
./build/squirrelstation help
```

## Example

```bash
S=./build/squirrelstation
$S new demo.sqdgn
$S level demo.sqdgn walls 0xFF0000
$S line demo.sqdgn 0,0 dl=10,0 --level walls     # id 3
$S shape demo.sqdgn 20,0 30,0 30,10 20,10
$S curve demo.sqdgn 0,20 di=10,0 dl=0,5
$S solid demo.sqdgn box 40,0,0 2,3,4
$S cell demo.sqdgn door 0,0 3
$S place demo.sqdgn door 100,0 2 90
$S attach demo.sqdgn survey.sqdgn 200,0,0 1
$S snap demo.sqdgn 100.1,19.9 0.5             # endpoint of the placed door
$S measure demo.sqdgn 3
$S convert demo.sqdgn demo.sqdgnb
$S export-dxf demo.sqdgn demo.dxf
```
