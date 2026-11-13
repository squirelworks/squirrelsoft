#!/bin/sh
# squirreldebug command line against the practice machine: commands,
# scripts, symbols, errors and exit codes. Usage: cli_smoke.sh BUILD_DIR
set -u
B=${1:-build}
P=$B/squirreldebug
fail() { echo "FAIL: $*" >&2; exit 1; }

$P --help | grep -q -- "--qemu" || fail "--help"
$P --bogus >/dev/null 2>&1; [ $? -eq 2 ] || fail "unknown option should exit 2"
$P --mode 64 >/dev/null 2>&1; [ $? -eq 2 ] || fail "a bad mode should exit 2"
$P --wait x >/dev/null 2>&1; [ $? -eq 2 ] || fail "a bad wait should exit 2"

# the registers and the code at the start
out=$($P --do regs --do "dis pc 3") || fail "regs and dis"
echo "$out" | grep -q "^eip 00001000" || fail "eip: $out"
echo "$out" | grep -q "32-bit code" || fail "the mode: $out"
echo "$out" | grep -q "^>  00001000  bc 00 80 00 00        mov esp, 0x8000" || fail "the first line: $out"
echo "$out" | grep -q "call 0x103e  <print>" || fail "a call named: $out"

# a breakpoint, a run to it, the program's output, memory
out=$($P --do "break square" --do continue --do "x counter 4" --do next --do regs) || fail "break and continue"
echo "$out" | grep -q "^breakpoint at 0x00001033 <square>" || fail "the breakpoint: $out"
echo "$out" | grep -q "^Hello from the practice machine" || fail "the program's output: $out"
echo "$out" | grep -q "^stopped at 0x00001033 <square>" || fail "the stop: $out"
echo "$out" | grep -q "^00002000  01 00 00 00" || fail "the counter: $out"
echo "$out" | grep -q "^eip 00001035" || fail "next: $out"

# a script, with comments and blank lines; a symbol file of our own
printf '# go round twice\nbreak again\n\ncontinue\ncontinue\nx counter 4\nsym pc\n' > "$B/script.txt"
out=$($P --script "$B/script.txt") || fail "a script"
echo "$out" | grep -q "^00002000  01 00 00 00" || fail "twice round: $out"
echo "$out" | grep -q "^0x00001011 <again>" || fail "sym: $out"
printf '00001033 T squaring\n' > "$B/syms.txt"
out=$($P --symbols "$B/syms.txt" --do "sym 0x1035") || fail "--symbols"
echo "$out" | grep -q "<squaring+0x2>" || fail "a symbol from the file: $out"
printf 'nothing here\n' > "$B/nosyms.txt"
$P --symbols "$B/nosyms.txt" --do regs >/dev/null 2>&1; [ $? -eq 1 ] || fail "a file with no symbols should exit 1"

# changing things
out=$($P --do "set eax 0xdead" --do "write result 2a" --do "x result 1" --do "mode 16" --do "dis pc 1") || fail "set and write"
echo "$out" | grep -q "^eax = 0xdead" || fail "set: $out"
echo "$out" | grep -q "^00002004  2a" || fail "write: $out"
echo "$out" | grep -q "16-bit code (set)" || fail "mode: $out"
out=$($P --mode 16 --do "dis pc 1") || fail "--mode 16"
echo "$out" | grep -q "bc 00 80              mov sp, 0x8000" || fail "16-bit code from --mode: $out"

# running and stopping
out=$($P --wait 50 --do continue --do stop --do where) || fail "continue and stop"
echo "$out" | grep -q "^still running" || fail "still running: $out"
echo "$out" | grep -q "(signal 2)" || fail "stopped by us: $out"

# errors: exit 1 with why, and where in a script
err=$($P --do "x nosuch" 2>&1); [ $? -eq 1 ] || fail "a bad address should exit 1"
echo "$err" | grep -q "x nosuch: x ADDRESS \[BYTES\]: which address?" || fail "bad address message: $err"
printf 'regs\nfly\n' > "$B/bad.txt"
err=$($P --script "$B/bad.txt" 2>&1); [ $? -eq 1 ] || fail "a bad script should exit 1"
echo "$err" | grep -q "bad.txt:2: fly: not a command: fly" || fail "bad script line: $err"
$P --script "$B/no-such.txt" >/dev/null 2>&1; [ $? -eq 1 ] || fail "a missing script should exit 1"
err=$($P --tcp 127.0.0.1:1 --do regs 2>&1); [ $? -eq 1 ] || fail "no stub there should exit 1"
echo "$err" | grep -q "127.0.0.1:1" || fail "the refused connection: $err"
err=$($P --tcp nonsense --do regs 2>&1); [ $? -eq 1 ] || fail "a bad TCP address should exit 1"
err=$($P --serial "$B/no-such-tty" --do regs 2>&1); [ $? -eq 1 ] || fail "a missing serial device should exit 1"
err=$($P --serial "/dev/null@1234" --do regs 2>&1); [ $? -eq 1 ] || fail "a bad baud should exit 1"
echo "$err" | grep -q "not a baud rate" || fail "bad baud message: $err"

# a picture of the window, with no window
$P --snapshot "$B/snap.ppm" || fail "snapshot"
[ "$(head -c 2 "$B/snap.ppm")" = "P6" ] || fail "the snapshot is a PPM"
echo "squirreldebug cli ok"
