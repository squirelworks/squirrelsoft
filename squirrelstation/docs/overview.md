# SquirrelStation overview

SquirrelStation targets MicroStation-class design workflows with original
geometry, levels, cells, Accu-style snaps, and `.sqdgn` interchange.

MicroStation `.dgn` files are not parsed (proprietary). Use ASCII DXF as the
bridge: export DXF from MicroStation → `import-dxf`, or `export-dxf` → import
DXF back into MicroStation. See `samples/microstation_bridge.dxf`.
