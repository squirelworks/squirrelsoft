# SquirrelGames

Card and board games on `libs/sqgui`, roadmap item S5.3. This first part
is patience: Klondike and FreeCell. The rules (`cards.c`) are freestanding
C11 with no allocation and no stdio, and `make test` checks that they
need nothing from outside but `memcpy`, `memmove`, `memset` and
`strlen`, so they move to Squirrel OS/2 as they are. Only `games_sdl.c`
and `main.c` know about SDL2 and files. The cards are drawn by
`gamesgui.c` itself: the suits are 9x9 pictures made for it, and the
ranks use the suite's 5x7 font.

```
make -C squirrelgames             # build/squirrelgames (a window when SDL2 is installed)
make -C squirrelgames test
build/squirrelgames --freecell --seed 7
```

## The games

- **Klondike**: seven columns, the first card of each face up, and the
  rest in the stock. Build columns down in alternating colours, a king
  (or a run under one) into an empty column; take the stock one card at a
  time or three, and turn the waste over again when the stock runs out.
  Score: 5 for a card from the waste to a column, 10 for a card home, 5
  for turning a card up, less 15 for a card brought back from home, and
  100 (20 when drawing three) for turning the waste over.
- **FreeCell**: all 52 cards face up in eight columns, four free cells.
  A run may move together when there is room to move it a card at a time:
  (free cells + 1), doubled for each empty column other than the one it
  goes to. Any card goes into an empty column.

A deal is a number from 0 to 4294967295: the same number always gives the
same deal (shuffled with SquirrelGames' own generator, so the numbers are
its own).

## The window

- Drag a card, or a run in a column, to where it goes. A double click or
  the right button sends it to the best place: home first, then a column
  with cards, then an empty column, then a free cell. A click on the
  stock turns the next cards; a right click on the table sends home what
  can go.
- **Auto home** (on to start with) sends cards home once nothing else can
  need them: an ace or two at once, any other card when both suits of the
  other colour are home to one below it. In Klondike, once the stock is
  gone and every card is face up, it finishes the game.
- Klondike or FreeCell, Draw 3, a deal by its number (Play), New (another
  deal), Restart (this one again), Undo (Ctrl+Z, as far back as 1000
  moves), Home (everything that can go, Ctrl+A). Ctrl+N deals anew.

## Command line

```
--klondike | --klondike3 | --freecell   the game (Klondike, draw 1, unless one of these)
--seed N         the deal
--do CMD         play (see below); in order with --script
--script FILE    commands one a line, # for comments
--show           print the table as text afterwards
--snapshot FILE  draw one 960x680 frame of the window to FILE (PPM), no window
--gui-frames N   close the window after N frames (testing)
```

Commands, with piles named `s` (stock), `w` (waste), `f1`-`f4` (home),
`t1`-`t8` (columns) and `c1`-`c4` (free cells):

```
new klondike|klondike3|freecell [SEED]
draw                 move FROM TO [N]     smart FROM [N]
auto                 finish               undo
```

`move` with no count takes the longest run that may go; `smart` sends the
cards to the best place, as the double click does; `auto` sends home what
is safe, `finish` everything that can go. In the text, a card is its rank
(A, 2-9, T, J, Q, K) and suit (C, D, H, S); `##` is face down. Exit codes:
0, 1 for a move the rules refuse or a bad command, 2 for usage or no
window.

## Tests

`make test` runs `tests/test_games.c`: deals (every card once, the same
seed the same deal), each rule of both games and its refusals, scoring,
the stock and its turning over, how many cards may move together in
FreeCell, the best place, cards going home by themselves and finishing,
undo, the commands and their errors, 200 games of random play (the cards
stay 52, each once, and undoing everything gives the deal back), and the
window driven by clicks, double clicks, drags, the right button and the
top bar. Then `tests/cli_smoke.sh`, the freestanding check, and ten frames
of the real window with `SDL_VIDEODRIVER=dummy` when SDL2 is installed.
