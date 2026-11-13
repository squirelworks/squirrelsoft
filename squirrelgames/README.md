# SquirrelGames

Card and board games on `libs/sqgui`, roadmap item S5.3. So far:
patience (Klondike and FreeCell), Mines, Hearts and Chess. The rules
(`cards.c`, `mines.c`, `hearts.c`, `chess.c`) are freestanding
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

- **Mines**: a field of hidden squares, some holding mines. Opening a
  square shows how many of its eight neighbours hold mines; one with none
  opens its neighbours too. Open every square without a mine to win.
  The mines go in at the first square opened, never on it or next to it,
  so the first square always opens a clear patch. Small is 9x9 with 10
  mines, Medium 16x16 with 40, Large 30x16 with 99; any field from 2x2 to
  40x24 from the command line.

- **Hearts**: you (South) against three players the computer plays
  (West, North, East). Thirteen cards each; pass three to the left, then
  the right, then across, then keep them, in turn. The two of clubs leads
  the first trick; follow suit if you can; the highest card of the suit
  led takes the trick and leads the next. Each heart taken is a point,
  the queen of spades 13; take all 26 (shooting the moon) and the other
  three get 26 instead. No hearts or the queen on the first trick, and no
  heart led until one has been played (unless a hand holds nothing else).
  Once a score reaches 100, the lowest wins.

- **Chess**: against the computer, as White or Black, or two people at
  one board. Every rule: castling (not out of, through or into check),
  en passant, promotion to any piece, check, checkmate, stalemate, and
  the draws by the fifty-move rule, threefold repetition and too little
  left to mate. The computer looks ahead one to four moves (its level)
  with a search of its own, the captures followed until it is quiet; the
  game's number gives its choices a little variety. Moves are read and
  written in the usual notation (`Nf3`, `exd5`, `O-O`, `e8=Q`) or as
  squares (`e2e4`, `e7e8n`).

A deal (or a field, or a chess game) is a number from 0 to 4294967295: the same number
always gives the same deal (shuffled with SquirrelGames' own generator,
so the numbers are its own).

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
- Klondike, FreeCell or Mines, Draw 3, a deal by its number (Play), New
  (another deal), Restart (this one again), Undo (Ctrl+Z, as far back as
  1000 moves), Home (everything that can go, Ctrl+A). Ctrl+N deals anew.
- Mines: the left button opens a square, or on a number with all its
  flags placed, the squares around it. The right button flags a square,
  then marks it with a question mark, then clears it. The counters show
  the mines less the flags, and the seconds since the first square.
- Hearts: click three cards to pass (again to put one back), then the
  Pass button; then click a card to play it (the ones you may not play
  are shaded). Each name shows the points taken this hand and, in
  brackets, the game's. The computer plays a card about every half
  second, and a trick stays in the middle a moment once it is taken.
- Chess: click a piece (its moves are marked), then where it goes. A
  pawn reaching the last rank becomes a queen in the window (the command
  line can choose). You play White, Black (the board turns round) or
  Both; Level 1 to 4; New; Undo (Ctrl+Z) takes back to your last move.
  The moves are listed beside the board, and the pieces are drawn here.

## Command line

```
--klondike | --klondike3 | --freecell   the game (Klondike, draw 1, unless one of these)
--mines SIZE     Mines: small, medium, large, or WxHxM (20x12x40)
--hearts         Hearts
--chess          Chess (you White, level 3)
--level N        Chess: the computer's level, 1 to 4
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

Mines commands, with squares as `X Y` from `0 0` at the top-left:

```
new small|medium|large [SEED]     new W H MINES [SEED]
open X Y             flag X Y             chord X Y
```

`flag` puts a flag on a hidden square or takes it off; `chord` opens the
neighbours of an open number that has as many flags around it. In the
text, `.` is hidden, `F` a flag, `?` a question mark, a space or 1-8 an
open square, and after a loss `*` a mine and `X` the one that went off.
Opening a mine is not an error: the game is lost and `--show` says so.

Hearts commands, after each of which the computer plays its players up
to your turn:

```
new [SEED]     pass C C C     play C     next (the next hand, once one is over)
```

`--show` prints the scores, the trick on the table (or the last one
taken), your hand, and what you passed and got.

Chess commands, after each of which the computer replies when it is its
turn:

```
new [white|black|both] [SEED]     move M     undo     go     level N     fen FEN
```

`new` says which side you play; `undo` takes back to your previous move;
`go` has the computer move for whichever side is to move; `fen` sets up a
position. `--show` prints the board (White at the bottom, capitals for
White), the moves and the FEN.

## Tests

`make test` runs `tests/test_games.c`: deals (every card once, the same
seed the same deal), each rule of both games and its refusals, scoring,
the stock and its turning over, how many cards may move together in
FreeCell, the best place, cards going home by themselves and finishing,
undo, the commands and their errors, 200 games of random play (the cards
stay 52, each once, and undoing everything gives the deal back), and the
window driven by clicks, double clicks, drags, the right button and the
top bar. For Mines: the first square clear and the counts right on 300
fields of random sizes, flood opening, chords, flags and question marks,
winning and losing, the clock, the commands, and the window's clicks. For
Hearts: every deal whole, the passing order, each rule and its refusal,
who takes a trick, shooting the moon, the end of the game, the computer's
choices, the commands, 200 games the computer plays through (every card
allowed, the cards always 52, each hand 26 points or 78), and a hand
played in the window by clicks. For Chess: the moves counted to depth 3
from three known positions (the start, and two that test castling, en
passant and promotion hard), FEN both ways and what is not a position,
each special move, the notation and which piece it names, checkmate,
stalemate and each draw, undo, the computer finding a mate and a free
queen, games the computer plays against itself, the commands, and moves,
undo, sides and levels in the window. Then `tests/cli_smoke.sh`, the
freestanding check, and ten frames of the real window with
`SDL_VIDEODRIVER=dummy` when SDL2 is installed.
