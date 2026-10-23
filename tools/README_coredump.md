# Core-dump retrieval over ThingsBoard

When the ESP32 panics, `saveCoreDumpToFile()` writes `/logs/gen2-dump.bin` to LittleFS
(see `src/logdebug.cpp`). On the next MQTT cloud-connect, the device publishes a
`dumpAvailable` shared attribute so dashboards can show a "fetch dump" button.

The dashboard triggers RPC `fetchCoreDump`. The device streams the file as small
base64 chunks on the regular telemetry topic `v1/devices/me/telemetry`, all under
the single key `coredump`. ThingsBoard's existing `save timeseries data` rule node
persists every chunk as a timeseries row — **no rule chain change needed**.

This script downloads those rows, reassembles the binary, and (optionally) deletes
the rows from TB so the timeseries doesn't accumulate.

## Why no rule-chain edit

Each chunk arrives as `{"coredump":{"seq":N,"total":T,"data":"<b64>"}}`. The
default `TbMsgTimeseriesNode` saves top-level keys; the entire `coredump` object
is serialized as a JSON-string value with the message's arrival timestamp.
Multiple chunks land as multiple rows under the same key — exactly what we want
to query by time window.

## Usage

```bash
# Simplest run — last 24 hours, prompt for password via env
export TB_PASSWORD='your-password'
python3 fetch_coredump.py \
  --host https://tb.example.com \
  --user tenant@example.com \
  --device 98:A3:16:DE:EE:84

# Specific window + cleanup after successful assembly
python3 fetch_coredump.py \
  --host https://tb.example.com \
  --user tenant@example.com --password "$TB_PASSWORD" \
  --device 98:A3:16:DE:EE:84 \
  --from "2026-04-29 09:00" --to "2026-04-29 12:00" \
  --cleanup
```

Output goes to `./dumps/gen2-dump_<deviceLabel>_<startTs>.bin`. Incomplete
sessions (missing `done` marker or missing seqs) get a `.partial` suffix and
list the missing seq ranges so you know what to re-fetch.

## Decoding

```bash
~/.platformio/packages/framework-espidf/components/espcoredump/espcoredump.py \
  info_corefile -t raw -c dumps/gen2-dump_<...>.bin .pio/build/<env>/firmware.elf
```

The ELF **must** match the firmware that was running when the panic happened.
Keep an archive of `firmware.elf` per release.

## Multiple fetches of the same panic

`saveCoreDumpToFile()` does not delete the dump file after streaming. Triggering
`fetchCoreDump` morning and evening produces two timeseries sessions, two
`.bin` files. They are identical (same panic) — handy if the network was flaky
and morning's session was incomplete.

The device's panic dump is only overwritten on the **next** panic, or when the
dashboard calls RPC `deleteCoreDump`.

## Out-of-order chunks

Chunks may arrive out of order under poor connectivity. The script orders by
`seq`, not arrival ts. The ts is only used to detect session boundaries (a new
`{"start":true}` marker opens a new session). Out-of-order assembly is handled
correctly.

## Dependencies

None. The script uses only the Python 3 standard library, so it runs on any
Ubuntu 18.04+ install without `pip install`.
