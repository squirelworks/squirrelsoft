#!/usr/bin/env python3
"""Make tests/x86_vectors.h from tests/x86_cases.txt with NASM.

Each case line is assembled by NASM (bits 16 or 32, org 0x1000) and its
bytes kept beside the text, so test_debug checks that x86dis turns the
bytes back into exactly the text, with no NASM needed when the tests run.
Run it again after changing the cases: python3 tests/make_x86_vectors.py
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    cases = os.path.join(HERE, "x86_cases.txt")
    out = os.path.join(HERE, "x86_vectors.h")
    bits = 32
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        src, binf = os.path.join(tmp, "t.asm"), os.path.join(tmp, "t.bin")
        for line in open(cases):
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            if line.startswith("bits "):
                bits = int(line[5:])
                continue
            with open(src, "w") as f:
                f.write("bits %d\norg 0x1000\n%s\n" % (bits, line))
            r = subprocess.run(["nasm", "-f", "bin", "-o", binf, src], capture_output=True, text=True)
            if r.returncode:
                sys.exit("nasm refused %r: %s" % (line, r.stderr))
            code = open(binf, "rb").read()
            rows.append((bits, code, line))
    with open(out, "w") as f:
        f.write("/* Made by tests/make_x86_vectors.py from tests/x86_cases.txt with NASM: do not edit. */\n")
        f.write("typedef struct {\n    int bits, len;\n    unsigned char code[15];\n    const char *text;\n} X86Vector;\n\n")
        f.write("static const X86Vector X86_VECTORS[] = {\n")
        for bits, code, text in rows:
            f.write("    {%d, %d, {%s}, \"%s\"},\n" % (bits, len(code), ", ".join("0x%02x" % b for b in code), text))
        f.write("};\n")
    print("%d vectors" % len(rows))


if __name__ == "__main__":
    main()
