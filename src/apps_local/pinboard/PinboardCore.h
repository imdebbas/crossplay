#pragma once

// The Pinboard-specific brains, freestanding on purpose: no Arduino, no
// renderer, no SD card, so host-tests/pinboard/ can drive all of it on a
// laptop. Everything here is a pure transform from text the account gives us
// into the model the screens draw.
//
// What is NOT here: the readability gate and the Markdown flattener that turn a
// fetched page into article text. A Pinboard bookmark is a URL and so is a
// Hacker News story, and both apps reach the same extractor (r.jina.ai), so the
// answer-parser is shared rather than twinned -- the Activity calls hn::
// (HackerNewsCore.h) for splitExtractorResponse / paragraphsFromMarkdown /
// readsAsProse / urlCanBeArticle. Keeping one copy is the fork's fix-the-twin
// rule; the alternative is two readability gates that drift.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pin {

// --- The account token -------------------------------------------------------
//
// Pinboard authenticates with an API token of the form "username:HEXSTRING",
// taken from the account's own password/settings page. It is the only
// credential this app holds, it is typed once on the device keyboard, and it
// never leaves the card except as the auth_token parameter of a TLS request to
// api.pinboard.in.

struct AuthToken {
  std::string user;    // the pinboard.in username, before the colon
  std::string secret;  // everything after the first colon
  bool valid = false;
};

// Parse a pasted token. Surrounding whitespace is trimmed (the keyboard can
// leave a trailing space, and a copy-paste can bring a newline), then it must
// be exactly "<non-empty>:<non-empty>". The secret's alphabet is NOT checked:
// Pinboard's secret is hex, but rejecting a non-hex secret here would refuse a
// token the service itself would accept, and a wrong token fails cleanly on the
// first fetch with a sentence either way.
AuthToken parseAuthToken(std::string_view input);

// "user:secret" -- the value the auth_token query parameter takes. Empty for an
// invalid token, so a request can never be built from one.
std::string authParam(const AuthToken& token);

// --- The list query ----------------------------------------------------------

// A conservative cap on how many bookmarks one fetch pulls. An established
// account can hold thousands and a small device should not try to; the newest
// are returned first.
constexpr int kDefaultMaxResults = 400;
constexpr int kMaxResultsCeiling = 2000;

// The posts/all request as a path + query string, with no host (the Activity's
// HTTP layer adds "https://api.pinboard.in"). format=json, the auth_token, and
// a results cap clamped to [1, kMaxResultsCeiling] so a typo cannot ask for the
// whole account. Empty when the token is invalid.
std::string listPath(const AuthToken& token, int maxResults = kDefaultMaxResults);

// --- The model the screens draw ----------------------------------------------

struct Bookmark {
  std::string url;    // href: the thing fetched, and the library key
  std::string title;  // description, folded for display (see displayTitle)
  std::string tags;   // space-separated, as Pinboard returns them
  bool toRead = false;
};

// What a browse row shows. The description when it has one; otherwise a
// readable form of the URL -- scheme and a leading "www." dropped, trailing
// slash removed -- because a bookmark with no description is common and an
// empty row is useless. Never empty for a non-empty URL. The title is folded
// for the reading cut, which has no glyph for curly quotes or em dashes; the
// URL fallback is folded too, since it is shown, not requested.
std::string displayTitle(const Bookmark& bookmark);

// Keep only the bookmarks flagged to-read, in the order given. The read-later
// view: posts/all carries no server-side to-read filter, so it is applied here.
std::vector<Bookmark> onlyToRead(const std::vector<Bookmark>& all);

}  // namespace pin
