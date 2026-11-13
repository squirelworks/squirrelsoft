#!/bin/sh
# squirreldebug against a real QEMU, when qemu-system-i386 and NASM are
# here: a boot sector (tests/boot.asm) followed from real mode into
# protected mode, over TCP (QEMU started by --qemu) and over a serial line
# (QEMU's gdbstub on a pty). Usage: qemu_smoke.sh BUILD_DIR
set -u
B=${1:-build}
P=$B/squirreldebug
T=$(dirname "$0")
fail() { echo "FAIL: $*" >&2; exit 1; }

if ! command -v qemu-system-i386 >/dev/null 2>&1 || ! command -v nasm >/dev/null 2>&1; then
    echo "squirreldebug: no qemu-system-i386 or no nasm, QEMU not tested"
    exit 0
fi

# the boot sector, and its symbols from NASM's map
{ echo "[map symbols $B/boot.map]"; cat "$T/boot.asm"; } > "$B/boot_map.asm"
nasm -f bin -o "$B/boot.img" "$B/boot_map.asm" || fail "nasm"
awk 'NF == 3 && $1 ~ /^[0-9A-F]+$/ { print $2, "T", $3 }' "$B/boot.map" > "$B/boot.sym"
grep -q " T count$" "$B/boot.sym" || fail "the symbols: $(cat "$B/boot.sym")"
QEMU="qemu-system-i386 -display none -nodefaults -drive file=$B/boot.img,format=raw,if=floppy"

# from the reset vector, through the boot sector in real mode, into 32-bit code
out=$(timeout 60 $P --qemu "$QEMU" --symbols "$B/boot.sym" \
    --do regs --do "break start" --do continue --do "dis pc 2" --do "step 5" --do next \
    --do "break bump" --do continue --do "x 0x500 2" --do "delete all" \
    --do "break count" --do continue --do regs --do continue --do "x 0x600 4" \
    --do "dis pc 3" --do "delete all" --do "until 0x7c4c" --do "set ecx 0x100" \
    --do "step 3" --do "x 0x600 4" --do "mon info registers" 2>&1) || fail "QEMU over TCP: $out"
echo "$out" | grep -q "^eip 0000fff0" || fail "the reset vector: $out"
echo "$out" | grep -q "16-bit code, cs base 0xffff0000" || fail "real mode at reset: $out"
echo "$out" | grep -q "^stopped at 0x00007c00 <start>" || fail "the boot sector: $out"
echo "$out" | grep -q "^>\* 00007c00  fa                    cli" || fail "its first line: $out"
echo "$out" | grep -q "^stopped at 0x00007c0a <start+0xa>" || fail "five steps: $out"
echo "$out" | grep -q "^stopped at 0x00007c30 <bump>" || fail "a breakpoint in a function: $out"
echo "$out" | grep -q "^00000500  01 00" || fail "real mode memory: $out"
echo "$out" | grep -q "^stopped at 0x00007c45 <count>" || fail "into protected mode: $out"
echo "$out" | grep -q "cr0 00000011  cr3 00000000  32-bit code" || fail "32-bit code: $out"
echo "$out" | grep -q "^00000600  01 00 00 00" || fail "on from a breakpoint: $out"
echo "$out" | grep -q "jmp 0x7c45  <count>" || fail "32-bit code shown: $out"
echo "$out" | grep -q "^ecx = 0x100" || fail "set: $out"
echo "$out" | grep -q "^00000600  01 01 00 00" || fail "steps after set: $out"
echo "$out" | grep -q "CS =0008 00000000 ffffffff 00cf9a00" || fail "the monitor: $out"

# over a serial line: QEMU's gdbstub on a pty
$QEMU -S -gdb pty > "$B/qemu_pty.txt" 2>&1 &
QPID=$!
pty=
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    pty=$(sed -n 's|.*\(/dev/pts/[0-9][0-9]*\).*|\1|p' "$B/qemu_pty.txt" | head -1)
    [ -n "$pty" ] && break
    sleep 0.2
done
if [ -z "$pty" ]; then
    kill $QPID 2>/dev/null
    fail "QEMU gave no pty: $(cat "$B/qemu_pty.txt")"
fi
out=$(timeout 30 $P --serial "$pty" --do "break 0x7c00" --do continue --do "step 2" --do where 2>&1)
rc=$?
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null
[ $rc -eq 0 ] || fail "QEMU over a serial line: $out"
echo "$out" | grep -q "^stopped at 0x00007c00" || fail "a breakpoint over serial: $out"
echo "$out" | grep -q "^stopped at 0x00007c03" || fail "steps over serial: $out"
echo "squirreldebug qemu ok"
