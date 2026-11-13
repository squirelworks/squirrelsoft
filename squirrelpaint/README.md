# SquirrelPaint

A paint program in the Paintbrush tradition, built on `libs/sqgui`. It is
roadmap item S5.2 and is written to move to Squirrel OS/2: the core
(`paint.c`, `paintsel.c`, `paintio.c`, `paintz.c`) is freestanding C11 with no allocation and no
stdio, and `make test` checks that it needs nothing from outside but
`memcpy`, `memmove`, `memset` and `strlen`. Only `paint_sdl.c` and
`main.c` know about SDL2 and files.

```
make -C squirrelpaint             # build/squirrelpaint (a window when SDL2 is installed)
make -C squirrelpaint test
build/squirrelpaint picture.png
```

## The window

- **Tools**, down the left: Pencil (one pixel), Brush (round), Eraser,
  Line, Rect, Ellipse, Fill, Picker and Select. Keys pick them too when no
  text field has the keyboard: P, B, E, L, R, O (ellipse), F, I and S.
- **Selections.** Drag a rectangle with Select; a click elsewhere clears
  it. Dragged by its inside, it lifts off the picture (leaving the second
  colour) and floats until it is put down, by a click outside, Escape,
  another tool, or saving. The lift, the moves and the drop are one step
  to undo. The arrow keys nudge it a pixel at a time. Cut, Copy and Paste
  (Ctrl+X, Ctrl+C, Ctrl+V) use SquirrelPaint's own clipboard; a paste
  comes in at the top-left of the view, floating, with Select ready to
  drag it. Delete (or the Delete key) fills the selection with the second
  colour, and Ctrl+A selects everything.
- **The picture or the selection**: Flip H, Flip V, Rot L and Rot R, and
  Invert work on the selection when there is one and on the whole picture
  when not. Crop cuts the picture to the selection. Resize, beside New,
  scales the selection or the picture to the size in the fields, weighing
  the nearest pixels so it stays smooth. Undo takes back size changes too.
- **Two colours.** The left button paints in the first and the right
  button in the second. The eraser works the other way round, rubbing out
  to the second colour. The palette takes the first colour on a left
  click and the second on a right click. Swap exchanges them, and the
  Picker takes either from the picture.
- **Shapes** are drawn as you drag and kept when you let go. Shift holds
  a rectangle to a square, an ellipse to a circle and a line to a multiple
  of 45 degrees. "Filled shapes" fills them in the colour, and Size sets
  the brush and outline width (1 to 64).
- **Fill** floods the area of the colour clicked, four ways, taking in
  colours within the tolerance (0 to 255 on each channel).
- **Undo and redo** (the buttons, Ctrl+Z, Ctrl+Y) go back over the last 64
  steps. Each stroke keeps only the rectangle it changed, so the limit is
  the step count unless the steps are whole-picture changes (two of the
  largest pictures fit). Escape
  takes back a stroke while it is still held.
- **Zoom** from 1/8 to 32 times with the wheel (about the pointer), `-`
  and `+`, `1:1` and `Fit`. A pixel grid shows from 8 times up. Ctrl and a
  drag move the view, and the arrow keys move it too. A new or opened
  picture is fitted to the window.
- **Files**: type a path, then Open or Save (Ctrl+S). Enter in the path
  field opens it. New makes a white picture of the size in the two fields
  beside it, up to 1024 x 1024. The title shows a star while there is
  something unsaved.

## Files

Read: PNG of every colour type and depth (grey, RGB, palette, grey and
RGB with alpha; 1 to 16 bits), interlaced or not, its data across any
number of IDAT chunks, every chunk's CRC checked. See-through pixels, from
an alpha channel or a tRNS chunk, are put over white, since the picture is
opaque. BMP with the Windows headers (40 to 124 bytes) or the OS/2 1.x
header; 1, 4 and 8 bits with a palette, and 16, 24 and 32 bits, plain or
with bit masks; bottom-up or top-down. Also netpbm PPM and PGM, in binary
or text, up to 16 bits a sample. Compressed BMPs (RLE) are refused with a
message saying so, as is JPEG. Written: PNG (8-bit RGB, each row given
the filter that leaves the smallest bytes, then deflated) for a name ending
in `.png`, P6 PPM for `.ppm` or `.pnm`, and 24-bit BMP for anything else.

The zlib and deflate behind PNG are SquirrelPaint's own (`paintz.c`,
written from RFC 1950 and RFC 1951): inflate reads stored, fixed and
dynamic Huffman blocks and checks the Adler-32; deflate finds matches
with hash chains over the 32 KB window, looks one byte on for a longer
one, and writes each block stored, with the fixed codes or with its own,
whichever is shortest. Its output is within a few percent of zlib's at
level 9.

## Command line

```
FILE, --open FILE  open a PNG, BMP or PPM
--new WxH        start with a white picture (640x480 unless a file is opened)
--do CMD         draw (see below); runs in order with --script
--script FILE    commands one a line, # for comments
--save FILE      write the picture after the commands (no window then)
--snapshot FILE  draw one 960x680 frame of the window to FILE (PPM), no window
--gui-frames N   close the window after N frames (testing)
```

Commands, the same tools as the window:

```
new W H              color RRGGBB        bg RRGGBB
size N               tolerance N         fill on | fill off
pencil|brush|eraser X Y [X Y ...]        a stroke through the points
line|rect|ellipse X0 Y0 X1 Y1 [square]
fill X Y             pick X Y
undo   redo   clear
select X0 Y0 X1 Y1   select all   select none
copy   cut   paste [X Y]   delete   move DX DY   drop
flip h|v   rotate 90|180|270|-90   resize W H [smooth]   crop
canvas W H (grow with the second colour, or cut, from the top-left)   invert
```

`right` among a tool's numbers uses the second colour, as the right button
does. Exit codes: 0, 1 for a bad command, file or write, 2 for usage or no
window.

## Code

- `paint.c`: the picture, the drawing (round dabs, Bresenham lines of
  dabs, rectangles, ellipses by rows of the box's inscribed ellipse,
  flood fill with a mark bitmap and an explicit list), the tools as
  press, drag and release, undo that keeps each stroke's rectangle and
  swaps it back, and the command line.
- `paintsel.c`: the selection (lifted pixels float over the picture until
  dropped), the clipboard, and the transforms, which work on what floats
  or on the whole picture; size changes are whole-picture undo steps.
- `paintio.c`: PNG, BMP and netpbm reading and writing, in memory.
- `paintz.c`: inflate and deflate, CRC-32 and Adler-32.
- `paintgui.c`: the panel; files go through hooks that `main.c` fills.
- `paint_sdl.c` / `paint_nosdl.c`: the window loop, with or without SDL2.

## Tests

`make test` runs `tests/test_paint.c`, which covers:

- dab, line, rectangle and ellipse pixels: counts, symmetry, clipping,
  and closed outlines that hold a fill;
- fill with tolerance, four ways only;
- every tool through press, drag and release, both buttons, Shift,
  cancel and the picker;
- selecting, lifting, moving off the edge, dropping, cut, copy, paste and
  delete, each as one undo step, and the select tool;
- flips, turns (and their inverses), nearest and smooth resizing, crop,
  canvas and invert, on the picture and on a selection, undone and redone
  across size changes;
- undo and redo, including the 64-step limit and the store's;
- every command and its errors;
- BMP and PPM both ways, plus hand-made files of every depth, header and
  bit-mask kind, and every refusal;
- zlib streams from Python's zlib (stored, fixed and dynamic blocks, fed
  whole or a byte at a time), our deflate back through our inflate, and
  broken streams; PNGs made by PIL and by `tests/make_png_vectors.py`
  (palette, 16-bit, interlaced, split IDATs, transparency), which writes
  `tests/png_vectors.h`, and damaged ones;
- the window driven by clicks, drags, both buttons, Shift, Ctrl, the
  wheel, keys, the edit and image buttons, and the file hooks.

It also runs `tests/cli_smoke.sh` (draw, save, reopen BMP, PPM and PNG
byte for byte, a saved PNG checked by a stdlib-only Python reader when
Python is installed, four turns and two flips that change nothing, scripts, undo, errors
and exit codes), the freestanding check, and
ten frames of the real window with `SDL_VIDEODRIVER=dummy` when SDL2 is
installed.
