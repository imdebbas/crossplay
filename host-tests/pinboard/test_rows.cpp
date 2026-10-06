// Host tests for the two shelves' rows.
//
// Like Hacker News's, these exist for a bug no amount of testing the pieces
// would catch: every function correct, and one call site that changed the view
// without rebuilding the rows, so the taps acted on the wrong shelf. The thing
// under test is the staleness question the paint asks before it draws, and the
// three empty screens staying three.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/apps_local/pinboard/PinboardRows.h"

namespace {

int checksRun = 0;
int checksFailed = 0;

void check(const bool condition, const char* what, const int line) {
  ++checksRun;
  if (!condition) {
    ++checksFailed;
    std::printf("FAIL test_rows.cpp:%d  %s\n", line, what);
  }
}

void checkEqual(const std::string& actual, const std::string& expected, const char* what, const int line) {
  ++checksRun;
  if (actual != expected) {
    ++checksFailed;
    std::printf("FAIL test_rows.cpp:%d  %s\n  expected [%s]\n  actual   [%s]\n", line, what, expected.c_str(),
                actual.c_str());
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define CHECK_EQ(actual, expected) checkEqual((actual), (expected), #actual, __LINE__)

std::vector<pin::Bookmark> browse() {
  std::vector<pin::Bookmark> list;
  for (int i = 0; i < 3; ++i) {
    pin::Bookmark b;
    b.url = "https://example.com/" + std::to_string(i);
    b.title = "Bookmark " + std::to_string(i);
    b.toRead = true;
    list.push_back(b);
  }
  return list;
}

std::vector<pin::SavedArticle> shelf() {
  std::vector<pin::SavedArticle> saved;
  for (int i = 0; i < 2; ++i) {
    pin::SavedArticle article;
    article.title = "Saved article " + std::to_string(i);
    saved.push_back(article);
  }
  return saved;
}

void testTheViewChangingMakesTheRowsStale() {
  pin::Rows rows;
  const auto marks = browse();
  const auto saved = shelf();
  const std::vector<bool> none;

  CHECK(pin::rowsStale(rows, pin::ListView::Browse));

  pin::buildRows(rows, pin::ListView::Browse, marks, none, saved);
  CHECK(!pin::rowsStale(rows, pin::ListView::Browse));
  CHECK(pin::rowsStale(rows, pin::ListView::Saved));

  pin::buildRows(rows, pin::ListView::Saved, marks, none, saved);
  CHECK(!pin::rowsStale(rows, pin::ListView::Saved));
  CHECK(pin::rowsStale(rows, pin::ListView::Browse));
}

void testEachViewsRowsAreItsOwn() {
  pin::Rows rows;
  const auto marks = browse();
  const auto saved = shelf();
  const std::vector<bool> none;

  pin::buildRows(rows, pin::ListView::Browse, marks, none, saved);
  CHECK_EQ(std::to_string(rows.size()), std::to_string(marks.size()));
  CHECK_EQ(rows.titles[0], "Bookmark 0");
  CHECK_EQ(rows.titles[2], "Bookmark 2");
  // Nothing picked yet: every row carries the empty box.
  CHECK_EQ(rows.values[0], pin::kUnpickedMark);
  CHECK_EQ(rows.values[2], pin::kUnpickedMark);

  pin::buildRows(rows, pin::ListView::Saved, marks, none, saved);
  CHECK_EQ(std::to_string(rows.size()), std::to_string(saved.size()));
  CHECK_EQ(rows.titles[0], "Saved article 0");
  CHECK_EQ(std::to_string(rows.values.size()), std::to_string(rows.titles.size()));
  CHECK_EQ(rows.values[0], "");
}

void testPickMarksTrackTheSelection() {
  pin::Rows rows;
  const auto marks = browse();
  const auto saved = shelf();

  std::vector<bool> picked{true, false, true};
  pin::buildRows(rows, pin::ListView::Browse, marks, picked, saved);
  CHECK_EQ(rows.values[0], pin::kPickedMark);
  CHECK_EQ(rows.values[1], pin::kUnpickedMark);
  CHECK_EQ(rows.values[2], pin::kPickedMark);

  // A pick vector shorter than the list -- which is exactly the shape during a
  // rebuild that races a toggle -- must not index past its end. The missing
  // tail reads as not picked.
  std::vector<bool> shortPick{true};
  pin::buildRows(rows, pin::ListView::Browse, marks, shortPick, saved);
  CHECK_EQ(rows.values[0], pin::kPickedMark);
  CHECK_EQ(rows.values[1], pin::kUnpickedMark);
  CHECK_EQ(rows.values[2], pin::kUnpickedMark);
}

void testTitleFallsBackToTheUrl() {
  // A Pinboard bookmark with no description is common, and an empty row is
  // useless, so displayTitle's URL fallback shows through the row builder.
  pin::Rows rows;
  std::vector<pin::Bookmark> list(1);
  list[0].url = "https://www.example.com/an/article/";
  list[0].title = "";
  pin::buildRows(rows, pin::ListView::Browse, list, {}, {});
  CHECK_EQ(rows.titles[0], "example.com/an/article");
}

void testRebuildingDropsTheFittedLabels() {
  pin::Rows rows;
  const auto marks = browse();
  const auto saved = shelf();

  pin::buildRows(rows, pin::ListView::Browse, marks, {}, saved);
  rows.labels = {"fitted 0", "fitted 1", "fitted 2"};
  rows.fitted = true;

  pin::buildRows(rows, pin::ListView::Saved, marks, {}, saved);
  CHECK(!rows.fitted);
  CHECK_EQ(std::to_string(rows.labels.size()), "0");
}

void testInvalidateCoversAShelfThatChangedUnderTheSameView() {
  pin::Rows rows;
  auto saved = shelf();

  pin::buildRows(rows, pin::ListView::Saved, {}, {}, saved);
  CHECK(!pin::rowsStale(rows, pin::ListView::Saved));

  saved.pop_back();
  CHECK(!pin::rowsStale(rows, pin::ListView::Saved));  // the view is unchanged

  rows.invalidate();
  CHECK(pin::rowsStale(rows, pin::ListView::Saved));
  pin::buildRows(rows, pin::ListView::Saved, {}, {}, saved);
  CHECK_EQ(std::to_string(rows.size()), "1");
}

// --- the empty screens -------------------------------------------------------

void testBrowseHasFourDistinctEmptyScreens() {
  const pin::EmptyState noToken = pin::emptyState(pin::ListView::Browse, false, false, false);
  const pin::EmptyState notLoaded = pin::emptyState(pin::ListView::Browse, true, false, false);
  const pin::EmptyState failed = pin::emptyState(pin::ListView::Browse, true, false, true);
  const pin::EmptyState loadedEmpty = pin::emptyState(pin::ListView::Browse, true, true, false);

  // Each says something, and each offers its own way onward.
  CHECK(noToken.headline != nullptr && noToken.actionLabel != nullptr);
  CHECK(notLoaded.headline != nullptr && notLoaded.actionLabel != nullptr);
  CHECK(failed.headline != nullptr && failed.actionLabel != nullptr);
  CHECK(loadedEmpty.headline != nullptr && loadedEmpty.actionLabel != nullptr);

  // Only the no-token screen opens the keyboard; the rest ask for the network.
  CHECK(noToken.action == pin::EmptyAction::EnterToken);
  CHECK(notLoaded.action == pin::EmptyAction::LoadList);
  CHECK(failed.action == pin::EmptyAction::LoadList);
  CHECK(loadedEmpty.action == pin::EmptyAction::LoadList);

  // They do not collapse into each other: the first-run invitation must not
  // read as an error, and a dropped connection must not read as "nothing here".
  CHECK(std::string(noToken.headline) != std::string(failed.headline));
  CHECK(std::string(notLoaded.headline) != std::string(failed.headline));
  CHECK(std::string(loadedEmpty.headline) != std::string(failed.headline));

  // The dropped-connection wording is the shared one, naming the Saved shelf.
  CHECK_EQ(std::string(failed.headline), std::string(pin::kUnreachableHeadline));
  CHECK_EQ(std::string(failed.message), std::string(pin::kUnreachableMessage));
  CHECK(std::string(pin::kUnreachableMessage).find("Saved") != std::string::npos);
}

void testTheSavedShelfOffersNoControl() {
  // It is the half that needs no network, and the only control it could carry
  // would reach for the other half. The failure flag from Browse must not leak
  // into it either.
  CHECK(pin::emptyState(pin::ListView::Saved, true, true, false).actionLabel == nullptr);
  CHECK(pin::emptyState(pin::ListView::Saved, false, false, true).actionLabel == nullptr);
  CHECK(pin::emptyState(pin::ListView::Saved, true, true, false).action == pin::EmptyAction::None);
}

}  // namespace

int main() {
  testTheViewChangingMakesTheRowsStale();
  testEachViewsRowsAreItsOwn();
  testPickMarksTrackTheSelection();
  testTitleFallsBackToTheUrl();
  testRebuildingDropsTheFittedLabels();
  testInvalidateCoversAShelfThatChangedUnderTheSameView();
  testBrowseHasFourDistinctEmptyScreens();
  testTheSavedShelfOffersNoControl();

  std::printf("%d checks, %d failed\n", checksRun, checksFailed);
  return checksFailed == 0 ? 0 : 1;
}
