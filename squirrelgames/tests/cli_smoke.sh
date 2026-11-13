#!/bin/sh
# squirrelgames command line: deals, play, text, errors and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
P=$B/squirrelgames
fail() { echo "FAIL: $*" >&2; exit 1; }

$P --help | grep -q -- "--freecell" || fail "--help"
$P --bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "unknown option should exit 2"
$P --seed x --show >/dev/null 2>&1; [ $? -eq 2 ] || fail "a bad seed should exit 2"

# a deal is its seed: the same twice, another for another
a=$($P --seed 42 --show) || fail "show"
b=$($P --seed 42 --show) || fail "show again"
c=$($P --seed 43 --show) || fail "show another"
[ "$a" = "$b" ] || fail "the same seed gave another deal"
[ "$a" != "$c" ] || fail "another seed gave the same deal"
echo "$a" | grep -q "^Klondike, draw 1  seed 42" || fail "the heading: $a"
[ "$(echo "$a" | grep -c '^T[1-7]:')" -eq 7 ] || fail "seven columns"

# play from a script: the ace home, a draw, undo; FreeCell from a command
printf '# seed 42\nmove t5 f1\ndraw\nundo\n' > "$B/play.txt"
out=$($P --seed 42 --script "$B/play.txt" --show) || fail "a script"
echo "$out" | grep -q "F: AH -- -- --" || fail "the ace went home: $out"
echo "$out" | grep -q "score 15  moves 1" || fail "score and moves: $out"
out=$($P --freecell --seed 7 --do "move t8 c1" --do "move t7 c2" --show) || fail "FreeCell"
echo "$out" | grep -q "C: 7D JD -- --" || fail "the cells: $out"

# errors: exit 1 with why, and where in a script
err=$($P --seed 42 --do "move t1 f1" 2>&1); [ $? -eq 1 ] || fail "a move the rules refuse should exit 1"
echo "$err" | grep -q "move t1 f1: the rules do not allow that move" || fail "refused move message: $err"
printf 'draw\njump\n' > "$B/bad.txt"
err=$($P --script "$B/bad.txt" 2>&1); [ $? -eq 1 ] || fail "a bad script should exit 1"
echo "$err" | grep -q "bad.txt:2: jump: unknown" || fail "bad script line: $err"
$P --script "$B/no-such.txt" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a missing script should exit 1"

# a picture of the window
$P --freecell --seed 7 --snapshot "$B/snap.ppm" || fail "snapshot"
[ "$(head -c 15 "$B/snap.ppm")" = "$(printf 'P6\n960 680\n255')" ] || fail "snapshot header"
echo "squirrelgames cli ok"
