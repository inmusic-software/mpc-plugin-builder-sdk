#!/usr/bin/env python3
"""make_presets.py - write the MPC's browsable preset files (.xpl) for a plugin's built-in programs.

Usage:
    make_presets.py --params build/params.json --manifest plugin.json --out "<build>/<Manufacturer> - VST - <Name>"
                    [--categories presets.json]

For every program declared with addProgram() it writes
    <out>/Presets/NNN-Name.xpl                 (no category: shown at the top level of the patch browser)
    <out>/Presets/NNN-Category-Name.xpl        (shown inside a folder called "Category")
using the state chunk that host_sim dumped into params.json ("programChunks"), so loading the file gives
exactly what choosing the built-in program gives.

When categories are used and program 0 is in a category, make_presets.py also writes 000-<Program0>.xpl at
the root level (before the categorised files) so that selecting the instrument in the MPC's Sounds browser
opens the top-level folder view (showing the category folders) rather than jumping straight into program 0's
subfolder.

Categories come from an optional project file presets.json that maps program names to folder names:
    { "Clean Sub": "Bass", "Acid Squelch": "Acid" }

Standard library only. Python 3.9+.
"""

import argparse
import datetime
import json
import os
import re
import shutil
import sys
from xml.sax.saxutils import quoteattr

ILLEGAL = '\\/:*?"<>|'


def clean(part):
    """File-name part: no path characters, no '-' (it separates number, category and name)."""
    part = "".join(" " if (ch in ILLEGAL or ch == "-" or ord(ch) < 32) else ch for ch in part)
    return re.sub(r"\s+", " ", part).strip()


def load_json(path):
    with open(path, encoding="utf-8-sig") as fh:
        return json.load(fh)


def build_xpl(manifest, dump, name, chunk, now):
    kind = str(manifest.get("kind", "instrument")).lower()
    is_instrument = bool(manifest.get("isInstrument", kind != "effect"))
    category = manifest.get("category") or ("Synth" if is_instrument else "Effect")
    binary = manifest.get("file64") or manifest.get("file32") or manifest.get("file") or ""
    attrs = [
        ("name", manifest.get("name", dump.get("name", ""))),
        ("format", "VST"),
        ("category", category),
        ("manufacturer", manifest.get("manufacturer", dump.get("vendor", ""))),
        ("version", manifest.get("version", "")),
        ("file", binary),
        ("uid", str(dump.get("uidInt", 0))),
        ("isInstrument", "1" if is_instrument else "0"),
        ("fileTime", "0"),
        ("infoUpdateTime", "0"),
        ("numInputs", str(dump.get("numInputs", manifest.get("numInputs", 0)))),
        ("numOutputs", str(dump.get("numOutputs", manifest.get("numOutputs", 2)))),
        ("isShell", "0"),
    ]
    plugin = "  <PLUGIN " + "\n          ".join("%s=%s" % (k, quoteattr(str(v))) for k, v in attrs) + "/>"
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        "",
        "<pluginstate>",
        '  <version file="1" date="%s"/>' % now.strftime("%Y-%m-%d-%H-%M"),
        plugin,
        "  <preset>%s</preset>" % name.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"),
        "  <state64>%s</state64>" % chunk,
        "</pluginstate>",
        "",
    ]
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Write browsable .xpl preset files for a plugin's built-in programs.")
    ap.add_argument("--params", required=True, help="build/params.json written by host_sim dump")
    ap.add_argument("--manifest", required=True, help="the project's plugin.json")
    ap.add_argument("--out", required=True, help="the plugin output folder (Presets/ is created inside it)")
    ap.add_argument("--categories", default="", help="optional presets.json mapping program name -> folder name")
    args = ap.parse_args(argv)

    dump = load_json(args.params)
    manifest = load_json(args.manifest)
    names = dump.get("programs") or []
    chunks = dump.get("programChunks") or []
    presets_dir = os.path.join(args.out, "Presets")
    if os.path.isdir(presets_dir):
        shutil.rmtree(presets_dir)

    if not names or len(chunks) != len(names) or not any(chunks):
        print("no preset files: the plugin declares no built-in programs, or host_sim could not read their state "
              "(re-run the build so build/params.json has \"programChunks\")")
        return 0

    categories = {}
    if args.categories and os.path.isfile(args.categories):
        raw = load_json(args.categories)
        categories = raw.get("categories", raw) if isinstance(raw, dict) else {}

    items = []
    for i, (name, chunk) in enumerate(zip(names, chunks)):
        if not chunk or not name:
            continue
        cat = clean(str(categories.get(name, "")))
        items.append((cat, name, chunk, i))

    if not items:
        return 0

    # Group presets by category (in order of first appearance) so each category folder's
    # indices are contiguous, matching the MPC's own factory synth preset layout.
    has_categories = any(cat for cat, _, _, _ in items)
    ordered = []
    if has_categories:
        # Ensure preset 000 is at the root level (uncategorised) so selecting the instrument
        # in the Sounds browser opens the top-level folder screen (showing all category folders)
        # instead of jumping straight into the first preset's subfolder.
        first_cat, first_name, first_chunk, _ = items[0]
        if first_cat:
            ordered.append(("", first_name, first_chunk))
        cat_order = []
        by_cat = {}
        for cat, name, chunk, _ in items:
            if cat not in by_cat:
                cat_order.append(cat)
                by_cat[cat] = []
            by_cat[cat].append((cat, name, chunk))
        # Put categorised folders in order of appearance, then any remaining root-level presets at the end (like Init).
        for cat in cat_order:
            if cat:
                ordered.extend(by_cat[cat])
        for cat in cat_order:
            if not cat:
                ordered.extend(by_cat[cat])
    else:
        ordered = [(cat, name, chunk) for cat, name, chunk, _ in items]

    os.makedirs(presets_dir)
    now = datetime.datetime.now()
    written = 0
    for idx, (cat, name, chunk) in enumerate(ordered):
        stem = "%03d-%s%s" % (idx, (cat + "-") if cat else "", clean(name) or "Program %d" % idx)
        with open(os.path.join(presets_dir, stem + ".xpl"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(build_xpl(manifest, dump, name, chunk, now))
        written += 1
    print("wrote %d preset file%s to %s" % (written, "" if written == 1 else "s", presets_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
