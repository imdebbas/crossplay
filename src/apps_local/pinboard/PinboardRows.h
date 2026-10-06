#pragma once

// The two list shelves' rows, and the one thing that has to be true about them.
//
// A direct sibling of HackerNewsRows: the list draws two shelves -- the
// bookmarks fetched from Pinboard (Browse) and the articles kept on the card
// (Saved) -- from one set of row vectors, and a tap acts on whatever sits at the
// tapped INDEX in the shelf the view says is showing. Carrying the view the rows
// were built FOR, and asking before every paint whether it still matches, is
// what makes "the titles say one shelf while the taps act on the other"
// unrepresentable. See HackerNewsRows.h for the bug that shaped this.
//
// Freestanding: a transform from a shelf to some strings, driven by
// host-tests/pinboard/ with no panel.

#include <cstdint>
#include <string>
#include <vector>

#include "PinboardCore.h"
#include "PinboardSaved.h"

namespace pin {

// Which shelf the list is showing. An enum rather than a bool because it is
// stored next to the rows it produced, so `builtFor == Browse` cannot be
// mistaken for its opposite.
enum class ListView : uint8_t { Browse, Saved };

// The pick marker shown in the Browse row's value column. A drawn checkbox (the
// shelf chooser's filled-vs-outline box) is the polish the three-variant render
// pass chooses on the Mac; these ASCII marks are the device-independent
// stand-in, so the row logic is host-testable and the screen stays trivial.
constexpr const char* kPickedMark = "[x]";
constexpr const char* kUnpickedMark = "[ ]";

struct Rows {
  // As the source wrote them. Kept because fitting is lossy and the fitted
  // label cannot be re-fitted to a different width.
  std::vector<std::string> titles;
  // Browse: the pick marker. Saved: empty strings, which the shelf has no
  // second column for but still needs one entry per row so the two vectors can
  // be indexed together.
  std::vector<std::string> values;
  // Fitted to the row width, which needs a draw target, so filled at paint time
  // rather than here. Cleared by every rebuild.
  std::vector<std::string> labels;

  ListView builtFor = ListView::Browse;
  bool sourced = false;
  bool fitted = false;

  size_t size() const { return titles.size(); }

  // Forces the next paint to rebuild, for the case view-tracking cannot see:
  // the shelf changed while the view did not. Saving an article, or toggling a
  // pick, both leave `builtFor` correct and the contents out of date.
  void invalidate() {
    sourced = false;
    fitted = false;
  }
};

// Whether the rows on hand can be drawn as `view`. True before the first build
// and after any view change.
bool rowsStale(const Rows& rows, ListView view);

// Fills `rows` from whichever shelf `view` names and marks them built for it.
// For Browse, `picked` gives each bookmark's pick state (a shorter or empty
// vector reads as "none picked", so a rebuild mid-toggle is safe); `bookmarks`
// titles come from displayTitle(). The shelf not named is ignored rather than
// required empty, so a caller can pass both and let the view decide.
void buildRows(Rows& rows, ListView view, const std::vector<Bookmark>& bookmarks, const std::vector<bool>& picked,
               const std::vector<SavedArticle>& saved);

// What an empty list says, and whether it offers a way to fill itself.
//
// Browse has three empty situations and they must stay three: no token yet (the
// first-run state, offering to enter one), a fetch that failed (an error,
// offering to retry), and a token present but the list not yet loaded or
// genuinely empty (an invitation to load). An empty Saved shelf is the ordinary
// state of a new device and is COMPLETE -- nothing to fetch, so no control.
enum class EmptyAction : uint8_t { None, EnterToken, LoadList };

struct EmptyState {
  const char* headline = nullptr;
  const char* message = nullptr;
  // nullptr label means no control (the Saved shelf's answer, and only its).
  const char* actionLabel = nullptr;
  EmptyAction action = EmptyAction::None;
};

EmptyState emptyState(ListView view, bool hasToken, bool fetched, bool fetchFailed);

// The one thing a dropped connection says, wherever it is discovered -- the
// list's empty state and the reader's failure notice both take it from here so
// neither can promise something different about the same lost radio. It names
// the Saved shelf on purpose: that is the half that still works with no network.
constexpr const char* kUnreachableHeadline = "NO LUCK";
constexpr const char* kUnreachableMessage = "Could not reach Pinboard. Saved articles still work.";

}  // namespace pin
