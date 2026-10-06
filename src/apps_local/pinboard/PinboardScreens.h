#pragma once

// The Pinboard screens. Freestanding builders in the HackerNewsScreens mould: a
// model in, a drawn frame out, no renderer and no Activity, so host-tests/ui/
// can assert what they drew and what they made tappable.
//
// Three screens -- a list (Browse or Saved), a reader, and a notice -- the same
// shape Hacker News has, because the jobs are the same. Two differences:
//
//   * Browse rows carry a PICK marker and a tap toggles it rather than opening.
//     A SAVE chip on the band turns the picked ones into saved articles. This is
//     the "choose several, then save" flow; Hacker News opens one and saves it.
//   * There are no comments, so the reader has no swap control -- just the page
//     arrows and, while reading a saved article, a SAVED chip that removes it.

#include <string>

#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"

namespace pbui {

namespace fui = freeink::ui;

// Chess uses 1-4, the link layer owns the 200s, Hacker News the 300s; these
// stay in the 320s so a tap routed to the wrong app's handler is impossible.
enum : fui::ActionId {
  // A row tap. One action for both shelves, with actionValue carrying the row;
  // the Activity routes it by the view (Browse toggles a pick, Saved opens).
  ActionRow = 320,
  ActionPagePrev = 321,
  ActionPageNext = 322,
  // The two segments. Each names a destination absolutely, so the one you are
  // in is inert and neither can disagree with what is on screen.
  ActionShowBrowse = 323,
  ActionShowSaved = 324,
  // The band's SAVE chip in Browse: pull the picked bookmarks onto the device.
  ActionSaveBatch = 325,
  // The reader band's chip while reading a saved article: remove it.
  ActionUnsave = 326,
  // The empty-state controls. Two, because the first-run screen opens the
  // keyboard and every other empty screen asks for the network, and a screen
  // that has just said the radio is down must not offer to type a token.
  ActionEnterToken = 327,
  ActionLoadList = 328,
  // Every notice's way back to the list.
  ActionNoticeBack = 329,
};

// --- The list (Browse or Saved) ---------------------------------------------

struct ListModel {
  const char* title = "PINBOARD";
  // Which shelf is on screen. Same filled-means-here language as the segments.
  bool showingSaved = false;
  // Browse only: how many bookmarks are picked. When > 0 the band draws a SAVE
  // chip (ActionSaveBatch) and names the count in its right label.
  int pickedCount = 0;
  // Drawn instead of rows when the list is empty. Both the label and the action
  // must be set together or nothing is drawn, the same rule the reader's chip
  // and Hacker News's empty state follow.
  const char* emptyHeadline = nullptr;
  const char* emptyMessage = nullptr;
  const char* emptyActionLabel = nullptr;
  fui::ActionId emptyAction = fui::NO_ACTION;
  const fui::ListItem* items = nullptr;
  int count = 0;
  int selected = 0;
  int topIndex = 0;
};

void buildList(toybox::Screen& screen, const ListModel& model);

// The band the list draws into, and the row height, shared with the Activity so
// its paging maths and the drawn rows come from one function rather than two
// that are only ever wrong together. (The reasons are HackerNewsScreens'.)
fui::Rect listBand(const fui::DeviceContext& device);
int16_t listRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens);
int16_t listTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device, const fui::ThemeTokens& tokens);
fui::TextStyle listValueStyle(const fui::ThemeTokens& tokens);

// `text` cut to at most `lines` lines of `width`, breaking between words and
// ending in an ellipsis when anything was dropped. Shared with the Activity so
// a headline is fitted to the same space the component draws it into.
std::string fitLines(const fui::DrawTarget& target, const char* text, int16_t width, int lines,
                     const fui::TextStyle& style);

// --- The reader --------------------------------------------------------------

struct ReaderBody {
  const char* text = "";
  fui::TextStyle style{};
  toybox::WrappedText* wrap = nullptr;
};

struct ReaderModel {
  const char* title = "";
  uint32_t topLine = 0;
  const char* pageLabel = "";  // "3 / 12", built by the Activity
  bool canPagePrev = false;
  bool canPageNext = false;
  // The band's chip. A saved article is always on the device when the reader is
  // showing it, so the chip is filled and reads SAVED; tapping it removes it.
  bool saved = false;
  // A one-line reason the last action was refused, drawn as a transient toast
  // over the page rather than ejecting to a notice that loses the place.
  const char* notice = nullptr;
};

uint32_t buildReader(toybox::Screen& screen, const ReaderModel& model, ReaderBody& body);
uint32_t readerLineCount(const fui::DrawTarget& target, const fui::DeviceContext& device, ReaderBody& body);
fui::Rect readerBody(const fui::DeviceContext& device);

// --- Notices -----------------------------------------------------------------

struct NoticeModel {
  const char* headline = "";
  const char* message = "";
  const freeink::Icon* mark = nullptr;  // the unreadable mark, when set
  // The one control. Both must be set or nothing is drawn; leaving both unset
  // draws no button, which is what a busy notice wants.
  const char* actionLabel = nullptr;
  fui::ActionId action = fui::NO_ACTION;
};

void buildNotice(toybox::Screen& screen, const NoticeModel& model);

}  // namespace pbui
