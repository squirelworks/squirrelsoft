#!/usr/bin/env python3
"""Build a minimal unsigned MSI from a stage directory (Windows + msilib).

Usage: make_msi.py <stage-dir> <product-name> <version> <outfile.msi>

Needs CPython on Windows with the msilib stdlib (removed in 3.13). Exits 2
when msilib is missing so callers can keep the zip and skip the MSI.
"""
from __future__ import annotations

import os
import sys
import uuid


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    stage, product, version, outfile = sys.argv[1:5]
    try:
        import msilib
        from msilib import Directory, Feature, add_data, add_tables, schema, sequence
    except ImportError:
        print("msilib unavailable; skip MSI", file=sys.stderr)
        return 2

    if sys.platform != "win32":
        print("make_msi.py only runs on Windows", file=sys.stderr)
        return 2
    if not os.path.isdir(stage):
        print(f"not a directory: {stage}", file=sys.stderr)
        return 2

    def guid(s: str) -> str:
        return "{" + str(uuid.uuid5(uuid.NAMESPACE_URL, s)).upper() + "}"

    product_code = guid(f"squirrelsoft:{product}:{version}")
    os.makedirs(os.path.dirname(os.path.abspath(outfile)) or ".", exist_ok=True)
    if os.path.exists(outfile):
        os.remove(outfile)

    db = msilib.init_database(
        outfile, schema, product, product_code, version, "SquirrelWorks"
    )
    add_tables(db, sequence)
    # Install into Program Files\SquirrelWorks\<Product>
    add_data(
        db,
        "Property",
        [
            ("UpgradeCode", guid(f"squirrelsoft:{product}:upgrade")),
            ("ALLUSERS", "1"),
            ("ARPURLINFOABOUT", "https://github.com/squirelworks/squirrelsoft"),
        ],
    )

    feature = Feature(db, "DefaultFeature", "Main", "Everything", 0, 1)
    # level 1 cabinet; AbsoluteSource is the stage folder
    cab = msilib.CAB("data")
    root = Directory(db, cab, None, stage, "TARGETDIR", "SourceDir", 0)
    prog = Directory(
        db,
        cab,
        root,
        None,
        "ProgramFilesFolder",
        "PFiles|ProgramFilesFolder",
        0,
    )
    co = Directory(db, cab, prog, None, "SQUIRRELDIR", "SquirrelWorks", 0)
    install = Directory(db, cab, co, None, "INSTALLDIR", product, 0)

    for name in sorted(os.listdir(stage)):
        path = os.path.join(stage, name)
        if not os.path.isfile(path):
            continue
        install.start_component(name.replace(".", "_"), feature, 0, name, keyfile=name)
        install.add_file(name)

    cab.commit(db)
    db.Commit()
    print(outfile)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
