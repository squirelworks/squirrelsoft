#!/usr/bin/env python3
"""SquirrelSuica desktop: run the C reader on dumps built from felica_parse.py's
fixtures and compare balance, history and station names with the Python parser.

Usage: python3 -I tests/test_desktop.py BUILD/squirrelsuica   (from squirrelsuica/)
"""
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent.parent
spec = importlib.util.spec_from_file_location("felica_parse", APP / "felica_parse.py")
fp = importlib.util.module_from_spec(spec)
sys.modules["felica_parse"] = fp
spec.loader.exec_module(fp)

EXE = sys.argv[1]
STATIONS = str(APP / "data" / "StationCode.csv")
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg, file=sys.stderr)


def service(code, blocks, readable=True):
    return {"code": f"0x{code:04x}", "kind": "cyclic-ro", "readable": readable, "key_version": "0x0000",
            "blocks": [b.hex() for b in blocks], "attempts": []}


def system(code, idm, services):
    return {"system_code": f"0x{code:04x}", "idm": idm, "pmm": "100b4b42848510ff", "mode": 0,
            "search_stopped_at": None, "areas": [], "services": services}


def dump(systems):
    return {"schema": "squirrel-felica-dump/1", "tool": "test", "created_at": "2026-11-04T00:00:00Z",
            "idm": systems[0]["idm"], "pmm": "100b4b42848510ff", "pmm_decoded": None, "polls": [],
            "system_codes": [s["system_code"] for s in systems], "systems": systems, "log": [], "error": None}


def run(d, *extra):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False)
        path = f.name
    p = subprocess.run([EXE, "--dump", path, "--stations", STATIONS, *extra], capture_output=True)
    Path(path).unlink()
    return p


def compare(name, d, brand, balance, entries):
    p = run(d, "--json")
    check(p.returncode == 0, f"{name}: exit {p.returncode} {p.stderr.decode()}")
    if p.returncode:
        return
    got = json.loads(p.stdout)
    check(got["brand"] == brand, f"{name}: brand {got['brand']}")
    check(got["balance_yen"] == balance, f"{name}: balance {got['balance_yen']} want {balance}")
    check(len(got["history"]) == len(entries), f"{name}: {len(got['history'])} entries want {len(entries)}")
    for g, e in zip(got["history"], entries):
        want = {"date": e.date, "kind": e.kind, "fare_yen": e.fare_yen, "balance_yen": e.balance_yen,
                "enter_code": e.enter_code, "exit_code": e.exit_code,
                "enter_station": e.enter_station, "exit_station": e.exit_station}
        have = {k: g[k] for k in want}
        check(have == want, f"{name}: entry\n  got  {have}\n  want {want}")
    t = run(d)
    check(t.returncode == 0 and b"Balance" in t.stdout, f"{name}: text output")
    return t.stdout.decode()


# Suica: the demo journeys plus a 008B block, more than one READ_CHUNK of history.
suica_blocks = [
    fp.hist_rail(0x16, 0x01, 2026, 10, 28, 0xE3, 0x59, 0xE3, 0x5E, 2480),
    fp.hist_rail(0x16, 0x01, 2026, 10, 27, 0x25, 0x07, 0x25, 0x0A, 2648),
    fp.hist_rail(0x16, 0x02, 2026, 10, 26, 0x01, 0x01, 0, 0, 2858),
    fp.hist_rail(0x16, 0x01, 2026, 10, 25, 0x25, 0x01, 0x25, 0x02, 858),
    fp.hist_rail(0x16, 0x01, 2026, 10, 24, 0x01, 0x02, 0x01, 0x05, 1058),
    fp.hist_rail(0x16, 0x01, 2026, 10, 23, 0x01, 0x05, 0x01, 0x02, 1258),
]
info = bytes(11) + (2480).to_bytes(2, "little") + bytes(3)
d = dump([system(0x0003, "0101020304050607",
                 [service(0x090F, suica_blocks), service(0x008B, [info]), service(0x1008, [], False)])])
text = compare("suica", d, "suica_family", fp.suica_balance(suica_blocks[0], info),
               fp.parse_history_blocks(suica_blocks))
check(text and "→" in text, "suica: text shows a route")

# nanaco, Edy and WAON with history on the common system.
nanaco_hist = [fp._hist_nanaco(0x47, 480, 5200, 2026, 10, 28, 18, 42),
               fp._hist_nanaco(0x6F, 3000, 5680, 2026, 10, 27, 12, 5)]
nanaco_bal = bytes([0x50, 0x14, 0x00, 0x00]) + bytes(12)
compare("nanaco", dump([system(0xFE00, "02fe0004c7000001",
                                [service(0x5597, [nanaco_bal]), service(0x564F, nanaco_hist)])]),
        "nanaco", fp.nanaco_balance(nanaco_bal), fp.parse_nanaco_history_blocks(nanaco_hist))

edy_bal = bytes([0x1C, 0x0C, 0x00, 0x00]) + bytes(12)
edy_hist = [bytes.fromhex("20000123" "2fa1c3b4" "000001f4" "00000c1c"),
            bytes.fromhex("02000122" "2fa0a001" "00000bb8" "00000e10")]
compare("edy", dump([system(0xFE00, "02fe001317000001",
                             [service(0x1317, [edy_bal]), service(0x170F, edy_hist)])]),
        "edy", fp.edy_balance(edy_bal), fp.parse_edy_history_blocks(edy_hist))

waon_bal = bytes([0xE8, 0x11]) + bytes(14)
waon_rec = fp.hist_waon(0x04, 2026, 10, 28, 19, 10, 4584, 620, 0)
waon_hist = [waon_rec[:16], waon_rec[16:]]
compare("waon", dump([system(0xFE00, "02fe006817000001",
                             [service(0x6817, [waon_bal]), service(0x680B, waon_hist)])]),
        "waon", fp.waon_balance(waon_bal), fp.parse_waon_history_blocks(waon_hist))

# A card with both systems (different IDm per system): transit wins.
compare("two systems", dump([system(0x0003, "0101020304050607", [service(0x090F, suica_blocks[:2])]),
                             system(0xFE00, "0201020304050607", [service(0x5597, [nanaco_bal])])]),
        "suica_family", fp.suica_balance(suica_blocks[0], None), fp.parse_history_blocks(suica_blocks[:2]))

# Unknown system: IDm only.
p = run(dump([system(0x8008, "0301020304050607", [])]), "--json")
got = json.loads(p.stdout) if p.returncode == 0 else {}
check(got.get("brand") == "unknown" and got.get("system_code") == "8008" and got.get("balance_yen") is None,
      f"unknown: {got}")

# SquirrelSuicaAnalysis's own demo dump, as that tool writes it.
ana = subprocess.run([sys.executable, "-I", str(APP.parent / "squirrelsuica-analysis" / "suica_analyze.py"), "--demo"],
                     capture_output=True, check=True)
p = run(json.loads(ana.stdout), "--json")
check(p.returncode == 0 and json.loads(p.stdout)["brand"] == "suica_family", "analysis demo dump")

# The made-up debugging dumps (tests/fixtures), against felica_parse.
FIX = APP / "tests" / "fixtures"


def blocks_of(d, system, code):
    sy = next(s for s in d["systems"] if s["system_code"] == system)
    return [bytes.fromhex(b) for b in next(v for v in sy["services"] if v["code"] == code)["blocks"]]


demo = json.loads((FIX / "demo-suica.json").read_text(encoding="utf-8"))
hist = blocks_of(demo, "0x0003", "0x090f")
compare("demo suica", demo, "suica_family",
        fp.suica_balance(hist[0], blocks_of(demo, "0x0003", "0x008b")[0]), fp.parse_history_blocks(hist))
demo = json.loads((FIX / "demo-nanaco.json").read_text(encoding="utf-8"))
compare("demo nanaco", demo, "nanaco", fp.nanaco_balance(blocks_of(demo, "0xfe00", "0x5597")[0]),
        fp.parse_nanaco_history_blocks(blocks_of(demo, "0xfe00", "0x564f")))

# Errors: not a dump, bad JSON, bad block hex, no file.
check(run({"schema": "something-else"}).returncode == 2, "wrong schema")
bad = dump([system(0x0003, "0101020304050607", [service(0x090F, [])])])
bad["systems"][0]["services"][0]["blocks"] = ["zz"]
check(run(bad).returncode == 2, "bad block hex")
check(subprocess.run([EXE, "--dump", "/nonexistent.json"], capture_output=True).returncode == 2, "missing file")
with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
    f.write('{"schema": "squirrel-felica-dump/1", "systems": [')
check(subprocess.run([EXE, "--dump", f.name], capture_output=True).returncode == 2, "truncated JSON")
Path(f.name).unlink()
# A live read with no such reader (or no PC/SC at all) is a reader error.
check(subprocess.run([EXE, "--reader", "no-such-reader-zz", "--wait", "0"], capture_output=True).returncode == 2,
      "missing reader")
# PaSoRi USB path with no hardware (or a nonsense name) is a reader error, not a crash.
usb = subprocess.run([EXE, "--usb", "--reader", "no-such-pasori-zz", "--wait", "0"], capture_output=True)
check(usb.returncode == 2 and usb.stderr, "missing pasori")
help_out = subprocess.run([EXE, "--help"], capture_output=True)
check(help_out.returncode == 0 and b"--usb" in help_out.stdout, "--help mentions --usb")
check(help_out.returncode == 0 and b"--gui" in help_out.stdout, "--help mentions --gui")
check(help_out.returncode == 0 and b"--csv" in help_out.stdout, "--help mentions --csv")
list_out = subprocess.run([EXE, "--list-readers"], capture_output=True)
check(list_out.returncode == 0, "--list-readers")
check(subprocess.run([EXE, "--wait", "x"], capture_output=True).returncode == 2, "bad --wait")

# GUI smoke: auto-quit after a short window. Uses the demo dump (made-up data).
# Prefer a real/Xvfb display when present; otherwise SDL's dummy video driver.
# nosdl stub builds print text and still exit 0.
import os
gui_env = os.environ.copy()
if not gui_env.get("DISPLAY") and not gui_env.get("WAYLAND_DISPLAY"):
    gui_env["SDL_VIDEODRIVER"] = "dummy"
gui = subprocess.run(
    [EXE, "--gui-ms", "250", "--dump", str(FIX / "demo-suica.json"), "--stations", STATIONS],
    capture_output=True, env=gui_env, timeout=30)
check(gui.returncode == 0, f"--gui-ms smoke exit {gui.returncode} {gui.stderr.decode()[:300]}")
# CSV export from the made-up demo dump: shape + IDm omitted by default.
csv = subprocess.run(
    [EXE, "--csv", "--dump", str(FIX / "demo-suica.json"), "--stations", STATIONS],
    capture_output=True, text=True)
check(csv.returncode == 0, f"--csv exit {csv.returncode} {csv.stderr}")
lines = [ln for ln in csv.stdout.splitlines() if ln.strip()]
check(lines and lines[0].startswith("index,brand,system_code,card_balance_yen"), f"--csv header {lines[:1]}")
check("idm" not in lines[0].split(","), "--csv omits idm by default")
check(len(lines) > 1 and "suica_family" in lines[1], "--csv data row")
# Demo IDm must not appear when redacted.
demo_idm = json.loads((FIX / "demo-suica.json").read_text(encoding="utf-8"))["idm"]
check(demo_idm not in csv.stdout, "--csv redacts demo idm")
with_idm = subprocess.run(
    [EXE, "--csv", "--include-idm", "--dump", str(FIX / "demo-suica.json"), "--stations", STATIONS],
    capture_output=True, text=True)
check(with_idm.returncode == 0 and demo_idm in with_idm.stdout, "--include-idm shows idm")
with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False) as f:
    path = f.name
csvf = subprocess.run(
    [EXE, "--csv-file", path, "--dump", str(FIX / "demo-suica.json"), "--stations", STATIONS],
    capture_output=True, text=True)
check(csvf.returncode == 0 and Path(path).read_text(encoding="utf-8").startswith("index,"), "--csv-file")
Path(path).unlink()

print("squirrelsuica desktop:", "FAILED" if failures else "ok")
sys.exit(1 if failures else 0)
