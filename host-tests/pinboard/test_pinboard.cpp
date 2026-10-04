// Host tests for PinboardCore: config parsing and URL building, the two pieces
// that are ours and verifiable without a device. Standard library only.

#include <cstdio>
#include <string>

#include "../../src/apps_local/pinboard/PinboardCore.h"

static int failures = 0;

static void check(bool cond, const char* what) {
  if (!cond) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

static void testParseConfig() {
  pin::Config c;
  check(pin::parseConfig("maciej:ABC123DEF", c), "token-only parses");
  check(c.token == "maciej:ABC123DEF", "token captured");
  check(c.tag.empty(), "no tag by default");

  pin::Config c2;
  check(pin::parseConfig("# my pinboard token\n\nmax:TOKEN99\ntag=x4\n", c2), "token with comments + tag");
  check(c2.token == "max:TOKEN99", "token past comments");
  check(c2.tag == "x4", "tag captured");

  pin::Config c3;
  check(!pin::parseConfig("# nothing but a comment\n\n", c3), "no token -> invalid");
  check(!c3.valid(), "invalid config reports invalid");

  pin::Config c4;
  // A tag line before the token, and a stray second colon line that must not win.
  check(pin::parseConfig("tag=read\nuser:FIRST\nuser:SECOND\n", c4), "tag-first still finds token");
  check(c4.token == "user:FIRST", "first token wins");
  check(c4.tag == "read", "tag before token captured");
}

static void testAllUrl() {
  pin::Config c;
  c.token = "max:TOKEN";
  const std::string u = pin::allUrl(c);
  check(contains(u, "api.pinboard.in/v1/posts/all"), "all endpoint");
  check(contains(u, "toread=yes"), "unread filter");
  check(contains(u, "format=json"), "json format");
  check(contains(u, "auth_token=max:TOKEN"), "token, colon unencoded");
  check(!contains(u, "&tag="), "no tag when unset");

  c.tag = "x 4";
  const std::string t = pin::allUrl(c);
  check(contains(t, "&tag=x%204"), "tag url-encoded");
}

static void testRecentUrl() {
  pin::Config c;
  c.token = "max:TOKEN";
  const std::string u = pin::recentUrl(c, 30);
  check(contains(u, "api.pinboard.in/v1/posts/recent"), "recent endpoint");
  check(contains(u, "count=30"), "recent count");
  check(contains(u, "format=json"), "json format");
  check(contains(u, "auth_token=max:TOKEN"), "token on recent");
  check(contains(pin::recentUrl(c, 9999), "count=100"), "recent count capped at 100");
  check(contains(pin::recentUrl(c, 0), "count=1"), "recent count floored at 1");

  c.tag = "x4";
  check(contains(pin::recentUrl(c, 30), "&tag=x4"), "recent tag filter");
}

static void testMarkReadUrl() {
  pin::Config c;
  c.token = "max:TOKEN";
  pin::Bookmark bm;
  bm.url = "https://example.com/a?b=1&c=2";
  bm.title = "Hello, World";
  bm.tags = "news tech";
  const std::string u = pin::markReadUrl(c, bm);
  check(contains(u, "posts/add"), "add endpoint");
  check(contains(u, "toread=no"), "marks read");
  check(contains(u, "replace=yes"), "replaces in place");
  check(contains(u, "url=https%3A%2F%2Fexample.com%2Fa%3Fb%3D1%26c%3D2"), "url fully encoded");
  check(contains(u, "description=Hello%2C%20World"), "title encoded");
  check(contains(u, "tags=news%20tech"), "tags encoded");
}

static void testDomainOf() {
  check(pin::domainOf("https://www.example.com/path?x=1") == "example.com", "strip scheme+www+path");
  check(pin::domainOf("http://sub.site.co.uk/") == "sub.site.co.uk", "keep subdomain");
  check(pin::domainOf("https://host:8080/x") == "host", "strip port");
  check(pin::domainOf("not a url") == "not a url", "no host -> as-is-ish");
}

int main() {
  testParseConfig();
  testAllUrl();
  testRecentUrl();
  testMarkReadUrl();
  testDomainOf();
  if (failures == 0) {
    std::printf("pinboard: all tests passed\n");
    return 0;
  }
  std::printf("pinboard: %d failure(s)\n", failures);
  return 1;
}
