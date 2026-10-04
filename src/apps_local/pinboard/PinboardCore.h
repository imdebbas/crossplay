#pragma once

// The Pinboard-specific logic, freestanding on purpose: no Arduino, no
// renderer, no SD card, no ArduinoJson. host-tests/pinboard/ drives all of it
// on a laptop, the same way HackerNewsCore is driven.
//
// What lives here is exactly the part that is ours and testable without a
// device: reading the token out of a config file, and building the two Pinboard
// URLs we call. The unread list itself is JSON parsed on the device with
// ArduinoJson (see PinboardActivity), the same split HackerNews uses for its
// front page; and turning a saved article's HTML into readable text is reused
// wholesale from hn:: (the r.jina.ai extractor + the readability gate), so it is
// not duplicated here.

#include <string>
#include <string_view>

namespace pin {

// The user's Pinboard credentials, read from /.crosspoint/pinboard.cfg.
//
//   username:APITOKEN          <- required, the auth_token from
//                                 https://pinboard.in/settings/password
//   tag=x4                     <- optional, only pull unread items with this tag
//
// A token rather than a password: Pinboard's API authenticates with the
// per-account token, so nothing here ever holds the account password.
struct Config {
  std::string token;  // "username:HEXTOKEN"
  std::string tag;    // empty for every unread item
  bool valid() const { return !token.empty(); }
};

// Parse pinboard.cfg. The first non-empty, non-comment line that looks like a
// token (contains ':') becomes the token; a `tag=` line narrows the list.
// Returns false when no token was found, so the app can show the "add your
// token" notice instead of fetching nothing.
bool parseConfig(std::string_view text, Config& out);

// A Pinboard bookmark as the unread list delivers it. The fields mark-read has
// to preserve are kept so re-adding with toread=no does not clobber them.
struct Bookmark {
  std::string url;       // href
  std::string title;     // description
  std::string extended;  // the longer note
  std::string tags;      // space-separated
  std::string hash;      // Pinboard's md5 of the URL; a stable id
  std::string time;      // ISO 8601
};

// Percent-encode everything outside the RFC 3986 unreserved set.
std::string urlEncode(std::string_view s);

// The reader list is the N most recent bookmarks plus everything still flagged
// to-read, de-duplicated. That is two calls:

// GET the N most recent bookmarks (any read state):
//   https://api.pinboard.in/v1/posts/recent?count=N&format=json&auth_token=...
// count is clamped to 1..100 (Pinboard's ceiling). &tag= appended when set.
std::string recentUrl(const Config& cfg, int count);

// GET the unread list as JSON:
//   https://api.pinboard.in/v1/posts/all?toread=yes&format=json&auth_token=...
// with &tag= appended when the config narrows it.
std::string allUrl(const Config& cfg);

// Re-add a bookmark with toread=no and replace=yes, which is how Pinboard marks
// something read. The other fields are sent back unchanged so the note and tags
// survive the round trip.
std::string markReadUrl(const Config& cfg, const Bookmark& bm);

// The registrable host of a URL, without scheme or leading "www.", for a list
// subtitle like "example.com". Empty when the URL has no host.
std::string domainOf(std::string_view url);

}  // namespace pin
