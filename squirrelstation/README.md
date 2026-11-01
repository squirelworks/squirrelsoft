# SquirrelStation

Clean-room 3D design application in the MicroStation class: levels, cells,
precision snaps, and an original `.sqdgn` design file — not Bentley MicroStation
and not the proprietary DGN format.

## Features (v0)

- 3D point / line / arc / polyline elements
- Named levels with display flags
- Cells (reusable element groups) and cell library stubs
- Accu-style endpoint / midpoint / intersection snaps
- Text `.sqdgn` interchange
- CLI: create, list, snap-query, write/read design files

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
./build/squirrelstation list demo.sqdgn
```
