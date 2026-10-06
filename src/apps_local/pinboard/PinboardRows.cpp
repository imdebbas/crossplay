#include "PinboardRows.h"

namespace pin {

bool rowsStale(const Rows& rows, const ListView view) { return !rows.sourced || rows.builtFor != view; }

void buildRows(Rows& rows, const ListView view, const std::vector<Bookmark>& bookmarks,
               const std::vector<bool>& picked, const std::vector<SavedArticle>& saved) {
  rows.titles.clear();
  rows.values.clear();
  // Cleared with the titles, never separately: `labels` is indexed in lockstep
  // with them at paint time.
  rows.labels.clear();
  rows.fitted = false;

  if (view == ListView::Saved) {
    rows.titles.reserve(saved.size());
    rows.values.reserve(saved.size());
    for (const SavedArticle& article : saved) {
      rows.titles.push_back(article.title);
      // One entry per row even though the shelf draws no second column, so the
      // two vectors stay indexable together without a length check at paint.
      rows.values.emplace_back();
    }
  } else {
    rows.titles.reserve(bookmarks.size());
    rows.values.reserve(bookmarks.size());
    for (size_t i = 0; i < bookmarks.size(); ++i) {
      rows.titles.push_back(displayTitle(bookmarks[i]));
      // A pick state out of range reads as not picked, so a rebuild that races
      // a toggle (the vectors resized a beat apart) cannot index past the end.
      const bool isPicked = i < picked.size() && picked[i];
      rows.values.emplace_back(isPicked ? kPickedMark : kUnpickedMark);
    }
  }

  rows.builtFor = view;
  rows.sourced = true;
}

EmptyState emptyState(const ListView view, const bool hasToken, const bool fetched, const bool fetchFailed) {
  EmptyState state;
  if (view == ListView::Saved) {
    // No control, deliberately. An empty shelf on a new device is not a fault
    // and not something a button can fix; a control here would offer to reach
    // Pinboard from the one screen that is about not needing it.
    state.headline = "NOTHING SAVED YET";
    state.message = "Pick bookmarks in Browse and tap SAVE.";
    return state;
  }

  if (!hasToken) {
    // The first-run state. The token is the only setup this app has, and this
    // is where it is offered -- the empty-state control opens the keyboard.
    state.headline = "CONNECT PINBOARD";
    state.message = "Enter your Pinboard API token to list your saved bookmarks.";
    state.actionLabel = "ENTER TOKEN";
    state.action = EmptyAction::EnterToken;
    return state;
  }

  if (fetchFailed) {
    // Taken from the shared pair rather than written here, so this screen and
    // the reader's own failure notice cannot promise different things about the
    // same dropped connection. See kUnreachableMessage.
    state.headline = kUnreachableHeadline;
    state.message = kUnreachableMessage;
    state.actionLabel = "TRY AGAIN";
    state.action = EmptyAction::LoadList;
    return state;
  }

  if (!fetched) {
    // Token present, nothing loaded yet. It says what is missing and what is
    // not, because the Saved half one tap away is why the app opens without a
    // radio.
    state.headline = "NOT LOADED YET";
    state.message = "The bookmark list needs a connection. Saved articles do not.";
    state.actionLabel = "LOAD";
    state.action = EmptyAction::LoadList;
    return state;
  }

  // Fetched, and nothing to read: the account has no bookmarks flagged "to
  // read". A reload is offered rather than nothing, in case the flag was just
  // set on the phone.
  state.headline = "NOTHING TO READ";
  state.message = "No bookmarks are flagged 'to read' in Pinboard.";
  state.actionLabel = "RELOAD";
  state.action = EmptyAction::LoadList;
  return state;
}

}  // namespace pin
