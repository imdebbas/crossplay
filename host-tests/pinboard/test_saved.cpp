// Host tests for the local save-for-later index.
//
// This file is the thing that has to survive: the network can be absent, the
// card can be pulled mid-write, and a reader should still open the device and
// find their articles. So the tests care most about the failure paths -- a
// half-written row, a title full of tabs, an index from a future version --
// because those are what stand between a damaged file and a lost library.
//
// It is a close sibling of host-tests/hackernews/test_saved.cpp, because the
// format is: Pinboard reuses Hacker News's library shape deliberately.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/pinboard/PinboardSaved.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_saved.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_saved.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

pin::SavedArticle make(const char* title, const char* url, const uint32_t at = 1000) {
  pin::SavedArticle article;
  article.id = pin::savedIdFor(url);
  article.title = title;
  article.url = url;
  article.savedAt = at;
  return article;
}

void testIdIsStableAndPerUrl() {
  // The same URL must give the same name across reboots, or a re-save writes a
  // second copy and the first is orphaned on the card forever.
  CHECK_EQ(pin::savedIdFor("https://example.com/a"), pin::savedIdFor("https://example.com/a"));
  CHECK(pin::savedIdFor("https://example.com/a") != pin::savedIdFor("https://example.com/b"));

  // Permutations of the same characters, which any hash that merely adds bytes
  // would collide -- and a collision here means opening one saved article and
  // getting a different one.
  CHECK(pin::savedIdFor("https://example.com/ab") != pin::savedIdFor("https://example.com/ba"));
  CHECK(pin::savedIdFor("https://a.com/1") != pin::savedIdFor("https://a.com/2"));

  // Eight lowercase hex characters: short enough for a FAT filename, and
  // nothing in it needs escaping.
  const std::string id = pin::savedIdFor("https://example.com/a");
  CHECK(id.size() == 8);
  for (const char c : id) CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));

  CHECK(pin::savedIdFor("").size() == 8);
}

void testRoundTrip() {
  std::vector<pin::SavedArticle> articles{
      make("A Story About Things", "https://example.com/one", 111),
      make("Another One", "https://example.com/two", 222),
  };

  std::vector<pin::SavedArticle> parsed;
  CHECK(pin::parseSavedIndex(pin::serializeSavedIndex(articles), parsed));
  CHECK(parsed.size() == 2);
  if (parsed.size() == 2) {
    CHECK_EQ(parsed[0].title, "A Story About Things");
    CHECK_EQ(parsed[0].url, "https://example.com/one");
    CHECK(parsed[0].savedAt == 111);
    // Order is the order it was written. A saved list that reshuffles itself
    // between boots is unusable.
    CHECK_EQ(parsed[1].title, "Another One");
  }
}

void testEmptyLibrary() {
  std::vector<pin::SavedArticle> parsed;
  const std::string empty = pin::serializeSavedIndex({});
  CHECK(pin::parseSavedIndex(empty, parsed));
  CHECK(parsed.empty());

  // A header with no rows is a valid empty library, not a parse failure. It is
  // what the first save writes into.
  CHECK(pin::parseSavedIndex("pinsaved 1\n", parsed));
  CHECK(parsed.empty());
  CHECK(pin::parseSavedIndex("pinsaved 1", parsed));
  CHECK(parsed.empty());
}

void testNotOurFile() {
  std::vector<pin::SavedArticle> parsed;
  CHECK(!pin::parseSavedIndex("", parsed));
  CHECK(!pin::parseSavedIndex("{\"json\": true}", parsed));
  CHECK(!pin::parseSavedIndex("some other file entirely\n", parsed));
  // Hacker News's file is not ours, even though the shape is identical: the
  // magic is what keeps one app from reading the other's library.
  CHECK(!pin::parseSavedIndex("hnsaved 2\nabc\t1\tTitle\thttps://example.com\n", parsed));
}

void testDamageCostsOneEntry() {
  // A power cut mid-write leaves a truncated last row. Everything before it
  // must still load: this is the whole argument for a line format over one
  // document that either parses or does not.
  std::string text = pin::serializeSavedIndex({
      make("First", "https://example.com/1"),
      make("Second", "https://example.com/2"),
  });
  text += "0badbeef\t999\tA title with no url yet";

  std::vector<pin::SavedArticle> parsed;
  CHECK(pin::parseSavedIndex(text, parsed));
  CHECK(parsed.size() == 3);  // the partial row still has an id and a title
  if (parsed.size() == 3) CHECK_EQ(parsed[2].url, "");

  // A row missing its title is damage and is dropped, but its neighbours live.
  std::string holed = pin::serializeSavedIndex({make("Keep Me", "https://example.com/keep")});
  holed += "\t\t\t\n";
  holed += "deadbeef\t5\tAlso Keep Me\thttps://example.com/also\n";
  std::vector<pin::SavedArticle> survivors;
  CHECK(pin::parseSavedIndex(holed, survivors));
  CHECK(survivors.size() == 2);
  if (survivors.size() == 2) {
    CHECK_EQ(survivors[0].title, "Keep Me");
    CHECK_EQ(survivors[1].title, "Also Keep Me");
  }
}

void testFieldsCannotSwallowEachOther() {
  // A title carrying a tab would end its own field and shift every one after
  // it, so the URL would be read out of the middle of the title.
  pin::SavedArticle nasty = make("Tabbed\tTitle\nWith Newlines", "https://example.com/x");
  std::vector<pin::SavedArticle> parsed;
  CHECK(pin::parseSavedIndex(pin::serializeSavedIndex({nasty}), parsed));
  CHECK(parsed.size() == 1);
  if (parsed.size() == 1) {
    CHECK_EQ(parsed[0].title, "Tabbed Title With Newlines");
    CHECK_EQ(parsed[0].url, "https://example.com/x");
  }

  CHECK_EQ(pin::sanitizeField("  padded  "), "padded");
  CHECK_EQ(pin::sanitizeField("a\t\tb"), "a b");
  CHECK_EQ(pin::sanitizeField(""), "");
  CHECK_EQ(pin::sanitizeField("\t\n "), "");

  // sanitizeField itself must NOT fold. It runs on the URL column as well as
  // the title, savedIdFor() hashes the URL, and an article whose stored URL no
  // longer hashes to its id can never be unsaved again.
  CHECK_EQ(pin::sanitizeField("They\xe2\x80\x99"
                              "re"),
           "They\xe2\x80\x99"
           "re");
}

void testTypographyIsFoldedButUrlsAreNot() {
  // A title with a curly apostrophe, a curly pair, an em dash and an ellipsis.
  // The reading cut has no glyph for any of them, so the title is folded at
  // BOTH ends of the index: written folded, and folded again on the read, which
  // is what makes an index saved before this existed stop having holes.
  pin::SavedArticle article;
  article.id = pin::savedIdFor("https://example.com/one");
  article.savedAt = 1000;
  article.title =
      "It\xe2\x80\x99"
      "s a \xe2\x80\x9c"
      "big\xe2\x80\x9d"
      " one \xe2\x80\x94"
      " really\xe2\x80\xa6";
  article.url = "https://example.com/one";

  std::vector<pin::SavedArticle> read;
  CHECK(pin::parseSavedIndex(pin::serializeSavedIndex({article}), read));
  CHECK(read.size() == 1);
  if (read.size() == 1) {
    CHECK_EQ(read[0].title, "It's a \"big\" one -- really...");
    // The URL survives byte for byte and still hashes to the id it was saved
    // under. This is the assertion that stops the fold being applied one field
    // to the left.
    CHECK_EQ(read[0].url, "https://example.com/one");
    CHECK_EQ(pin::savedIdFor(read[0].url), read[0].id);
  }

  // A URL with a real non-ASCII character in it: unencoded paths happen, and a
  // fold would rewrite one into a different address.
  pin::SavedArticle exotic;
  exotic.url =
      "https://example.com/caf\xc3\xa9\xe2\x80\x94"
      "notes";
  exotic.id = pin::savedIdFor(exotic.url);
  exotic.savedAt = 2000;
  exotic.title = "Notes";
  CHECK(pin::parseSavedIndex(pin::serializeSavedIndex({exotic}), read));
  CHECK(read.size() == 1);
  if (read.size() == 1) {
    CHECK_EQ(read[0].url, exotic.url);
    CHECK_EQ(pin::savedIdFor(read[0].url), exotic.id);
  }

  // The letters the reading cut can draw are left in the title.
  pin::SavedArticle accented;
  accented.url = "https://example.com/two";
  accented.id = pin::savedIdFor(accented.url);
  accented.savedAt = 3000;
  accented.title =
      "Bj\xc3\xb6"
      "rn in a caf\xc3\xa9";
  CHECK(pin::parseSavedIndex(pin::serializeSavedIndex({accented}), read));
  if (read.size() == 1)
    CHECK_EQ(read[0].title,
             "Bj\xc3\xb6"
             "rn in a caf\xc3\xa9");
}

void testUnknownVersionIsLeftAlone() {
  // A library written by a newer build is not something to guess at. Half-read
  // and then rewritten is how a version bump destroys the thing it protects.
  std::vector<pin::SavedArticle> parsed;
  CHECK(!pin::parseSavedIndex("pinsaved 99\nabc\t1\tTitle\thttps://example.com\n", parsed));
  CHECK(parsed.empty());
  CHECK(!pin::parseSavedIndex("pinsaved 0\n", parsed));
  CHECK(!pin::parseSavedIndex("pinsaved x\n", parsed));
}

void testWeWriteTheCurrentVersion() {
  // What we write must be what we read, or the next boot migrates its own
  // output. The round trip above would pass even if both were wrong, so the
  // header is asserted literally.
  const std::string written = pin::serializeSavedIndex({make("A", "https://example.com/a")});
  CHECK(written.compare(0, 11, "pinsaved 1\n") == 0);
}

}  // namespace

int main() {
  testIdIsStableAndPerUrl();
  testRoundTrip();
  testEmptyLibrary();
  testNotOurFile();
  testDamageCostsOneEntry();
  testFieldsCannotSwallowEachOther();
  testTypographyIsFoldedButUrlsAreNot();
  testUnknownVersionIsLeftAlone();
  testWeWriteTheCurrentVersion();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
