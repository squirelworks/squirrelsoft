# SquirrelCAD

Clean-room drafting application in the AutoCAD class: layers, blocks, a
command-oriented drawing core, and an original `.sqdwg` file — not Autodesk
AutoCAD binaries.

## AutoCAD / `.dwg` interchange

**Binary `.dwg` is not read directly** (proprietary). The supported bridge is
**ASCII DXF**:

```text
AutoCAD (.dwg)  --SAVEAS DXF-->  file.dxf  --IMPORT-DXF-->  drawing.sqdwg
drawing.sqdwg  --EXPORT-DXF-->  file.dxf  --open in-->  AutoCAD
```

```bash
./build/squirrelcad IMPORT-DXF plan.sqdwg plan.dxf
./build/squirrelcad LIST plan.sqdwg
./build/squirrelcad EXPORT-DXF plan.sqdwg plan_out.dxf
./build/squirrelcad PROBE something.dwg
```

Imported: `LINE`, `CIRCLE`, `ARC`, `TEXT`/`MTEXT`, `LWPOLYLINE`, `LAYER`.  
Sample: `samples/autocad_bridge.dxf`.

## The window

```bash
./build/squirrelcad gui plan.sqdwg            # or a .dxf: saves as plan.sqdwg
./build/squirrelcad gui plan.sqdwg --snapshot plan.ppm   # off screen, for CI
```

A drawing area on black with a toolbar and a `Command:` line, on
`libs/sqgui` (SDL2; without SDL2 only `--snapshot` works).

- **Select**, **Line** (a chain; Esc ends it), **Circle** (centre, then a
  point on it) and **PLine** (Enter ends it; clicking the first point
  closes it). A second Esc goes back to Select.
- Points snap to ends, midpoints, centres and quadrants (through blocks),
  then to the grid when **Grid** is on. **Ortho** keeps a line horizontal
  or vertical from the last point. The status line shows the point and the
  snap.
- The wheel zooms about the pointer, dragging empty space pans, **Fit**
  shows the whole drawing.
- A click selects an entity (an insert as a whole); **Erase** or Delete
  removes it, **Undo** puts it back, **Save** writes the `.sqdwg`.
- Every change goes through the command line, so anything the toolbar
  lacks (`LAYER`, `MOVE`, `TEXT`, `PLOT`, ...) can be typed there, and
  `QUIT` closes the window.

## Features

- Entities: LINE, CIRCLE, ARC, PLINE, TEXT
- Named layers with colour and on/off
- Blocks and INSERT with scale and rotation, nested up to 8 deep. A
  block's parts leave model space, as in classic CAD.
- **The command line** (`squirrelcad run DRAWING [SCRIPT]`): classic verbs
  typed at `Command:` or read from a script.
  - Drawing: `LINE` (a chain), `CIRCLE`, `ARC`, `PLINE ... CLOSE`, `TEXT`
    (to the end of the line).
  - Layers: `LAYER NEW|SET|ON|OFF|COLOR|LIST`.
  - Blocks: `BLOCK name base sel`, `INSERT name point [scale [rotation]]`.
  - Editing: `ERASE`, `MOVE`, `COPY`, `ROTATE`, `SCALE`, and one level of
    `UNDO`.
  - Inquiry: `LIST`, `DIST`, `AREA` (with perimeter), `EXTENTS`.
  - Files: `SAVE`, `OPEN`, `NEW`, `TITLE`, `EXPORT-DXF`, `PLOT`.
  - Points can be `x,y`, `x,y,z`, `@dx,dy` (relative to the last point),
    `d<angle` or `@d<angle` (polar, degrees counter-clockwise from east).
    A selection is entity ids, `ALL` or `LAST`.
  - In a script, a failing line stops the run with `file:line: message`.
    The drawing is saved when the input ends, if it changed.
- **Paper space** (`PLOT out.svg [A4..A0|LETTER|TABLOID] [1:N|FIT]
  [PORTRAIT]`, or `squirrelcad plot`): model space on a sheet as SVG, with
  a border and a title block (title, scale, sheet). Model units are
  metres, so 1:500 is 2 paper mm per metre. Layer colours and on/off
  apply, blocks are expanded, and the drawing is clipped to the viewport.
- **Interchange:** text `.sqdwg` and ASCII DXF (R12).
  - The DXF export writes layer colours (negative when off), a BLOCKS
    section and real INSERTs.
  - The import reads layer colours and on/off, `BLOCKS` as blocks and
    `INSERT` as inserts (nested ones too; `*Model_Space` and the like are
    left out), R12 `POLYLINE`/`VERTEX` as well as `LWPOLYLINE` (chained
    pieces past 16 vertices), `MTEXT` in pieces, and `3DFACE`/`SOLID` as
    closed outlines. A bad number or an overlong line costs one entity,
    never the rest of the file.
  - The `.sqdwg` reader keeps text with spaces, refuses damaged files
    instead of overrunning, and never reuses an id.

## Build

```bash
make
make test
./build/squirrelcad help
```

## Example

```bash
./build/squirrelcad new plan.sqdwg
./build/squirrelcad LINE plan.sqdwg 0,0 40,0
./build/squirrelcad CIRCLE plan.sqdwg 20,10 5
./build/squirrelcad LAYER plan.sqdwg Notes 7
./build/squirrelcad IMPORT-DXF from_acad.sqdwg samples/autocad_bridge.dxf
./build/squirrelcad LIST from_acad.sqdwg

cat > lot.scr <<'SCR'
TITLE Lot_7
LAYER NEW boundary 1
PLINE 0,0 30,0 @0,20 @30<180 CLOSE
AREA LAST                       # area 600  perimeter 100
LAYER SET 0
TEXT 10,10 Lot 7
PLOT lot.svg A4 1:200
SCR
./build/squirrelcad run lot.sqdwg lot.scr
./build/squirrelcad run lot.sqdwg            # Command: prompt
```
