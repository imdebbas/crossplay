#pragma once

// Pinboard, read on the device.
//
// ---------------------------------------------------------------------------
// The shape of it, modelled on Hacker News because the bet is the same: the
// device talks straight to a public API over TLS and needs no server of ours.
//
// 1. The LIST is one GET to api.pinboard.in/v1/posts/all, carrying the user's
//    own API token. The ARTICLE text is the same r.jina.ai extractor Hacker
//    News uses, so the readability gate and Markdown flattener are shared
//    (hn::, HackerNewsCore.h) rather than twinned.
//
// 2. Nothing slow happens on the render path. A fetch or a save STEP is
//    requested by setting pending_ and asking for a repaint; the loop performs
//    it on the following pass, once the busy screen is already up. A batch SAVE
//    is one article per pass, never a chain, so Back always answers and the
//    caption can name the article arriving -- the discipline Instapaper keeps.
//
// 3. Browse lets you pick several bookmarks and SAVE pulls them onto the device;
//    you then read them from the Saved shelf, offline. That inverts Hacker
//    News's open-one-then-save, and it is the only structural difference.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"
#include "PinboardCore.h"
#include "PinboardLibrary.h"
#include "PinboardRows.h"
#include "PinboardScreens.h"

class PinboardActivity final : public Activity {
 public:
  PinboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Pinboard", renderer, mappedInput) {}
  ~PinboardActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  // Raising the Wi-Fi picker leaves phase_ alone, so there is no Connecting
  // phase; see ensureConnected, after HackerNewsActivity.
  enum class Phase : uint8_t {
    Busy,     // a fetch or a save step is about to happen or is happening
    List,     // Browse or Saved
    Reading,  // one saved article
    Notice,   // a verdict or an error
  };

  // What the next loop pass should do. Set alongside a repaint request so the
  // work starts after the screen showing it is drawn.
  enum class Pending : uint8_t { None, LoadList, SaveStep };

  void onWifiChosen(bool connected);
  void ensureConnected(Pending what, const char* busyMessage);
  void request(Pending what, const char* busyMessage);

  bool fetchList();
  // One picked bookmark per pass: fetch, gate, save. Returns false only on a
  // transport failure, which aborts the batch; an unreadable page is counted
  // and the batch goes on.
  bool saveStep();
  void startSaveBatch();
  void finishSaveBatch();

  void enterToken();
  void onTokenEntered(const std::string& text);

  void togglePick(int index);
  void openSavedArticle(int index);
  void showDocument(const char* title);
  void removeCurrent();
  void turnPage(int delta);
  void returnToList();
  void pageList(int delta);
  void showNotice(const char* headline, std::string message, bool unreadable, const char* actionLabel,
                  freeink::ui::ActionId action);

  int pickedCount() const;

  pin::Library library_;
  pin::AuthToken token_;
  // Pinboard rejected the stored token (a 401/403), as opposed to a dropped
  // connection. Drives the "re-enter your token" empty state rather than "try
  // again".
  bool tokenRejected_ = false;

  // The fetched bookmarks (already filtered to the to-read set) and, in
  // lockstep, whether each is picked to save. picked_ is resized with
  // bookmarks_ so the two never disagree on length.
  std::vector<pin::Bookmark> bookmarks_;
  std::vector<bool> picked_;
  bool fetched_ = false;
  bool fetchFailed_ = false;

  pin::ListView view_ = pin::ListView::Browse;

  // A batch save in flight: the picked bookmark indices, where we are, and the
  // tally the verdict reads out.
  std::vector<int> saveQueue_;
  size_t saveIndex_ = 0;
  int saved_ = 0;
  int unreadable_ = 0;
  char busyDetail_[48] = "";

  // Reading. A Pinboard article is always read from the card, so the reader is
  // always "saved"; the band's chip removes it.
  std::string document_;
  toybox::WrappedText wrap_;
  std::string readerTitle_;
  std::string readerUrl_;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  static constexpr int kPageLabelCap = 2 * toybox::kULongChars + toybox::literalChars("/") + 1;
  char pageLabel_[kPageLabelCap] = "";
  // A remove the card refused, drawn as a transient toast over the reader
  // rather than ejecting to a notice; cleared by the reader's next input.
  bool actionFailedNotice_ = false;

  std::string noticeHeadline_;
  std::string noticeMessage_;
  bool noticeUnreadable_ = false;
  std::string noticeActionLabel_;
  freeink::ui::ActionId noticeAction_ = freeink::ui::NO_ACTION;

  // The strings the list draws, owned here because fui::ListItem holds pointers.
  pin::Rows rows_;
  std::vector<freeink::ui::ListItem> listItems_;

  int selected_ = 0;
  int topIndex_ = 0;
  int visibleRows_ = 0;

  bool backPressSeen_ = false;

  Phase phase_ = Phase::List;
  Pending pending_ = Pending::None;
  const char* busyMessage_ = "";
  Pending afterConnect_ = Pending::None;
  const char* afterConnectMessage_ = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
