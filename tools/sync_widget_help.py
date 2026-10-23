#!/usr/bin/env python3
"""Copy help texts from data/help_en.json into the cloud widgets.

The device portal serves data/help_en.json itself, so the portal always has the
current text. A ThingsBoard widget cannot reach that file - the device sits on a
LAN and may be offline - and this TB version has no resource type that would let
the catalog be uploaded as JSON. So each widget carries a generated copy, and
this script regenerates it.

data/help_en.json stays the one source of truth: edit it, run this, re-import
the widget. Nothing here edits help text.

    python3 tools/sync_widget_help.py            # update every widget below
    python3 tools/sync_widget_help.py --check    # report drift, change nothing

A widget declares which entries it wants in WIDGETS. Only those are copied, so a
widget carries a few hundred bytes rather than the whole 10 KB catalog.
"""

import argparse
import json
import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HELP_JSON = os.path.join(REPO, "data", "help_en.json")

# widget file -> the help ids it shows. "_base" is always included: the
# "Learn more" links are relative to it.
WIDGETS = {
    "resources/DashboardOct2026/schedule_widget_supersmart___8.json": [
        "schedule.details",
        "schedule.cyclic",
    ],
    "resources/DashboardOct2026/device_control_v14.json": [
        "home.control",
        "home.telemetry",
        "home.logs",
    ],
    "resources/DashboardOct2026/power_settings_widget___v12.json": [
        "power.dryrunCurrent",
        "power.dryrunRestart",
        "power.dryrunRetry",
        "power.protectionOnly",
        "power.thresholdCurrent",
        "power.overcurrent",
        "power.overcurrentRestart",
        "power.overcurrentRetry",
        "power.lowVoltage",
        "power.lowVoltageRestart",
        "power.lowVoltageRetry",
        "power.highVoltage",
        "power.highVoltageRestart",
        "power.highVoltageRetry",
        "power.powerResumeRestart",
        "power.restartDelay",
        "power.maxRunDuration",
        "power.breakTime",
    ],
}

BEGIN = "// === HELP CATALOG (generated from data/help_en.json - do not edit by hand) ==="
END = "// === END HELP CATALOG ==="


def json_string_span(raw, key):
    """Byte span of the raw JSON string value for "key": "...", quotes excluded.

    The widget files are edited by hand and by ThingsBoard exports; rewriting
    them with json.dump would reflow the whole file, so the value is swapped in
    place and everything else stays byte-identical.
    """
    marker = '"%s":' % key
    i = raw.index(marker)
    j = raw.index('"', i + len(marker))
    k = j + 1
    while True:
        if raw[k] == "\\":
            k += 2
            continue
        if raw[k] == '"':
            break
        k += 1
    return j + 1, k


def build_catalog(help_data, ids):
    catalog = {"_base": help_data.get("_base", "")}
    missing = []
    for help_id in ids:
        entry = help_data.get(help_id)
        if entry is None:
            missing.append(help_id)
            continue
        catalog[help_id] = entry
    return catalog, missing


def patch_controller(script, catalog):
    """Replace the generated block, keeping the markers and the comment line."""
    start = script.index(BEGIN)
    end = script.index(END, start)
    head = script[:start]
    tail = script[end + len(END):]
    block = (
        BEGIN
        + "\n// Regenerate with: python3 tools/sync_widget_help.py\n"
        + "var HELP_CATALOG = "
        + json.dumps(catalog, separators=(",", ":"), ensure_ascii=False)
        + ";\n"
        + END
    )
    return head + block + tail


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="report whether a widget is out of date; write nothing")
    args = parser.parse_args()

    with open(HELP_JSON, encoding="utf-8") as fh:
        help_data = json.load(fh)

    stale = False
    for rel_path, ids in WIDGETS.items():
        path = os.path.join(REPO, rel_path)
        name = os.path.basename(path)

        catalog, missing = build_catalog(help_data, ids)
        if missing:
            # A renamed id would silently leave the widget with no help text, so
            # this is an error rather than a warning.
            sys.exit("%s: ids missing from help_en.json: %s" % (name, ", ".join(missing)))

        with open(path, encoding="utf-8") as fh:
            raw = fh.read()

        s, e = json_string_span(raw, "controllerScript")
        script = json.loads('"' + raw[s:e] + '"')

        if BEGIN not in script or END not in script:
            sys.exit("%s: no HELP CATALOG block - add the markers first" % name)

        updated = patch_controller(script, catalog)
        if updated == script:
            print("%s: up to date (%d entries)" % (name, len(ids)))
            continue

        stale = True
        if args.check:
            print("%s: OUT OF DATE" % name)
            continue

        new_raw = raw[:s] + json.dumps(updated)[1:-1] + raw[e:]
        json.loads(new_raw)  # refuse to write anything that is not valid JSON
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(new_raw)
        print("%s: updated (%d entries, %d bytes of catalog)"
              % (name, len(ids), len(json.dumps(catalog, separators=(",", ":")))))

    if args.check and stale:
        sys.exit(1)


if __name__ == "__main__":
    main()
