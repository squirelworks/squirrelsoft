# SquirrelCAD

Clean-room drafting application in the AutoCAD class: layers, blocks, a
command-oriented drawing core, and an original `.sqdwg` file — not Autodesk
AutoCAD and not DWG.

## Features (v0)

- Entities: LINE, CIRCLE, ARC, PLINE, TEXT
- Named layers with color / on-off
- Blocks and INSERT
- Model-space drawing list
- Command CLI mirroring classic CAD verbs
- Text `.sqdwg` interchange

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
./build/squirrelcad LIST plan.sqdwg
```
