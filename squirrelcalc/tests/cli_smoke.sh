#!/bin/sh
# squirrelcalc command line: snapshot, errors and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
C=$B/squirrelcalc
fail() { echo "FAIL: $*" >&2; exit 1; }

$C --help | grep -q -- "--snapshot" || fail "--help"
$C --bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "unknown option should exit 2"

out=$($C --fn "x^2 - 2" --roots --snapshot "$B/snap.ppm") || fail "snapshot exit"
echo "$out" | grep -q -- "-1.41421" || fail "roots in the status: $out"
[ "$(head -c 15 "$B/snap.ppm")" = "$(printf 'P6\n900 600\n255')" ] || fail "snapshot header"

out=$($C --let a=3 --fn "a x" --fn "x + a" --roots --snapshot "$B/snap2.ppm") || fail "--let"
echo "$out" | grep -q "y1: 0" || fail "a x has a root at 0: $out"
echo "$out" | grep -q "y2: -3" || fail "x + a has a root at -3: $out"

out=$($C --line "solve x^2 - x = 1" --line "expand (x+1)^3" --snapshot "$B/snap5.ppm") || fail "--line"
echo "$out" | grep -q "x = (1 + sqrt(5))/2" || fail "solve line: $out"
echo "$out" | grep -q "= x^3 + 3\*x^2 + 3\*x + 1" || fail "expand line: $out"

out=$($C --line "factor x^4 - 1" --line "integrate 1/(x^2 + 1)" --snapshot "$B/snap13.ppm") || fail "cas step 3"
echo "$out" | grep -q "= (x - 1)\*(x + 1)\*(x^2 + 1)" || fail "factor line: $out"
echo "$out" | grep -q "integrate 1/(x^2 + 1) = atan(x) + C" || fail "integral line: $out"

out=$($C --fn "x^2 - 2" --fn x --crossings --line "integrate y1 from 0 to 3" --snapshot "$B/snap6.ppm") || fail "crossings"
echo "$out" | grep -q "integrate y1 from 0 to 3 = 3" || fail "integrate line: $out"
echo "$out" | grep -q "y1=y2 at (2, 2)" || fail "crossings: $out"
$C --fn "x^2" --table --snapshot "$B/snap7.ppm" >/dev/null || fail "--table"

$C --let a=3 --fn "(a cos(t), 2 sin(t))" --fn "r = 1 + cos(t)" --snapshot "$B/snap8.ppm" >/dev/null || fail "curves"
err=$($C --fn "(x, t)" --snapshot "$B/snap9.ppm" 2>&1 >/dev/null)
echo "$err" | grep -q "curves use t, not x" || fail "x in a curve: $err"

$C --fn "x^2 + y^2 = 25" --fn "y < sin(x)" --fn "x y >= 4" --snapshot "$B/snap10.ppm" >/dev/null || fail "relations"
err=$($C --fn "x^2 + y" --snapshot "$B/snap11.ppm" 2>&1 >/dev/null)
echo "$err" | grep -q "with y, write an equation" || fail "y without =: $err"
out=$($C --line "xs = {1, 2, 3, 4}" --line "ys = {3, 5, 7, 9}" --line "linreg xs ys" --line "stats ys" \
    --snapshot "$B/snap12.ppm") || fail "lists"
echo "$out" | grep -q "^  y = 2 x + 1$" || fail "linreg line: $out"
echo "$out" | grep -q "r^2 = 1  r = 1" || fail "linreg r^2: $out"
echo "$out" | grep -q "n = 4  mean = 6" || fail "stats: $out"
echo "$out" | grep -q "Fit y = 2 x + 1" || fail "fit in the status: $out"

# the sheet: a CSV in, cells and a fill, a regression over ranges, the CSV out
printf 'x,y\n1,3\n2,5\n3,7\n4,9\n' > "$B/data.csv"
out=$($C --csv "$B/data.csv" --cell "C1=twice" --cell "C2==B2*2" --line "fill C2:C5" --line "linreg A2:A5 B:B" \
    --line "D1 = mean(C2:C5)" --line "normalcdf(-1, 1)" --line "hist B2:B5 2" --sheet \
    --save-csv "$B/out.csv" --snapshot "$B/snap13.ppm") || fail "sheet"
echo "$out" | grep -q "^  y = 2 x + 1$" || fail "linreg over ranges: $out"
echo "$out" | grep -q "D1 = 12" || fail "mean of a range: $out"
echo "$out" | grep -q "normalcdf(-1, 1) = 0.682689492137" || fail "normalcdf: $out"
echo "$out" | grep -q "4 bins of 2 from 2" || fail "hist: $out"
[ "$(sed -n 5p "$B/out.csv")" = "4,9,18," ] || fail "saved values: $(cat "$B/out.csv")"
$C --csv "$B/data.csv" --cell "C2==B2*2" --save-sheet "$B/out2.csv" >/dev/null --snapshot "$B/snap14.ppm" || fail "save-sheet"
grep -q "^1,3,=B2\*2$" "$B/out2.csv" || fail "saved formulas: $(cat "$B/out2.csv")"
$C --cell "Z9=1" --snapshot "$B/snap4.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "--cell Z9 should exit 1"
$C --csv "$B/no-such.csv" --snapshot "$B/snap4.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a missing CSV should exit 1"

# geometry: a triangle, its circumcircle, a measurement in a graph row
out=$($C --line "A = (0, 0)" --line "B = (4, 0)" --line "C = (0, 3)" --line "t = polygon(A, B, C)" \
    --line "c = circle(A, B, C)" --line "area(t) + angle(B, A, C)" --fn "y = slope(segment(B, C)) x" \
    --snapshot "$B/snap15.ppm" 2>&1) || fail "geometry"
echo "$out" | grep -q "c: circle, centre (2, 1.5), radius 2.5" || fail "circumcircle: $out"
echo "$out" | grep -q "area(t) + angle(B, A, C) = 96" || fail "measurements: $out"

err=$($C --fn "sin(" --snapshot "$B/snap3.ppm" 2>&1 >/dev/null)
echo "$err" | grep -q "y1:" || fail "parse error reported: $err"
$C --let "x=1" --snapshot "$B/snap4.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "--let x=1 should exit 1"
$C --fn 1 --fn 2 --fn 3 --fn 4 --fn 5 >/dev/null 2>&1; [ $? -eq 2 ] || fail "five functions should exit 2"
# notebooks: lines run in order, saved, loaded back the same
out=$($C --note-text "# Waves" --note "a = 2" --note "a * 3" --note "y1 = a sin(x)" --note "b + 1" --cell "A1=5" \
    --notes --save-notebook "$B/nb.sqnb" --snapshot "$B/snap15.ppm") || fail "notebook"
echo "$out" | grep -q "^== Waves ==$" || fail "notebook heading: $out"
echo "$out" | grep -q "^> a \* 3$" || fail "notebook line: $out"
echo "$out" | grep -q "^  = 6$" || fail "notebook result: $out"
echo "$out" | grep -q "^  ! b is not defined$" || fail "notebook error: $out"
grep -q "^y1: a sin(x)$" "$B/nb.sqnb" || fail "saved row: $(cat "$B/nb.sqnb")"
grep -q "^cell A1: 5$" "$B/nb.sqnb" || fail "saved cell: $(cat "$B/nb.sqnb")"
grep -q "^math: a \* 3$" "$B/nb.sqnb" || fail "saved line: $(cat "$B/nb.sqnb")"
$C --notebook "$B/nb.sqnb" --save-notebook "$B/nb2.sqnb" --snapshot "$B/snap16.ppm" >/dev/null || fail "notebook back in"
cmp -s "$B/nb.sqnb" "$B/nb2.sqnb" || fail "notebook round trip: $(diff "$B/nb.sqnb" "$B/nb2.sqnb")"
printf 'SquirrelCalc notebook 1\nmath: 1\nbogus\n' > "$B/bad.sqnb"
err=$($C --notebook "$B/bad.sqnb" --snapshot "$B/snap4.ppm" 2>&1 >/dev/null); [ $? -eq 1 ] || fail "a bad notebook should exit 1"
echo "$err" | grep -q "bad.sqnb: line 3: expected" || fail "bad notebook line: $err"
$C --notebook "$B/no-such.sqnb" --snapshot "$B/snap4.ppm" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a missing notebook should exit 1"
echo "squirrelcalc cli ok"
