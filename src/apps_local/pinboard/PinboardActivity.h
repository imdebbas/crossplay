#pragma once

// Pinboard "read later", read on the device.
//
// This is deliberately the Hacker News app with a different front door. The two
// read the same way -- a list of links, each opening into one flat paged
// document -- so the reader, the notice and the article pipeline are reused
// rather than reimplemented:
//
//   * the SCREENS come from hnui:: (HackerNewsScreens.h): buildList, buildReader
//     and buildNotice, with their models and action ids. Pinboard needs no new
//     pixels, only new rows and a new source.
//   * the ARTICLE TEXT comes from hn:: (HackerNewsCore.h): the r.jina.ai
//     extractor response is split, gated for prose and flattened to paragraphs
//     by the exact same code, so reading behaves identically.
//
// What is Pinboard's own lives in PinboardCore (the token + the two URLs) and
// here (the list fetch and parse). The token is read from a file on the card:
//
//     /.crosspoint/pinboard.cfg
//     username:APITOKEN            <- from pinboard.in/settings/password
//     tag=x4                       <- optional
//
// A file rather than an on-device keyboard or an OAuth pairing flow (the way
// Instapaper does it): Pinboard authenticates with a single token string, so a
// one-line file is the whole credential. An on-device entry screen is a natural
// follow-up; the seam for it is loadConfig().
//
// Like Hacker News, onEnter touches no radio. The list needs the network, so
// the first LOAD is what raises the Wi-Fi picker; nothing before it does.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../activities/Activity.h"
#include "../hackernews/HackerNewsCore.h"
#include "../hackernews/HackerNewsScreens.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxWrappedText.h"
#include "PinboardCore.h"

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
  enum class Phase : uint8_t { Busy, List, Reading, Notice };
  enum class Pending : uint8_t { None, List, Article };

  void loadConfig();

  void ensureConnected(Pending what, const char* busyMessage);
  void onWifiChosen(bool connected);
  void request(Pending what, const char* busyMessage);

  bool fetchList();
  bool fetchArticle();

  void showDocument(const char* title);
  void returnToList();
  void turnPage(int delta);
  void pageList(int delta);
  void showNotice(const char* headline, const char* message, bool unreadable);

  const pin::Bookmark* currentBookmark() const;

  pin::Config config_;
  bool configChecked_ = false;

  std::vector<pin::Bookmark> bookmarks_;
  int selected_ = 0;
  int topIndex_ = 0;
  int visibleRows_ = 0;

  // The flattened article the reader draws, and where in it we are.
  std::string document_;
  toybox::WrappedText wrap_;
  std::string readerTitle_;
  std::string readerUrl_;
  uint32_t topLine_ = 0;
  uint32_t lineCount_ = 0;
  uint16_t visibleLines_ = 0;
  static constexpr int kPageLabelCap = 2 * toybox::kULongChars + toybox::literalChars("/") + 1;
  char pageLabel_[kPageLabelCap] = "";

  std::string noticeHeadline_;
  std::string noticeMessage_;
  bool noticeUnreadable_ = false;

  // Owned here because fui::ListItem holds pointers, not copies.
  std::vector<std::string> rowLabels_;
  std::vector<std::string> rowValues_;
  std::vector<freeink::ui::ListItem> listItems_;
  bool rowsFitted_ = false;

  bool backPressSeen_ = false;
  bool listFailed_ = false;

  Phase phase_ = Phase::List;
  Pending pending_ = Pending::None;
  const char* busyMessage_ = "";
  Pending afterConnect_ = Pending::None;
  const char* afterConnectMessage_ = "";

  toybox::Interactions interactions_;
  bool interactionsReady_ = false;
};
