# SquirrelDebug

A debugger for Squirrel OS/2, and for any i386 code QEMU runs, on
`libs/sqgui`: roadmap item S5.4. It talks the GDB remote serial protocol
to QEMU's gdbstub or to a stub on a serial line, and shows the code, the
registers and memory in a window, or takes commands from the command line.

The core (`rsp.c`, the protocol's packets; `x86dis.c`, the disassembler;
`dbg.c`, the debugger; `stub.c`, the practice machine) is freestanding
C11 with no allocation and no stdio, and `make test` checks that it needs
nothing from outside but `memcpy`, `memmove`, `memset` and `strlen`, so it
moves to Squirrel OS/2 as it is. `link.c` (sockets, serial lines, starting
QEMU; POSIX), `dbg_sdl.c` and `main.c` are the desktop's.

```
make -C squirreldebug             # build/squirreldebug (a window when SDL2 is installed)
make -C squirreldebug test
build/squirreldebug                                        # the practice machine
build/squirreldebug --qemu "qemu-system-i386 -fda os.img"  # QEMU, stopped at its first instruction
build/squirreldebug --tcp :1234                            # a QEMU run with -gdb tcp::1234
build/squirreldebug --serial /dev/ttyUSB0@115200 --symbols kernel.sym
```

## What it does

- **Targets**: QEMU over TCP (started by hand with `-gdb tcp::1234`, or
  by SquirrelDebug with `--qemu`, which adds `-S -gdb tcp:...` and ends
  QEMU when it is done), a stub on a serial line (8N1, 9600 to 921600
  baud; QEMU's `-gdb pty` works the same way), or the **practice
  machine**: a small 32-bit machine built in, whose program prints a
  greeting and squares numbers, for trying things out with nothing else.
- **The code**: an x86 disassembler for 16- and 32-bit code in NASM's
  syntax, covering the integer and system instructions a kernel and its
  boot code use (`lgdt`, `mov cr0, eax`, far jumps, string instructions
  with `rep`, `in` and `out`...). x87 instructions show as `fpu` with
  their right length. Calls and jumps are named from the symbols.
- **Real or protected mode**: QEMU's monitor (`info registers`) gives CR0
  and the code segment's descriptor, so the code is shown in 16 or 32
  bits as the CPU runs it, at CS's base; `mode 16|32` (or `--mode`)
  settles it for a stub with no monitor.
- **Running**: step, next (over a call, an `int` or a `rep`), run to an
  address or a line, continue, stop, and breakpoints. Stepping off a
  breakpoint is done for the stub, as gdb does.
- **Changing things**: registers (with `P`, or `G` for a stub without it)
  and memory.
- **Symbols**: `nm`'s output (`ADDRESS TYPE NAME`), so addresses read as
  `<kmain+0x14>` and names work wherever an address does.
- **Output**: what the target prints through the stub (its `O` packets;
  the practice machine's port 0xE9) shows in the window's log.

## The window

Continue, Stop, Step, Next and Run to along the top, then where the
target is, and the mode (Auto, 16, 32). The code, with the pc
highlighted: a click in the left margin sets or clears a breakpoint, a
click on a line picks it for Run to, the wheel scrolls. The registers,
those the last step or run changed in red, the flags, CR0 and CR3.
Memory at an address typed above it (the wheel scrolls). The
breakpoints. The log: commands, their answers, and the target's output.
And a command line that takes the commands below. Ctrl+S steps, Ctrl+N
goes over, Ctrl+R continues, Ctrl+B sets or clears a breakpoint on the
line picked.

## Commands

`--do` (as many as wanted), `--script FILE` (one a line, `#` for
comments), or the window's command line:

| Command | |
|---|---|
| `regs` | the registers |
| `x ADDR [N]` | N bytes of memory (64), hex and text |
| `dis [ADDR] [N]` | N instructions (10) from ADDR (the pc) |
| `break ADDR`, `delete ADDR`, `delete all`, `breaks` | breakpoints |
| `step [N]`, `next`, `until ADDR`, `continue`, `stop` | running |
| `set REG VALUE`, `write ADDR BYTE...` | changing registers and memory |
| `mode 16\|32\|auto` | the code's mode |
| `mon TEXT` | a QEMU monitor command (`mon info registers`) |
| `sym NAME\|ADDR`, `where` | names and where the target is |
| `detach`, `kill` | let the target run on, or end it |

Addresses are hex numbers (`0x` optional), registers (`eax`, `sp`, `pc`
for CS's base plus the instruction pointer), symbols, `SEG:OFF` (SEG × 16
+ OFF), joined by `+` and `-`: `kmain+0x10`, `esp+8`, `07c0:0`. Counts
are decimal. After `continue`, `next` and `until`, the command line waits
up to `--wait` milliseconds (5000) for the target to stop.

## Tests

`make test` runs the protocol's framing, escapes and run lengths; 204
instructions assembled by NASM (`tests/x86_cases.txt`, made into
`tests/x86_vectors.h` by `tests/make_x86_vectors.py`) that must come back
as the same text, and random bytes that must not trip the disassembler;
the debugger against the practice machine, including a stub that refuses
packets and one that compresses its answers; the window driven off
screen; the command line; and, when `qemu-system-i386` and `nasm` are
installed, a boot sector (`tests/boot.asm`) followed in a real QEMU from
the reset vector through real mode into protected mode, over TCP and
over a serial line.
