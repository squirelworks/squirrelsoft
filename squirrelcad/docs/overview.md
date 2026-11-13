# SquirrelCAD overview

SquirrelCAD targets AutoCAD-class drafting: layers, blocks, INSERT, a
classic command line (`squirrelcad run`) with relative and polar point
entry, editing verbs and undo, paper-space plotting to SVG with a title
block, and `.sqdwg` and DXF interchange with blocks.

AutoCAD `.dwg` is proprietary. Use ASCII DXF as the bridge: SAVEAS DXF in
AutoCAD → `IMPORT-DXF`, or `EXPORT-DXF` → open in AutoCAD. See
`samples/autocad_bridge.dxf`.
