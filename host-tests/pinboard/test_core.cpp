// Host tests for the Pinboard-specific brains: the account token, the list
// query, and the bookmark model the screens draw. All pure transforms, so the
// failure paths that matter are the human ones -- a token pasted with a
// trailing newline, a bookmark with no description, a typo'd result count.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/pinboard/PinboardCore.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_core.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

void testTokenParse() {
  pin::AuthToken t = pin::parseAuthToken("maxd:ABC123DEF456");
  CHECK(t.valid);
  CHECK_EQ(t.user, "maxd");
  CHECK_EQ(t.secret, "ABC123DEF456");

  // The keyboard and a paste leave whitespace; it must not become part of the
  // credential, or every request 401s for a reason nothing on screen explains.
  t = pin::parseAuthToken("  maxd:ABC123  ");
  CHECK(t.valid);
  CHECK_EQ(t.user, "maxd");
  CHECK_EQ(t.secret, "ABC123");

  t = pin::parseAuthToken("maxd:ABC123\n");
  CHECK(t.valid);
  CHECK_EQ(t.secret, "ABC123");

  // A secret that itself contains a colon is kept whole: only the FIRST colon
  // splits user from secret.
  t = pin::parseAuthToken("maxd:AA:BB");
  CHECK(t.valid);
  CHECK_EQ(t.user, "maxd");
  CHECK_EQ(t.secret, "AA:BB");
}

void testTokenRejects() {
  // No colon, empty halves, and nothing but whitespace are all "this is not a
  // Pinboard token", caught before anything is stored.
  CHECK(!pin::parseAuthToken("").valid);
  CHECK(!pin::parseAuthToken("   ").valid);
  CHECK(!pin::parseAuthToken("justausername").valid);
  CHECK(!pin::parseAuthToken(":secretonly").valid);
  CHECK(!pin::parseAuthToken("useronly:").valid);
  CHECK(!pin::parseAuthToken(":").valid);

  CHECK_EQ(pin::authParam(pin::parseAuthToken("bad")), "");
  CHECK_EQ(pin::authParam(pin::parseAuthToken("maxd:ABC")), "maxd:ABC");
}

void testListPath() {
  const pin::AuthToken t = pin::parseAuthToken("maxd:ABC123");
  const std::string path = pin::listPath(t, pin::kDefaultMaxResults);
  CHECK(contains(path, "/v1/posts/all"));
  CHECK(contains(path, "format=json"));
  CHECK(contains(path, "auth_token=maxd:ABC123"));
  CHECK(contains(path, "results=400"));

  // A result count is clamped both ways, so a typo cannot ask Pinboard for the
  // whole account or for nothing.
  CHECK(contains(pin::listPath(t, -5), "results=1"));
  CHECK(contains(pin::listPath(t, 0), "results=1"));
  CHECK(contains(pin::listPath(t, 999999), "results=" + std::to_string(pin::kMaxResultsCeiling)));

  // An invalid token builds no request at all, so a bad token can never reach
  // the wire with a half-formed query.
  CHECK_EQ(pin::listPath(pin::parseAuthToken("nope"), 10), "");
}

void testDisplayTitle() {
  pin::Bookmark b;
  b.url = "https://example.com/a-great-read";
  b.title = "A Great Read";
  CHECK_EQ(pin::displayTitle(b), "A Great Read");

  // Folded for the reading cut, which has no glyph for curly punctuation.
  b.title =
      "It\xe2\x80\x99"
      "s \xe2\x80\x9c"
      "Good\xe2\x80\x9d";
  CHECK_EQ(pin::displayTitle(b), "It's \"Good\"");

  // No description: the URL, cleaned to what a person reads.
  pin::Bookmark n;
  n.url = "https://www.example.com/path/to/thing/";
  CHECK_EQ(pin::displayTitle(n), "example.com/path/to/thing");

  n.url = "http://news.site.org/story";
  CHECK_EQ(pin::displayTitle(n), "news.site.org/story");

  n.url = "https://example.com";
  CHECK_EQ(pin::displayTitle(n), "example.com");

  // A description that is only whitespace is treated as absent by the caller
  // (Pinboard sends ""), so the fallback stands in. Empty description here:
  pin::Bookmark empty;
  empty.url = "https://example.com/x";
  empty.title = "";
  CHECK_EQ(pin::displayTitle(empty), "example.com/x");
}

void testOnlyToRead() {
  std::vector<pin::Bookmark> all(4);
  all[0].url = "https://a";
  all[0].toRead = true;
  all[1].url = "https://b";
  all[1].toRead = false;
  all[2].url = "https://c";
  all[2].toRead = true;
  all[3].url = "https://d";
  all[3].toRead = false;

  const std::vector<pin::Bookmark> unread = pin::onlyToRead(all);
  CHECK(unread.size() == 2);
  if (unread.size() == 2) {
    // Order is preserved, so the read-later list reads the way Pinboard ordered
    // it rather than reshuffled.
    CHECK_EQ(unread[0].url, "https://a");
    CHECK_EQ(unread[1].url, "https://c");
  }

  CHECK(pin::onlyToRead({}).empty());
}

}  // namespace

int main() {
  testTokenParse();
  testTokenRejects();
  testListPath();
  testDisplayTitle();
  testOnlyToRead();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
