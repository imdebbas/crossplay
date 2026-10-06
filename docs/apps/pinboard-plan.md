# Pinboard

A read-later shelf for a [pinboard.in](https://pinboard.in) account, built on the
same bones as Hacker News and Instapaper: browse what you have saved, pick the
ones you want, and keep the article text on the device to read offline.

Read [docs/shelf.md](../shelf.md) for how an app is registered, and
[docs/apps/instapaper-plan.md](instapaper-plan.md) and the Hacker News sources
for the two apps this one is modelled on.

## Why it looks like Hacker News and not Instapaper

The two existing read-later apps make opposite bets about where the hard work
happens, and Pinboard can take the cheaper one.

- **Instapaper** needs a server. Instapaper's API is OAuth, and turning an
  article's HTML into flat text needs a paid extractor key, so a FastAPI
  **bridge** (`server/read-bridge`, published at `read.ma-r-s.com`) holds the
  token and does the extraction. That bridge runs on the upstream author's own
  hardware; a fork cannot deploy to it, and the host is compiled into the
  firmware (`InstapaperSync.h:51`).

- **Hacker News** needs nothing of ours. The device talks straight to public
  endpoints over TLS: the Algolia API for the list
  (`HackerNewsActivity.cpp:28`) and a free readability proxy,
  `https://r.jina.ai/`, to turn a linked page into Markdown
  (`HackerNewsActivity.cpp:44`).

Pinboard fits the Hacker News shape exactly, and that is the whole reason this
app is affordable:

- **The list** is one HTTPS GET to `https://api.pinboard.in/v1/posts/all`
  carrying the user's own API token. No OAuth, no server, no secret anywhere
  but on the device. Pinboard's token is `username:HEXSTRING`, taken from the
  user's own settings page.
- **The article text** is the *same* `r.jina.ai` extractor and the *same*
  readability gate Hacker News already uses. A bookmark is a URL; so is a
  Hacker News story. The answer-parser is shared rather than twinned
  (`hn::splitExtractorResponse`, `hn::paragraphsFromMarkdown`,
  `hn::readsAsProse`, `hn::urlCanBeArticle`).

So there is **no `server/` component for Pinboard and nothing to host.** That
also keeps the fork upstream-mergeable: everything new lives under
`src/apps_local/pinboard/` plus one row in `src/apps_local/Shelf.cpp` and one
line in `tools_local/toybox/icons.txt`, which is the fork's designated
no-conflict zone (see [LOCAL_SCOPE.md](../../LOCAL_SCOPE.md)).

## The shape of it

Four screens, the same state machine Hacker News runs
(`HackerNewsActivity.h`), with one view added for the thing that is genuinely
different here — picking several at once.

```
Token      first run only: type the pinboard.in API token, stored on the card
Browse     the bookmarks fetched from Pinboard, each with a checkbox
           tap a row to pick it; SAVE pulls the picked ones onto the device
Saved      the read-later shelf: articles kept on the card, read offline
Reading    one article, wrapped to the panel (identical to HN's reader)
Busy       a network step is about to happen; one step per loop pass
Notice     a verdict or an error
```

The two shelves — Browse and Saved — are the same split Hacker News draws
(front page vs. saved), through the same row machinery, so the paint can never
show one shelf's titles over the other's indices
([HackerNewsRows.h](../../src/apps_local/hackernews/HackerNewsRows.h)).

### The one new idea: pick many, then Save

Hacker News opens one article, and *then* a SAVE button keeps the one you are
reading. Pinboard inverts that, because the user's ask was "show my saved
articles, let me check the ones I want, hit a save button, and have them on my
device":

- **Browse rows carry a checkbox.** Tapping a row toggles whether it is picked,
  drawn with the same filled-vs-outlined box the shelf chooser uses
  ([docs/shelf.md](../shelf.md) "Choosing what a folder shows"). The whole row
  is the hit target, not the box on it — same rule as the chooser.
- **SAVE runs a batch.** For each picked bookmark, one loop pass: fetch through
  `r.jina.ai`, run the readability gate, write the flat text to the card. One
  network operation per pass, never a chain — the exact discipline Instapaper's
  download loop keeps (`InstapaperActivity.h` decisions 1–2) so Back always
  answers and the busy caption can name the article arriving.
- **A bookmark whose page will not render** (a PDF, a JS-only page) is reported
  in the batch verdict rather than failing silently, the same way Hacker News's
  gate reports an unreadable link (`HackerNewsActivity.cpp:539`).

The default list is **"to read"** bookmarks — Pinboard's own read-later flag.
`posts/all` has no server-side `toread` filter, so the device filters on the
`toread` field each post carries. A later toggle can show all bookmarks; the
filter is one predicate in `PinboardCore` and host-tested.

## Authentication: the token, typed once

No QR, no server, no OAuth. On first run the Browse screen is empty and offers
ENTER TOKEN, which opens the firmware's own on-screen keyboard
([`KeyboardEntryActivity`](../../src/activities/util/KeyboardEntryActivity.h))
via `startActivityForResult`, exactly as the OPDS and Wi-Fi flows do. The
result comes back as a `KeyboardResult{ text }`
([ActivityResult.h:16](../../src/activities/ActivityResult.h)).

The token is stored on the card at `/.crosspoint/pinboard/.token`, beside the
library and under the same `/.crosspoint/` tree everything else caches to, so
clearing that directory clears it. It is the user's credential and never leaves
the device except as the `auth_token` parameter on a TLS request to
`api.pinboard.in`.

`PinboardCore::parseAuthToken` validates the `user:HEX` shape before anything
is stored, so a mistyped token fails on the Token screen with a sentence rather
than as a 401 three screens later.

## How articles are stored — plain text, like Hacker News

The library mirrors Hacker News's exactly
([HackerNewsLibrary.cpp](../../src/apps_local/hackernews/HackerNewsLibrary.cpp),
[HackerNewsSaved.h](../../src/apps_local/hackernews/HackerNewsSaved.h)), because
that format was chosen to outlive the code and there is no reason to invent a
second one:

```
/.crosspoint/pinboard/
  .token          the user:HEX API token, typed once
  saved.tsv       the index: one version-header line, then one tab-separated
                  line per article (id, savedAt, title, url)
  <id>.txt        one plain UTF-8 text file per article, body as flat text
                  id = 32-bit FNV-1a of the URL, lowercase hex
```

Not EPUB, and not an opaque cache. Readable in any text editor, recoverable by
hand, and still there if the app is ever removed. (If the user asked for real
`.epub` files in the main bookshelf, that is a different feature — an on-device
EPUB writer — and is explicitly out of scope here; this is the in-app reader
the two existing apps are.)

`PinboardSaved` is a byte-for-byte sibling of `HackerNewsSaved`: FNV-1a id,
tab-separated line format that fails *open* (a damaged row costs one entry, not
the library), typography folded on both read and write so a stored title has no
glyph holes, the URL never folded because it is hashed. The magic is
`pinsaved` and the version starts at 1. Pinboard has no comment threads, so the
`savedThread*` helpers do not exist here.

## File layout and the three-way split

Per [docs/shelf.md](../shelf.md) "Split it three ways":

| Layer         | File                   | Knows about                                   |
| ------------- | ---------------------- | --------------------------------------------- |
| State / format | `PinboardSaved.{h,cpp}` | nothing — freestanding, host-tested          |
| State / format | `PinboardCore.{h,cpp}`  | nothing — freestanding, host-tested          |
| State / format | `PinboardRows.{h,cpp}`  | nothing — freestanding, host-tested          |
| Screens       | `PinboardScreens.{h,cpp}` | FreeInkUI + Toybox tokens only              |
| Activity      | `PinboardActivity.{h,cpp}` | renderer, storage, Wi-Fi, keyboard, shelf  |

- **`PinboardSaved`** — the on-card library format (above).
- **`PinboardCore`** — the Pinboard-specific brains: the `Bookmark` model, the
  `auth_token` parse/validate/format, the `posts/all` query-path builder, the
  `toread` filter, and the display-title fallback (a bookmark with no
  description shows a cleaned form of its URL). The readability gate and
  Markdown flattener are *not* re-implemented here — the Activity calls `hn::`
  for those, since both apps hit the same extractor and should share one
  answer-parser.
- **`PinboardRows`** — browse rows (with their checkbox state) and saved rows
  from one row machine, carrying the view they were built for, as
  `HackerNewsRows` does.
- **`PinboardScreens` / `PinboardActivity`** — a close adaptation of the Hacker
  News pair. The reader, the busy/notice screens, the Wi-Fi-on-demand flow and
  the Back/paging discipline are all reused with minimal change; what is new is
  the Token screen, the checkbox rows, and the batch SAVE loop.

## Registration

One row in `src/apps_local/Shelf.cpp`'s `kApps[]`:

```cpp
{"PINBOARD", &icon_pinboard_32, &PinboardActivity::create},
```

and one line in `tools_local/toybox/icons.txt`:

```
pinboard = pin
```

then `./tools_local/toybox/gen_toybox_icons.sh` regenerates
`src/apps_local/ui/ToyboxIcons.h` (committed). Lucide's `pin` is the obvious
silhouette; `bookmark` is an alternative if `pin` reads too much like a map
marker next to the other rows.

## What is verified where

- **Freestanding, host-tested** (`host-tests/pinboard/`, no device, no
  PlatformIO): the saved-index format, the token parsing, the query-path
  builder, the `toread` filter, the display-title fallback. These run on any
  laptop with a C++17 compiler and are the real logic.
- **Simulator** (`scripts_local/sim-shot.sh`, on a machine with the toolchain):
  the three-variant render of the Browse and Token screens before the winner is
  built (docs/shelf.md "Three variants"), the batch SAVE verdict, and the
  Back/paging behaviour.
- **Device**: a real sync against a real Pinboard account, and the offline
  Saved shelf with the radio down.

The build and the simulator need the firmware toolchain, which lives on the
maintainer's Mac, not in a cloud session.

## Pinboard API notes

- `GET https://api.pinboard.in/v1/posts/all?format=json&auth_token=USER:HEX&results=N`
  returns an array of objects: `href` (the URL), `description` (the title),
  `extended` (notes), `tags`, `time` (ISO 8601), `toread` (`"yes"`/`"no"`),
  `shared`.
- There is no server-side read-later filter on `posts/all`; `toread` is a field
  per post, filtered on the device.
- `posts/all` is rate-limited to once every few minutes; the app fetches on
  demand (opening Browse, or a manual refresh), never in a loop.
- `results=N` caps the response size. An established account can have thousands
  of bookmarks, and the whole point of a small device is not to hold all of
  them; N is a conservative cap (a few hundred) with the newest first.
