#!/usr/bin/env python3
# Fetches ESP32 core-dump chunks that the device streamed to ThingsBoard
# telemetry (key "coredump") and reassembles them into a binary file.
#
# Pure stdlib — no pip install needed.
#
# Typical usage:
#   ./fetch_coredump.py \
#       --host https://tb.example.com \
#       --user tenant@example.com --password 'secret' \
#       --device 98:A3:16:DE:EE:84 \
#       --from "2026-04-29 00:00" \
#       --cleanup
#
# On success writes one .bin per complete session into --out-dir (default ./dumps).
# Decode with:
#   espcoredump.py info_corefile -t raw -c gen2-dump_<...>.bin firmware.elf

import argparse
import base64
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone


def http_request(method, url, token=None, body=None, timeout=30):
    headers = {"Content-Type": "application/json"}
    if token:
        headers["X-Authorization"] = "Bearer " + token
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            payload = r.read()
            if not payload:
                return None
            return json.loads(payload.decode("utf-8"))
    except urllib.error.HTTPError as e:
        msg = e.read().decode("utf-8", "replace")
        raise SystemExit(f"HTTP {e.code} on {method} {url}: {msg}")
    except urllib.error.URLError as e:
        raise SystemExit(f"Network error on {method} {url}: {e}")


def login(host, user, password):
    url = host.rstrip("/") + "/api/auth/login"
    res = http_request("POST", url, body={"username": user, "password": password})
    if not res or "token" not in res:
        raise SystemExit("Login response did not contain a token")
    return res["token"]


def resolve_device_id(host, token, name):
    # If the user already passed a UUID, use it as-is.
    if len(name) == 36 and name.count("-") == 4:
        return name
    qs = urllib.parse.urlencode({"deviceName": name})
    url = f"{host.rstrip('/')}/api/tenant/devices?{qs}"
    res = http_request("GET", url, token=token)
    if not res or "id" not in res or "id" not in res["id"]:
        raise SystemExit(f"Device '{name}' not found")
    return res["id"]["id"]


def fetch_timeseries(host, token, device_id, start_ms, end_ms, limit=10000):
    qs = urllib.parse.urlencode({
        "keys": "coredump",
        "startTs": start_ms,
        "endTs": end_ms,
        "limit": limit,
        "agg": "NONE",
        "interval": 0,
        "orderBy": "ASC",
    })
    url = f"{host.rstrip('/')}/api/plugins/telemetry/DEVICE/{device_id}/values/timeseries?{qs}"
    res = http_request("GET", url, token=token)
    return (res or {}).get("coredump", [])


def delete_timeseries(host, token, device_id, start_ms, end_ms):
    qs = urllib.parse.urlencode({
        "keys": "coredump",
        "deleteAllDataForKeys": "false",
        "startTs": start_ms,
        "endTs": end_ms,
        "rewriteLatestIfDeleted": "true",
    })
    url = f"{host.rstrip('/')}/api/plugins/telemetry/DEVICE/{device_id}/timeseries/delete?{qs}"
    http_request("DELETE", url, token=token)


def parse_local_time(s):
    # Accept "YYYY-MM-DD HH:MM" or "YYYY-MM-DDTHH:MM[:SS]".
    s = s.strip().replace("T", " ")
    for fmt in ("%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M", "%Y-%m-%d"):
        try:
            return datetime.strptime(s, fmt).astimezone()
        except ValueError:
            continue
    raise SystemExit(f"Could not parse time: {s!r}")


def to_ms(dt):
    return int(dt.timestamp() * 1000)


def assemble_sessions(rows):
    # rows: list of {"ts": <ms>, "value": "<json string>"} sorted oldest->newest.
    sessions = []
    cur = None  # {"start_ts", "size", "total", "chunks": {seq -> bytes}, "done"}

    def open_session(ts, payload):
        return {
            "start_ts": ts,
            "size": payload.get("size", 0),
            "total": payload.get("total", 0),
            "chunks": {},
            "done": False,
        }

    for r in rows:
        ts = r["ts"]
        try:
            payload = json.loads(r["value"])
        except (json.JSONDecodeError, TypeError):
            continue

        if payload.get("start"):
            if cur is not None and not cur["done"]:
                # Discard prior incomplete session — a new start arrived.
                print(f"  ! discarding incomplete session @ {cur['start_ts']} "
                      f"({len(cur['chunks'])}/{cur['total']} chunks received)")
            cur = open_session(ts, payload)
            continue

        if cur is None:
            # Chunk arrived before any start marker we can see — synthesize.
            cur = {
                "start_ts": ts,
                "size": 0,
                "total": payload.get("total", 0),
                "chunks": {},
                "done": False,
            }

        if payload.get("done"):
            cur["done"] = True
            sessions.append(cur)
            cur = None
            continue

        if "seq" in payload and "data" in payload:
            try:
                cur["chunks"][int(payload["seq"])] = base64.b64decode(payload["data"])
                # Late-arriving total in case start was missed.
                if not cur["total"] and "total" in payload:
                    cur["total"] = int(payload["total"])
            except (ValueError, TypeError):
                continue

    # Trailing session without a done marker (still useful if all chunks present).
    if cur is not None:
        sessions.append(cur)

    return sessions


def write_session(session, out_dir, device_label):
    total = session["total"] or len(session["chunks"])
    received = sorted(session["chunks"].keys())
    missing = [i for i in range(total) if i not in session["chunks"]]
    state = "complete" if (not missing and session["done"]) else "incomplete"

    iso = datetime.fromtimestamp(session["start_ts"] / 1000, tz=timezone.utc) \
        .astimezone().strftime("%Y%m%d_%H%M%S")
    safe_label = device_label.replace(":", "").replace("/", "_")
    suffix = "" if state == "complete" else ".partial"
    fname = f"gen2-dump_{safe_label}_{iso}{suffix}.bin"
    fpath = os.path.join(out_dir, fname)

    with open(fpath, "wb") as f:
        for seq in sorted(session["chunks"].keys()):
            f.write(session["chunks"][seq])

    written = os.path.getsize(fpath)
    print(f"  -> {fpath}  [{state}, {len(received)}/{total} chunks, {written} bytes]")
    if missing:
        # Compress consecutive runs for readability: 4-7,9,12-14
        runs = []
        i = 0
        while i < len(missing):
            j = i
            while j + 1 < len(missing) and missing[j + 1] == missing[j] + 1:
                j += 1
            runs.append(f"{missing[i]}-{missing[j]}" if j > i else f"{missing[i]}")
            i = j + 1
        print(f"     missing seqs: {','.join(runs)}")
    return state == "complete", session


def main():
    ap = argparse.ArgumentParser(description="Fetch & reassemble ESP32 core dumps from ThingsBoard timeseries")
    ap.add_argument("--host", required=True, help="ThingsBoard base URL, e.g. https://tb.example.com")
    ap.add_argument("--user", help="Tenant admin username (email)")
    ap.add_argument("--password", help="Password (or set TB_PASSWORD env)")
    ap.add_argument("--token", help="Pre-obtained JWT (skip login)")
    ap.add_argument("--device", required=True, help="Device name OR UUID")
    ap.add_argument("--from", dest="t_from", default=None,
                    help="Start time (local), e.g. '2026-04-29 00:00'. Default: 24h ago")
    ap.add_argument("--to", dest="t_to", default=None,
                    help="End time (local). Default: now")
    ap.add_argument("--out-dir", default="dumps", help="Output directory (default: dumps)")
    ap.add_argument("--cleanup", action="store_true",
                    help="Delete the coredump timeseries rows from TB after successful assembly")
    ap.add_argument("--limit", type=int, default=10000,
                    help="Max timeseries rows to fetch in one call (default 10000)")
    args = ap.parse_args()

    password = args.password or os.environ.get("TB_PASSWORD")
    if not args.token and (not args.user or not password):
        raise SystemExit("Provide --token, OR --user with --password / TB_PASSWORD env")

    now = datetime.now().astimezone()
    end_dt = parse_local_time(args.t_to) if args.t_to else now
    start_dt = parse_local_time(args.t_from) if args.t_from else now.replace(microsecond=0).timestamp() - 86400
    if isinstance(start_dt, float):
        start_dt = datetime.fromtimestamp(start_dt).astimezone()

    start_ms, end_ms = to_ms(start_dt), to_ms(end_dt)
    if start_ms >= end_ms:
        raise SystemExit("--from must be earlier than --to")

    print(f"Window: {start_dt.isoformat()} -> {end_dt.isoformat()}")

    token = args.token or login(args.host, args.user, password)
    device_id = resolve_device_id(args.host, token, args.device)
    print(f"Device:  {args.device}  (id={device_id})")

    rows = fetch_timeseries(args.host, token, device_id, start_ms, end_ms, args.limit)
    print(f"Fetched: {len(rows)} timeseries rows for key 'coredump'")
    if not rows:
        print("Nothing to assemble.")
        return 0
    if len(rows) >= args.limit:
        print(f"  ! hit limit ({args.limit}); narrow the window or raise --limit")

    # TB returns newest first; sort oldest first so session boundaries are stable.
    rows.sort(key=lambda r: r["ts"])

    sessions = assemble_sessions(rows)
    print(f"Sessions detected: {len(sessions)}")

    os.makedirs(args.out_dir, exist_ok=True)
    completed_sessions = []
    for i, sess in enumerate(sessions, 1):
        print(f"Session {i}: start_ts={sess['start_ts']} total={sess['total']} done={sess['done']}")
        ok, _ = write_session(sess, args.out_dir, args.device)
        if ok:
            completed_sessions.append(sess)

    if args.cleanup and completed_sessions:
        # Only delete rows belonging to fully-assembled sessions.
        # Use start..(last_chunk_ts+1ms) to scope the deletion narrowly.
        for sess in completed_sessions:
            chunk_ts = [r["ts"] for r in rows
                        if sess["start_ts"] <= r["ts"]
                        and (r["ts"] - sess["start_ts"]) < 10 * 60 * 1000]  # 10 min cap
            if not chunk_ts:
                continue
            ds, de = min(chunk_ts), max(chunk_ts) + 1
            print(f"Cleaning timeseries rows in [{ds}, {de}) ...")
            delete_timeseries(args.host, token, device_id, ds, de)
        print("Cleanup done.")

    incomplete = len(sessions) - len(completed_sessions)
    return 0 if incomplete == 0 else 2


if __name__ == "__main__":
    sys.exit(main())
