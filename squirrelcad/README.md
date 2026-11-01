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

## Features (v0)

- Entities: LINE, CIRCLE, ARC, PLINE, TEXT
- Named layers with color / on-off
- Blocks and INSERT
- Model-space drawing list
- Command CLI mirroring classic CAD verbs
- Text `.sqdwg` + ASCII DXF interchange

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
```
