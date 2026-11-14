#!/bin/sh
# sqcalc command-line smoke test: outputs and exit codes.
# Usage: sh tests/sqcalc_smoke.sh build/sqcalc
set -u
C=${1:?usage: sqcalc_smoke.sh path/to/sqcalc}
fail=0

expect() { # expect WANT ARGS...: stdout must equal WANT and the exit code be 0
    want=$1
    shift
    got=$("$C" "$@" 2>/dev/null)
    rc=$?
    # Windows text-mode stdout is CRLF; strip CR so multi-line compares match LF wants.
    got=$(printf '%s' "$got" | tr -d '\r')
    want=$(printf '%s' "$want" | tr -d '\r')
    if [ "$rc" != 0 ] || [ "$got" != "$want" ]; then
        echo "FAIL: sqcalc $* -> [$got] rc=$rc, want [$want]"
        fail=1
    fi
}

status() { # status CODE ARGS...: only the exit code
    want=$1
    shift
    "$C" "$@" >/dev/null 2>&1
    rc=$?
    if [ "$rc" != "$want" ]; then
        echo "FAIL: sqcalc $* exited $rc, want $want"
        fail=1
    fi
}

expect 17 "2x^2 - 1" x=3
expect 1 "sin(x)" x=pi/2
expect 0.333333333333 "1/3"
expect 3.142 --digits 4 pi
expect "12*x^3 - 2" --diff x "3x^4 - 2x + 7"
expect "$(printf '2*x*sin(x) + x^2*cos(x)\n0')" --diff x "x^2 sin(x)" x=0
expect "5*x" --simplify "2x + 3x"
expect "$(printf 'x = -1.41421356237\nx = 1.41421356237')" --solve x -5 5 "x^2 - 2"
expect 2 --integrate x 0 pi "sin(x)"
expect "$(printf '0\t0\n0.5\t0.25\n1\t1')" --table x 0 1 0.5 "x^2"
expect "x^3 + 3*x^2 + 3*x + 1" --expand "(x+1)^3"
expect "(a + b)*x^2 + x" --collect x "a x^2 + b x^2 + x"
expect "$(printf 'x = (1 - sqrt(5))/2  (-0.61803398875)\nx = (1 + sqrt(5))/2  (1.61803398875)')" --solve-for x "x^2 - x = 1"
expect "$(printf 'x = 2\nx = 3')" --solve-for x "x^2 + 6 = 5x"
expect "x = -b/a" --solve-for x "a x + b = 0"
expect "no real solution" --solve-for x "x^2 = -1"
expect "every x" --solve-for x "2x = x + x"
expect "x*(x - 1)*(x + 1)" --factor x "x^3 - x"
expect "1/(2*(x - 1)) - 1/(2*(x + 1))" --apart x "1/(x^2 - 1)"
expect "e^x*(x - 1)" --antideriv x "x e^x"
expect "x/(2*x^2 + 2) + atan(x)/2" --antideriv x "1/(x^2 + 1)^2"
expect undefined "sqrt(-1)"
expect undefined "1/0"
status 0 --help
status 1 "2 +"
status 1 "y + 1"
status 1 --diff x "min(x, 1)"
status 1 --solve x 0 1 "x + z"
status 2
status 2 --bogus
status 2 --digits 40 pi
status 2 "x" y
status 2 --table x 1 0 0.1 x
status 1 --solve-for x "x^3 = 2"
status 1 --solve-for x "x = 1 = 2"
status 1 --expand "(2x+1)^64"
status 2 --expand
status 1 --antideriv x "e^(x^2)"
status 1 --factor x "x + y"
status 2 --factor x
status 2 --solve-for x
"$C" "2 * (3" 2>&1 | grep -q '^        \^$' || { echo "FAIL: caret under the error"; fail=1; }
"$C" --solve x 2 3 "x^2 - 2" 2>&1 | grep -q 'no sign change' || { echo "FAIL: no-root message"; fail=1; }

[ "$(printf '%s' "$("$C" --units "3 ft + 2 in to cm")" | tr -d '\r')" = "96.52 cm" ] || { echo "FAIL: --units"; fail=1; }
[ "$(printf '%s' "$("$C" --digits 4 --units "100 km/h to mph")" | tr -d '\r')" = "62.14 mph" ] || { echo "FAIL: --units digits"; fail=1; }
[ "$(printf '%s' "$("$C" --units "5 kg * 9.81 m/s^2")" | tr -d '\r')" = "49.05 N" ] || { echo "FAIL: --units SI"; fail=1; }
status 1 --units "5 m to kg"
status 2 --units
"$C" --units "3 m + 2 s" 2>&1 | grep -q '^      \^$' || { echo "FAIL: --units caret"; fail=1; }

[ $fail = 0 ] && echo "sqcalc smoke ok"
exit $fail
