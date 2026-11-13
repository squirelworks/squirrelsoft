# SquirrelCAD overview

SquirrelCAD targets AutoCAD-class drafting: layers, blocks, INSERT, a
classic command line (`squirrelcad run`) with relative and polar point
entry, editing verbs and undo, paper-space plotting to SVG with a title
block, `.sqdwg` and DXF interchange with blocks, and a desktop window
(`squirrelcad gui`) on `libs/sqgui`.

AutoCAD `.dwg` is proprietary. Use ASCII DXF as the bridge: SAVEAS DXF in
AutoCAD → `IMPORT-DXF`, or `EXPORT-DXF` → open in AutoCAD. See
`samples/autocad_bridge.dxf`.

## The window

`sqc_gui` is a panel over a command session: the mouse, the toolbar and
the typed `Command:` line all end in `sqc_exec`, so a line drawn with the
mouse and one typed are the same command, with the same undo. Snapping and
picking walk the drawing with `sqc_visit` (blocks expanded) and measure in
window pixels, so the snap distance is the same at every zoom. Segments are
clipped to the drawing area before they are drawn.
