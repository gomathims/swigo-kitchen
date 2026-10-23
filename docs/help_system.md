# Portal help system

Inline help in the SuperSmart local portal, designed to work with no internet.

## Why it is built this way

Two constraints drive the design:

1. **AP mode has no internet.** When the phone joins the device's own AP
   (`192.168.4.1`) nothing can be fetched from eviqtech.com — and that is exactly
   the first-run situation where help matters most.
2. **The portal has no stable origin.** `http://192.168.4.1` and
   `http://192.168.0.124` are different browser origins, so anything the portal
   caches in `localStorage` in STA mode is invisible in AP mode, and a DHCP lease
   change wipes it. The portal is therefore a poor place to own a help cache.

## Where the text comes from

All of it from `/help_en.json`, served by the device itself — so help works in
AP mode, in a plain browser, and on a factory-fresh unit. **The portal makes no
cloud request of its own.** Resolution lives in one place
([help.js](../data/scripts/help.js)):

```js
function helpFor(id) {
  var injected = window.__EVIQ_HELP__ && window.__EVIQ_HELP__[id];
  return injected || (builtinHelp && builtinHelp[id]);
}
```

The EVIQtech app may override entries by setting `window.__EVIQ_HELP__` — that
hook stays wired up as the app-side half of a fixed contract — but nothing is
fetched unless the app pushes it.

## Catalog schema

```json
{
  "_v": 1,
  "_base": "https://www.eviqtech.com/manual/swigo-supersmart/",
  "schedule.repeat": {
    "t": "Repeat",
    "s": "Days this schedule runs. Unticked days are skipped.",
    "l": "schedule#repeat"
  }
}
```

`t` = title, `s` = short body, `l` = optional link. `<br>` in `s` renders as a
line break; the renderer splits on it and emits text nodes, so injected text
cannot smuggle markup.

`_base` is the manual's root URL, configured once for the whole catalog. Each
`l` is a path relative to it, so moving the manual to a new domain or path is a
single edit here rather than 40 edits across the entries. Resolution rules:

- a relative `l` is joined to `_base` (extra or missing slashes are handled);
- an `l` that is already absolute (`http://…` / `https://…`) is used verbatim,
  as an escape hatch for one-off links outside the manual;
- a relative `l` with no `_base` in its catalog renders no link at all, rather
  than a broken one.

The base is read from whichever catalog the entry came from, so an app-injected
override catalog can carry its own `_base` and point somewhere else entirely.

## Markup

One shared renderer, triggered by the italic-`i` icon:

```html
<button type="button" class="help-i" data-help="schedule.repeat" aria-label="Help"></button>
```

Add `help-i-inline` when the icon should sit in normal flow beside a label
instead of floating at the section's top-right. The icon is borderless and
consumes no vertical space. In the Settings and Schedule accordions it is
visible only while the accordion is open.

Granularity is per section, not per field, wherever a form is dense enough that
one icon per label becomes noise. A schedule card carries exactly two:
`schedule.details` at its top-right corner, covering type, days, timings,
enable and delete; and `schedule.cyclic` beside the cyclic checkbox, covering
the on/off durations too. Field titles there are bold instead, so the labels —
not a column of icons — do the signposting.

One id is set from JavaScript rather than markup: `#cloudHelpIcon`, where
`setCloudHelpId()` switches between `settings.cloud` (not yet registered) and
`settings.cloudStatus` (registered).

## App ↔ portal interface

Fixed contract — do not change one side alone.

| Direction | Name | Meaning |
|---|---|---|
| App → portal | `window.__EVIQ_HELP__` | override catalog, same schema |
| App → portal | `eviq-help-ready` event on `window` | catalog was just set or updated |
| App → portal | `window.__EVIQ_ONLINE__` | boolean; can veto "Learn more" links |

The app may set `window.__EVIQ_HELP__` before *or* shortly after load, so the
portal handles both: `helpFor()` reads the global on every lookup, and the
`eviq-help-ready` listener repaints an already-open panel.

The portal itself never fetches anything from the cloud. All text comes from
`/help_en.json` on the device.

### "Learn more" links

Any entry may carry an `l` URL. It renders only when the device has a route out
of the local network: `script.js` calls `setHelpNetworkState()` on every status
update, passing `wMode == "W" || wMode == "E"` (WiFi or Ethernet). In AP mode
the phone is joined to the device's own network with no way out, so the link
is hidden rather than offered and left to fail. It starts hidden and appears
once the first status message confirms an uplink; if a panel is already open
when that arrives, it repaints in place.

Note this is a proxy for reachability, not proof of it — a device on a LAN with
no internet still reports `wMode == "W"`. The firmware does have a real check,
`isInternetAvailable()` in `timesettings.cpp`, but it is a blocking ping to
8.8.8.8 and is not exposed over the API, so it is not suitable for this.

The app can veto independently by setting `window.__EVIQ_ONLINE__` to false.
An `http://` page linking to `https://` is allowed — mixed content only blocks
the reverse.

## Ids are a contract

Any app-injected override catalog keys off these ids. **Renaming or removing a
`data-help` id is a breaking change** — a renamed id silently loses its override
on every device already in the field. Adding a new id is safe.

### Current ids (v1)

`home.control` `home.telemetry` `home.alerts` `home.logs`

`schedule.details` `schedule.cyclic`

`power.dryrunCurrent` `power.dryrunRestart` `power.dryrunRetry`
`power.protectionOnly` `power.thresholdCurrent` `power.overcurrent`
`power.overcurrentRestart` `power.overcurrentRetry` `power.lowVoltage`
`power.lowVoltageRestart` `power.lowVoltageRetry` `power.highVoltage`
`power.highVoltageRestart` `power.highVoltageRetry` `power.powerResumeRestart`
`power.restartDelay` `power.maxRunDuration` `power.breakTime`

`settings.deviceTime` `settings.wifi` `settings.wifiIp`
`settings.deviceName` `settings.devicePin` `settings.apPassword`
`settings.logs` `settings.restart` `settings.cloud` `settings.cloudStatus`
`settings.firmware` `settings.floatSwitch` `settings.floatFlowDelay`
`settings.masterRemote`

Other pages, one namespace each:

`firstuse.device` `firstuse.wifi` — first-run setup
`login.pin` — unlock screen
`techsetup.circuit` `techsetup.inrush` `techsetup.timing` — admin page
`cert.upload` — root CA upload
`mqtt.sync` `mqtt.access` — cloud server settings

`home.alerts` has no visible trigger today because the alert section is hidden;
the entry is kept for when that section returns.

## Build and serving

`data/help_en.json` goes through the normal web-asset pipeline
([migrateWebfilestoStrings.py](../migrateWebfilestoStrings.py)): the JSON is
whitespace-stripped, parsed (a malformed catalog fails the build), gzipped and
emitted into `src/web_content.h` / `src/web_mappings.h` as PROGMEM. `api.cpp`
serves it as `application/json` with `Content-Encoding: gzip` and
`Cache-Control: no-cache`, so an OTA's new catalog is picked up rather than a
stale copy — the URL carries no version query string.

Cost as of v1: 47 entries across all six portal pages, 3611 B gzipped for the
flashed catalog and 1705 B for `help.js`; **+3200 B total firmware flash**,
since moving the help text out of the markup gave part of it back. The source
`help_en.json` is ~11 KB on disk, ~10 KB after minification.
