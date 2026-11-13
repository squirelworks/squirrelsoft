# SquirrelPaint

A paint program in the Paintbrush tradition, built on `libs/sqgui`. It is
roadmap item S5.2 and is written to move to Squirrel OS/2: the core
(`paint.c`, `paintio.c`) is freestanding C11 with no allocation and no
stdio, and `make test` checks that it needs nothing from outside but
`memcpy`, `memmove`, `memset` and `strlen`. Only `paint_sdl.c` and
`main.c` know about SDL2 and files.

```
make -C squirrelpaint             # build/squirrelpaint (a window when SDL2 is installed)
make -C squirrelpaint test
build/squirrelpaint picture.bmp
```

## The window

- **Tools**, down the left: Pencil (one pixel), Brush (round), Eraser,
  Line, Rect, Ellipse, Fill and Picker. Keys pick them too when no text
  field has the keyboard: P, B, E, L, R, O (ellipse), F and I.
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
  strokes. Each stroke keeps only the rectangle it changed, so the limit
  is the step count unless the strokes are whole-picture fills. Escape
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

Read: BMP with the Windows headers (40 to 124 bytes) or the OS/2 1.x
header; 1, 4 and 8 bits with a palette, and 16, 24 and 32 bits, plain or
with bit masks; bottom-up or top-down. Also netpbm PPM and PGM, in binary
or text, up to 16 bits a sample. Compressed BMPs (RLE) are refused with a
message saying so, as are PNG and JPEG. Written: 24-bit BMP, or P6 PPM
for a name ending in `.ppm` or `.pnm`.

## Command line

```
FILE, --open FILE  open a BMP or PPM
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
- `paintio.c`: BMP and netpbm reading and writing, in memory.
- `paintgui.c`: the panel; files go through hooks that `main.c` fills.
- `paint_sdl.c` / `paint_nosdl.c`: the window loop, with or without SDL2.

## Tests

`make test` runs `tests/test_paint.c`, which covers:

- dab, line, rectangle and ellipse pixels: counts, symmetry, clipping,
  and closed outlines that hold a fill;
- fill with tolerance, four ways only;
- every tool through press, drag and release, both buttons, Shift,
  cancel and the picker;
- undo and redo, including the 64-step limit and the store's;
- every command and its errors;
- BMP and PPM both ways, plus hand-made files of every depth, header and
  bit-mask kind, and every refusal;
- the window driven by clicks, drags, both buttons, Shift, Ctrl, the
  wheel, keys and the file hooks.

It also runs `tests/cli_smoke.sh` (draw, save, reopen both ways byte for
byte, scripts, undo, errors and exit codes), the freestanding check, and
ten frames of the real window with `SDL_VIDEODRIVER=dummy` when SDL2 is
installed.
