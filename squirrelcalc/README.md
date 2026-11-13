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

- **Functions of x:** four rows, y1 to y4, each drawn in its own colour,
  typed as `x^2 - 2` or `y = x^2 - 2`. A row that does not parse shows the
  error and where it is.
- **Equations in x and y:** `x^2 + y^2 = 25`, `y^2 = x^3 - 4x`,
  `sin(x) = cos(y)` or `x = 3` are drawn as curves by marching squares. Each
  crossing is refined by bisection, so no line is drawn down a pole such as
  `x/4 = tan(y)`.
- **Inequalities:** `y < sin(x)`, `x y >= 4` or `x^2 + y^2 <= k` shade the
  region where they hold. `<` and `>` draw the edge dashed, and `<=` and `>=`
  draw it solid.
- **Curves of t:** a row can also be a parametric curve `(3 cos(t), 2 sin(t))`
  or a polar one `r = 1 + cos(t)`. t runs from 0 to 2 pi, or from `tmin` to
  `tmax` once those are defined (`tmax = 6pi` for a longer spiral).
- **Sliders:** every variable defined in the calculator (but `ans`) gets a
  slider, up to four. Dragging one redraws every graph that uses it. The
  range is -10 to 10, or wider for bigger values.
- **Calculator:** type a line and press Enter or `=`. `a = 2` defines a
  variable that the graphs can use (`y1 = a sin(x)` redraws when `a`
  changes), and `ans` is the last result. Anything not finite shows as
  `undefined`.
- **CAS lines** in the calculator: `expand (x+1)^3`, `collect x a x + b x`,
  `d/dx sin(x)^2`, `solve x^2 - x = 1` (or `solve a t + 1 = 0 for t`),
  `factor x^4 - 1`, `apart 1/(x^2 - 1)` (partial fractions), and
  `integrate x e^x` or `integrate y1` with no bounds for the symbolic
  integral (`e^x*(x - 1) + C`).
  Answers stay exact (`(1 - sqrt(5))/2`) with the value beside them, and a
  symbolic answer gets a value once its names are defined.
- **Lists and regression** in the calculator:
  - `xs = {1, 2, 3, 4}` (or `[...]`, up to 100 values, each any expression)
    makes a list, and typing its name shows it.
  - `stats xs` gives n, mean, sample standard deviation, min, median, max and
    sum.
  - `plot xs ys` rings the points.
  - `linreg xs ys`, `quadreg xs ys` and `expreg xs ys` fit `y = a x + b`,
    `y = a x^2 + b x + c` and `y = a e^(b x)` by least squares. They print
    the fit and r² (and r for the line) and draw the fit through the points.
    The view moves onto the points, and Reset clears them.
  - r² is measured on y itself, also for the exponential fit.
- **Units:** `5 ft to m`, `65 mph -> km/h`, `20 degC to degF`,
  `35.681236 deg to dms`, or `units 5 kg * 9.81 m/s^2` (shown as `49.05 N`).
  `convert` works like `units`, and the number lands in `ans`.
- **Integrals:** `integrate y1 from 0 to 2` (or any expression of x, with
  bounds such as `pi/2` or a variable) gives the value and shades the area
  between the curve and the x axis. Reset clears the shading.
- **Roots** rings every root in view and lists them at the bottom.
  **Crossings** does the same for every point where two functions meet.
  **d/dx y1** draws the derivative of y1 and prints it, simplified.
- **Graph:** the wheel zooms about the pointer, dragging pans, and `-`, `+`,
  Reset and Square do what they say. The pointer position is shown above the
  graph. Click a curve to trace it: the point and its value stay pinned, and
  Left and Right move it one pixel. Click empty space to let go. Curves break at poles (`1/x`, `tan(x)`) and gaps (`floor(x)`)
  instead of joining across them. Esc closes the window.
- **Table** shows x and every function from Start by Step in place of the
  graph; the wheel scrolls it.

## Command line

```
--fn EXPR        graph EXPR as the next of y1..y4 (a function, a curve of t,
                 an equation in x and y, or an inequality)
--let NAME=EXPR  define a variable first (--let a=2)
--line TEXT      run a calculator line first ("a = 2", "solve x^2 = 2",
                 "xs = {1, 2, 3}", "linreg xs ys");
                 with --snapshot the history is printed
--roots          mark the roots
--crossings      mark where the functions cross
--table          show the table instead of the graph
--deriv          also graph d/dx y1
--snapshot FILE  draw one 900x600 frame to FILE (PPM) and exit, with no window;
                 the roots line is printed
--gui-frames N   close the window after N frames (testing)
```

Exit codes: 0, 1 for a bad `--let` or an unwritable snapshot, 2 for usage
or no window.

## Code

- `calcgraph.c`: the view (pan, zoom, square), plane-to-pixel maps, tick
  steps and labels, axes and grid, curve plotting, point rings, area shading.
- `calcgraph.c` also has implicit curves (marching squares with bisection),
  shaded regions, and plots of fitted curves.
- `calcstats.c`: list statistics and the least-squares fits. The quadratic
  fit centres and scales x first, so data far from 0 stays well conditioned.
- `calcgui.c`: the panel, the calculator line, its variables and lists.
- `calc_sdl.c` / `calc_nosdl.c`: the window loop, with or without SDL2.

## Tests

`make test` runs `tests/test_calc.c` (tick steps and labels, view maps, zoom
about a point, pan, plotted pixels for `x`, `1/x`, `sqrt(x)`, the calculator
line and its errors, roots, the derivative, wheel zoom and drag pan through
the panel, crossings, integrals and their shading, trace by click and arrow
keys, the table, parametric and polar curves, tmin and tmax, sliders, implicit curves with
saddles, poles and dashes, region areas, equation and inequality rows and
their errors, statistics and fits checked against an independent fit, and
lists and their errors),
`tests/cli_smoke.sh`, and ten frames of the real window with
`SDL_VIDEODRIVER=dummy` when SDL2 is installed.
