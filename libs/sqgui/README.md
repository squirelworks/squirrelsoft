# libs/sqgui

A tiny immediate-mode widget layer in C11 (original code), shared by the
desktop apps and meant to port to Squirrel OS/2's wm/gfx.

- **Core** (`src/sqgui_draw.c`, `src/sqgui_ui.c`): a 32-bit `0xAARRGGBB`
  canvas with clipping, lines, fills, bevels, scaled blits and the OS/2 5x7
  font; widgets (label, button, toggle, checkbox, slider, segments, list,
  text field, text box) and a row layout helper. No OS calls, no allocation.
- **Window** (`src/sqgui_sdl.c`): an SDL2 back end, built when `pkg-config`
  finds SDL2.
- **Font**: `font/font5x7.txt` is the source; `tools/mkfont.py` writes
  `src/font5x7.inc` (`make font-check` keeps them in step).

```bash
make            # build/libsqgui.a, plus build/sqgui_sdl.o and the demo with SDL2
make test       # unit tests with a pinned rendering hash, font check, demo on SDL's dummy driver
build/sqgui_demo --scale 2
```

Users: SquirrelSound's Live window (`squirrelsound live`) and SquirrelQR's
`--gui` window. Link `build/libsqgui.a`, and `build/sqgui_sdl.o` with SDL2
for a window; everything else can draw into a buffer and test headless.
