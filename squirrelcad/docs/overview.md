# SquirrelCAD overview

SquirrelCAD targets AutoCAD-class drafting: layers, blocks, INSERT, and a
command verb CLI with `.sqdwg` interchange.

AutoCAD `.dwg` is proprietary. Use ASCII DXF as the bridge: SAVEAS DXF in
AutoCAD → `IMPORT-DXF`, or `EXPORT-DXF` → open in AutoCAD. See
`samples/autocad_bridge.dxf`.
