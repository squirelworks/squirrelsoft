# SquirrelStation overview

SquirrelStation targets MicroStation-class design workflows with original
geometry, levels, cells, Accu-style snaps, and `.sqdgn` interchange.

MicroStation `.dgn` files are not parsed (proprietary). Use ASCII DXF as the
bridge: export DXF from MicroStation → `import-dxf`, or `export-dxf` → import
DXF back into MicroStation. See `samples/microstation_bridge.dxf`.

## The design

A design holds levels, elements, cell definitions and reference attachments.
`sqs_visit` walks it as drawn: hidden and frozen levels left out, cell
members drawn only through their placements, cell placements expanded (innermost transform first, at most four deep and a fixed
number of elements per walk), and attached references moved and scaled.
Snapping and the CLI go through that one walk, so what is snapped to is what
is drawn. DXF export instead writes the structure (BLOCKS and INSERTs) so
that cells survive the trip.

`.sqdgn` is line-oriented text (`SQDGN 2`, one record per line). `.sqdgnb` is
`SQDGNB\1\0` followed by tagged little-endian records, each with its length;
a reader that meets a short, oversized or unknown record refuses the file.
