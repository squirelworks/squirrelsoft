#!/usr/bin/env python3
"""Tests for suica_analyze.py: frames, the demo walk, redaction and the shared schema."""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

import suica_analyze as sa  # noqa: E402

fails = 0


def expect(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", msg)


def validate(value, schema, path="$"):
    """Minimal JSON Schema subset: type (incl. lists), const, required, properties, items."""
    types = schema.get("type")
    if types is not None:
        names = types if isinstance(types, list) else [types]
        ok = False
        for t in names:
            if t == "null" and value is None:
                ok = True
            elif t == "string" and isinstance(value, str):
                ok = True
            elif t == "boolean" and isinstance(value, bool):
                ok = True
            elif t == "integer" and isinstance(value, int) and not isinstance(value, bool):
                ok = True
            elif t == "number" and isinstance(value, (int, float)) and not isinstance(value, bool):
                ok = True
            elif t == "object" and isinstance(value, dict):
                ok = True
            elif t == "array" and isinstance(value, list):
                ok = True
        expect(ok, "%s: %r is not %s" % (path, value, names))
        if not ok:
            return
    if "const" in schema:
        expect(value == schema["const"], "%s: %r != %r" % (path, value, schema["const"]))
    if isinstance(value, dict):
        for k in schema.get("required", []):
            expect(k in value, "%s: missing %s" % (path, k))
        for k, sub in schema.get("properties", {}).items():
            if k in value:
                validate(value[k], sub, path + "." + k)
    if isinstance(value, list) and "items" in schema:
        for i, v in enumerate(value):
            validate(v, schema["items"], "%s[%d]" % (path, i))


def main() -> int:
    idm = bytes(range(0x10, 0x18))
    expect(list(sa.polling(0x0003, 1)) == [6, 0, 0, 3, 1, 0], "polling frame")
    rd = sa.read_without_encryption(idm, 0x090F, [0, 1, 255, 300])
    expect(rd[0] == len(rd) and rd[1] == 0x06, "read frame header")
    expect(list(rd[14:23]) == [0x80, 0, 0x80, 1, 0x80, 0xFF, 0x00, 0x2C, 0x01], "block list 2/3-byte")
    expect(sa.readable_without_key(0x090F) and not sa.readable_without_key(0x1008), "attributes")
    expect(sa.is_area(0x0000) and sa.service_kind(0x090F) == "cyclic-ro", "kinds")
    expect(abs(sa.response_time_ms(0x4B) - sa.T_MS * 24) < 1e-9, "pmm timing")

    dump = sa.Analyzer(sa.DemoCard(), max_blocks=24).analyze(None, "test")
    expect(dump["error"] is None, "demo error %r" % dump["error"])
    expect(dump["system_codes"] == ["0x0003"], "system codes")
    sysd = dump["systems"][0]
    expect([s["code"] for s in sysd["services"]] == ["0x090f", "0x008b", "0x1008"], "services")
    hist = sysd["services"][0]
    expect(len(hist["blocks"]) == 20, "20 history blocks, got %d" % len(hist["blocks"]))
    by = {a["chunk_size"]: a for a in hist["attempts"]}
    expect(by[4]["blocks_read"] == 20 and by[8]["sf2"] == 0xA2 and by[8]["stopped_at"] == 0, "chunk sweep")
    expect(sysd["services"][2]["attempts"] == [], "keyed service not read")

    with open(os.path.join(ROOT, "schema", "felica-dump.schema.json"), encoding="utf-8") as f:
        schema = json.load(f)
    validate(json.loads(json.dumps(dump)), schema)

    # The made-up debugging dumps in squirrelsuica/tests/fixtures follow the schema too.
    fixtures = os.path.join(os.path.dirname(ROOT), "squirrelsuica", "tests", "fixtures")
    for name in ("demo-suica.json", "demo-nanaco.json"):
        with open(os.path.join(fixtures, name), encoding="utf-8") as f:
            validate(json.load(f), schema, name)

    red = sa.redact(dump)
    text = json.dumps(red)
    expect(dump["idm"] not in text, "IDm removed everywhere")
    expect(red["idm"] == sa.REDACTED, "idm field redacted")
    validate(red, schema)

    if fails:
        print(fails, "failure(s)")
        return 1
    print("suica_analyze tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
