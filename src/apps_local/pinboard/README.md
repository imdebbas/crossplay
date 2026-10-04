# Pinboard — read-later on the device

A crossplay tiny app that pulls your [Pinboard](https://pinboard.in) unread
(`toread`) list over Wi-Fi and reads each saved article on the panel. It is the
Hacker News app with a different front door: the reader, the notice screen, and
the article-extraction pipeline are reused from `hackernews/`, so reading
behaves identically and this app only adds the Pinboard source.

## Files

| File | What it is |
|---|---|
| `PinboardCore.{h,cpp}` | Freestanding, host-tested: token/config parsing + the two Pinboard URLs (`posts/all`, `posts/add`). No Arduino/SD/JSON. |
| `PinboardActivity.{h,cpp}` | The app: Wi-Fi-on-demand, list fetch + parse, and the reader/notice (reusing `hnui::` screens and `hn::` extraction). |

Host tests: `host-tests/pinboard/run.sh` (standard-library C++17, builds and runs
`PinboardCore` on a laptop — no device).

## Setup: your Pinboard token

The app reads one file from the SD card:

```
/.crosspoint/pinboard.cfg
```
```
username:APITOKEN      # from https://pinboard.in/settings/password
tag=x4                 # optional: only pull unread items with this tag
```

A file rather than an on-device keyboard or an OAuth pairing flow (how
Instapaper does it): Pinboard authenticates with a single token string, so a
one-line file is the whole credential. On-device entry is a natural follow-up;
the seam is `PinboardActivity::loadConfig()`.

## How it works

1. **Opens offline.** `onEnter` touches no radio; if the token file is missing it
   shows a notice telling you how to add it.
2. **LOAD** raises the Wi-Fi picker (the first thing that needs the network),
   then builds the reader list: the **30 most recent bookmarks**
   (`posts/recent?count=30`) **plus everything still flagged to-read**
   (`posts/all?toread=yes`), de-duplicated by hash and shown newest first. Each
   call is cached to the card, parsed with ArduinoJson, then merged.
3. **Open a link** → fetches the article through the `r.jina.ai` reader, runs the
   same readability gate as Hacker News, and pages the text. Links that can't be
   articles (PDF/video/JS-only) say so instead of spending the round trip.

`posts/all` is rate-limited by Pinboard to once every ~5 minutes; LOAD is manual,
so that's easy to respect.

## Build it into the firmware (crossplay)

This app compiles **into** the firmware — crossplay has no runtime app loading.
After copying this folder into `src/apps_local/pinboard/`:

1. **Register** (already done if you took the diff): one include + one `kApps`
   row in `src/apps_local/Shelf.cpp`.
2. **Icon**: `tools_local/toybox/icons.txt` has `pinboard = pin`. The symbol
   `icon_pinboard_32` is generated into `src/apps_local/ui/ToyboxIcons.h` by
   `tools_local/toybox/gen_toybox_icons.sh`. **Heed the warning at the top of
   `icons.txt`**: a straight regeneration drops two hand-spliced icons
   (`yahtzee`, `connectfour`), so regenerate to a scratch file and splice in
   `icon_pinboard_32`, the way `seasalt`'s was. Until that symbol exists the
   build won't link.
3. **Build + flash**: `pio run -e x4pro`, then flash (crossplay's one-click web
   installer, or your usual route). Try it in the simulator first:
   `./scripts/dev.sh`, and `CROSSPLAY_AUTOSTART=PINBOARD ./scripts/dev.sh` to
   open straight into it.

## Status / honesty

`PinboardCore` is host-tested and passing. `PinboardActivity` is modeled
line-for-line on the Hacker News app but has **not** been compiled against the
firmware/simulator yet (it was scaffolded in an environment without the ESP32
toolchain). Expect to shake out a few compile errors on the first
`./scripts/dev.sh` — the shapes are right; the details (exact `toybox`/`fui`
signatures, the generated icon) are what a first build will surface.

## Follow-ups worth doing

- **Mark read on Pinboard** after reading: `PinboardCore::markReadUrl()` already
  builds the `posts/add?...&toread=no&replace=yes` call; wire a reader action to
  fire it and drop the row.
- **Offline cache** of the last list (like HN's saved shelf) so the app opens on
  your list with no network.
- **On-device token entry**, replacing the config file.
