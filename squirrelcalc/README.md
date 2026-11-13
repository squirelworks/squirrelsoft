# SquirrelCalc

A graphing calculator built on `libs/sqmath` (parsing, compiled evaluation,
derivatives, roots) and `libs/sqgui` (the window). It is roadmap item S5.1 and
is written to move to Squirrel OS/2 later: everything except `calc_sdl.c` is
plain C11 drawing into a pixel buffer.

```
make -C squirrelcalc            # build/squirrelcalc (a window when SDL2 is installed)
make -C squirrelcalc test
build/squirrelcalc --fn "x^2 - 2" --fn "2 sin(x)" --roots
```

## The window

- **Functions of x:** four rows, y1 to y4, each drawn in its own colour.
  A row that does not parse shows the error and where it is.
- **Calculator:** type a line and press Enter or `=`. `a = 2` defines a
  variable that the graphs can use (`y1 = a sin(x)` redraws when `a`
  changes), and `ans` is the last result. Anything not finite shows as
  `undefined`.
- **CAS lines** in the calculator: `expand (x+1)^3`, `collect x a x + b x`,
  `d/dx sin(x)^2`, and `solve x^2 - x = 1` (or `solve a t + 1 = 0 for t`).
  Answers stay exact (`(1 - sqrt(5))/2`) with the value beside them, and a
  symbolic answer gets a value once its names are defined.
- **Roots** rings every root in view and lists them at the bottom.
  **d/dx y1** draws the derivative of y1 and prints it, simplified.
- **Graph:** the wheel zooms about the pointer, dragging pans, and `-`, `+`,
  Reset and Square do what they say. The pointer position is shown above the
  graph. Curves break at poles (`1/x`, `tan(x)`) and gaps (`floor(x)`)
  instead of joining across them. Esc closes the window.

## Command line

```
--fn EXPR        graph EXPR as the next of y1..y4
--let NAME=EXPR  define a variable first (--let a=2)
--line TEXT      run a calculator line first ("a = 2", "solve x^2 = 2");
                 with --snapshot the history is printed
--roots          mark the roots
--deriv          also graph d/dx y1
--snapshot FILE  draw one 900x600 frame to FILE (PPM) and exit, with no window;
                 the roots line is printed
--gui-frames N   close the window after N frames (testing)
```

Exit codes: 0, 1 for a bad `--let` or an unwritable snapshot, 2 for usage
or no window.

## Code

- `calcgraph.c`: the view (pan, zoom, square), plane-to-pixel maps, tick
  steps and labels, axes and grid, curve plotting, root rings.
- `calcgui.c`: the panel, the calculator line and its variables.
- `calc_sdl.c` / `calc_nosdl.c`: the window loop, with or without SDL2.

## Tests

`make test` runs `tests/test_calc.c` (tick steps and labels, view maps, zoom
about a point, pan, plotted pixels for `x`, `1/x`, `sqrt(x)`, the calculator
line and its errors, roots, the derivative, wheel zoom and drag pan through
the panel), `tests/cli_smoke.sh`, and ten frames of the real window with
`SDL_VIDEODRIVER=dummy` when SDL2 is installed.
