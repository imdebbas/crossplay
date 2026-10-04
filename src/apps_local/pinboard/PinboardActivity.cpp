#include "PinboardActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "../../DevMode.h"
#include "../../SilentRestart.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"

namespace {

// The token and optional tag live in one file on the card. See the class
// comment for the format.
constexpr const char* kConfigPath = "/.crosspoint/pinboard.cfg";

// The unread list JSON goes to the card first, so the TLS buffers are freed
// before ArduinoJson allocates -- the same order HackerNews and the font
// downloader use, and for the same reason.
constexpr const char* kListTmp = "/pinboard.tmp";

// The text extractor, reused from HackerNews. Same third party, same caveat:
// every article opened tells them what is being read, and the reader stops
// working the day they stop answering. It is only ever reached for an article
// body; the list itself is Pinboard's own API and would keep working without
// this line.
constexpr const char* kExtractorPrefix = "https://r.jina.ai/";

// An article is fetched into RAM and gated before anything is drawn, so it is
// bounded; the largest real article HackerNews measured was 32KB.
constexpr size_t kMaxArticleBytes = 96u * 1024u;

// The reader list is the 30 newest bookmarks plus everything still to-read.
// The recent half is fixed at 30; the total is bounded defensively so a reader
// with a huge to-read pile does not set the memory ceiling.
constexpr int kRecentCount = 30;
constexpr int kMaxBookmarks = 120;

// Temp files for the two list calls. Each goes to the card first so the TLS
// buffers are freed before ArduinoJson allocates.
constexpr const char* kRecentTmp = "/pinboard_recent.tmp";

}  // namespace

std::unique_ptr<Activity> PinboardActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PinboardActivity>(renderer, mappedInput);
}

// --- Lifecycle -----------------------------------------------------------

void PinboardActivity::loadConfig() {
  configChecked_ = true;
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
  if (Storage.exists(kConfigPath)) {
    pin::parseConfig(Storage.readFile(kConfigPath).c_str(), config_);
  }
#endif
}

void PinboardActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  // NOTHING TOUCHES THE RADIO HERE, exactly as HackerNews: the list needs the
  // network, and the first LOAD is what asks for it. See ensureConnected.
  loadConfig();
  if (!config_.valid()) {
    showNotice("ADD YOUR TOKEN",
               "Create /.crosspoint/pinboard.cfg on the SD card with one line: your Pinboard API token "
               "(username:TOKEN) from pinboard.in/settings/password. Add a second line \"tag=x4\" to pull only "
               "items with that tag. Then reopen this app.",
               false);
    requestUpdate();
    return;
  }
  phase_ = Phase::List;
  requestUpdate();
}

void PinboardActivity::onExit() {
  Activity::onExit();
  // The radio comes down before the activity does, the way the rest of the
  // firmware returns to a clean state after station mode. Not ours to put down
  // if Developer Mode brought it up.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Storage.remove(kListTmp);
  Storage.remove(kRecentTmp);
}

// --- Connecting on demand ------------------------------------------------

void PinboardActivity::ensureConnected(const Pending what, const char* busyMessage) {
  if (WiFi.status() == WL_CONNECTED) {
    request(what, busyMessage);
    return;
  }
  afterConnect_ = what;
  afterConnectMessage_ = busyMessage;
  // Phase is deliberately left alone: the picker swaps in on this same loop
  // pass, so whatever was on screen stays until it paints. A cancelled picker
  // then needs only a repaint. See HackerNewsActivity::ensureConnected for the
  // long version of why.
  backPressSeen_ = false;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void PinboardActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    afterConnect_ = Pending::None;
    requestUpdate();
    return;
  }
  const Pending what = afterConnect_;
  afterConnect_ = Pending::None;
  request(what, afterConnectMessage_);
}

void PinboardActivity::request(const Pending what, const char* busyMessage) {
  if (what == Pending::None) return;
  {
    RenderLock lock(*this);
    phase_ = Phase::Busy;
    busyMessage_ = busyMessage;
    pending_ = what;
  }
  requestUpdate();  // paint the busy screen; the fetch runs next loop pass
}

// --- Input ---------------------------------------------------------------

void PinboardActivity::loop() {
  namespace fui = freeink::ui;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen_ = true;

  // The deferred fetch, one pass after the screen that announced it.
  if (pending_ != Pending::None) {
    const Pending what = pending_;
    pending_ = Pending::None;
    bool ok = false;
    switch (what) {
      case Pending::List:
        ok = fetchList();
        break;
      case Pending::Article:
        ok = fetchArticle();
        break;
      case Pending::None:
        break;
    }
    if (!ok && phase_ == Phase::Busy) {
      if (what == Pending::List) {
        listFailed_ = true;
        phase_ = Phase::List;
      } else {
        showNotice("COULDN'T REACH IT", "That article did not come back. The connection may have dropped.", false);
      }
    }
    requestUpdate();
    return;
  }

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    // Back walks out one layer and never names where it lands; the shelf owns
    // the last step. From the list -- or from the token notice, which has no
    // list to fall back to -- that is leaving the app.
    if (phase_ == Phase::List || (phase_ == Phase::Notice && !config_.valid())) {
      shelf::leave(renderer, mappedInput);
    } else {
      returnToList();
    }
    return;
  }

  const MappedInputManager::SwipeDir swipe = mappedInput.wasSwipe();
  const bool swipeNext = swipe == MappedInputManager::SwipeDir::Up;
  const bool swipePrev = swipe == MappedInputManager::SwipeDir::Down;

  if (phase_ == Phase::Reading) {
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || swipeNext) {
      turnPage(1);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack) || swipePrev) {
      turnPage(-1);
      return;
    }
  }

  const bool next = mappedInput.wasReleased(MappedInputManager::Button::Down) || swipeNext;
  const bool prev = mappedInput.wasReleased(MappedInputManager::Button::Up) || swipePrev;
  if (phase_ == Phase::List && (next || prev)) {
    pageList(next ? 1 : -1);
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY) || !interactionsReady_) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions_.route(input);

  switch (event.action) {
    case hnui::ActionOpenStory:
      selected_ = event.value;
      ensureConnected(Pending::Article, "OPENING");
      break;
    case hnui::ActionPagePrev:
      turnPage(-1);
      break;
    case hnui::ActionPageNext:
      turnPage(1);
      break;
    case hnui::ActionLoadFrontPage:
      // The empty list's only control, and the one thing in the app that asks
      // for the network by itself. Guarded to the empty list, the same way
      // HackerNews guards its twin.
      if (phase_ != Phase::List || !bookmarks_.empty()) break;
      listFailed_ = false;
      ensureConnected(Pending::List, "LOADING YOUR LIST");
      break;
    case hnui::ActionNoticeBack:
      returnToList();
      break;
    default:
      break;
  }
}

// --- Fetching ------------------------------------------------------------

const pin::Bookmark* PinboardActivity::currentBookmark() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(bookmarks_.size())) return nullptr;
  return &bookmarks_[static_cast<size_t>(selected_)];
}

namespace {

// Download `url` to `tmp`, parse it, and append the unique bookmarks under it
// to `out` (de-duplicated by hash across both calls). `wrapped` is true for
// posts/recent, whose array lives under "posts"; posts/all is a bare array.
// Returns false only when the download itself failed, so a partial reader list
// (recent succeeded, to-read did not, or the other way round) still shows.
bool appendPosts(const std::string& url, const char* tmp, bool wrapped, std::set<std::string>& seen,
                 std::vector<pin::Bookmark>& out) {
  if (HttpDownloader::downloadToFile(url, tmp) != HttpDownloader::OK) {
    LOG_ERR("PIN", "list fetch failed (status %d)", HttpDownloader::lastStatus());
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("PIN", tmp, file)) {
    Storage.remove(tmp);
    return false;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, file);
  file.close();
  Storage.remove(tmp);
  if (err) {
    LOG_ERR("PIN", "list parse error: %s", err.c_str());
    return false;
  }

  const JsonArrayConst posts = wrapped ? doc["posts"].as<JsonArrayConst>() : doc.as<JsonArrayConst>();
  for (JsonObjectConst post : posts) {
    if (static_cast<int>(out.size()) >= kMaxBookmarks) break;
    const std::string hash = post["hash"] | "";
    const std::string href = post["href"] | "";
    if (href.empty() || hash.empty()) continue;
    if (!seen.insert(hash).second) continue;  // already have it from the other call
    pin::Bookmark bm;
    bm.url = href;
    bm.hash = hash;
    // Folded where somebody else's text becomes ours: the reading cut has no
    // glyph for a curly quote, so it would draw as nothing. The URL is a
    // request, not a sentence, and is never folded.
    bm.title = utf8FoldTypography(post["description"] | "");
    if (bm.title.empty()) bm.title = bm.url;
    bm.extended = post["extended"] | "";
    bm.tags = post["tags"] | "";
    bm.time = post["time"] | "";
    out.push_back(std::move(bm));
  }
  return true;
}

}  // namespace

bool PinboardActivity::fetchList() {
  // The reader list: the 30 newest bookmarks (posts/recent) PLUS everything
  // still to-read (posts/all?toread=yes), de-duplicated by hash. Either call
  // failing is survivable -- a partial list beats an error screen -- so the
  // failure that matters is both producing nothing.
  bookmarks_.clear();
  bookmarks_.reserve(kMaxBookmarks);
  std::set<std::string> seen;

  const bool recentOk = appendPosts(pin::recentUrl(config_, kRecentCount), kRecentTmp, true, seen, bookmarks_);
  const bool toreadOk = appendPosts(pin::allUrl(config_), kListTmp, false, seen, bookmarks_);
  if (!recentOk && !toreadOk) return false;

  // Newest first. ISO 8601 timestamps sort chronologically as plain strings.
  std::sort(bookmarks_.begin(), bookmarks_.end(),
            [](const pin::Bookmark& a, const pin::Bookmark& b) { return a.time > b.time; });

  LOG_INF("PIN", "reader list: %d bookmarks (recent %s, to-read %s)", static_cast<int>(bookmarks_.size()),
          recentOk ? "ok" : "failed", toreadOk ? "ok" : "failed");
  selected_ = 0;
  topIndex_ = 0;
  rowsFitted_ = false;  // rebuilt at paint, where a width exists
  phase_ = Phase::List;
  listFailed_ = bookmarks_.empty();
  return !bookmarks_.empty();
}

bool PinboardActivity::fetchArticle() {
  const pin::Bookmark* bm = currentBookmark();
  if (bm == nullptr) return false;
  readerUrl_ = bm->url;

  // The free half of the gate: a PDF or a browser-only page cannot become an
  // article however it is fetched, so it is not fetched.
  if (!hn::urlCanBeArticle(bm->url)) {
    showNotice("NOT READABLE HERE",
               "That link is a PDF, a video, or a page that only a browser can open. There is no article text to "
               "bring back.",
               true);
    return true;
  }

  std::string response;
  std::string url = kExtractorPrefix;
  url += bm->url;
  bool overLimit = false;
  const bool ok = HttpDownloader::fetchUrl(url, [&response, &overLimit](const uint8_t* data, const size_t len) {
    if (response.size() + len > kMaxArticleBytes) {
      overLimit = true;
      return false;
    }
    response.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok && !overLimit) {
    LOG_ERR("PIN", "article fetch failed");
    return false;
  }

  // The paid half of the gate, reused wholesale from HackerNews: a PDF extracts
  // to an empty body and a JS-only page to an error sentence, both at HTTP 200,
  // so only counting the prose separates them from a real post.
  const hn::Extracted extracted = hn::splitExtractorResponse(response);
  if (!hn::readsAsProse(extracted.body)) {
    showNotice("NOT READABLE HERE", "That page came back with no article in it. Whatever is there needs a browser.",
               true);
    return true;
  }

  document_ = extracted.title.empty() ? bm->title : extracted.title;
  document_ += "\n\n";
  for (const std::string& paragraph : hn::paragraphsFromMarkdown(extracted.body)) {
    document_ += paragraph;
    document_ += "\n\n";
  }
  showDocument(extracted.title.empty() ? bm->title.c_str() : extracted.title.c_str());
  return true;
}

// --- Reading -------------------------------------------------------------

void PinboardActivity::showDocument(const char* title) {
  RenderLock lock(*this);
  readerTitle_ = title != nullptr ? title : "";
  topLine_ = 0;
  phase_ = Phase::Reading;
  lineCount_ = 0;
  visibleLines_ = 0;
}

void PinboardActivity::returnToList() {
  phase_ = Phase::List;
  requestUpdate();
}

void PinboardActivity::turnPage(const int delta) {
  if (phase_ != Phase::Reading || visibleLines_ == 0) return;
  const uint32_t span = visibleLines_;
  const uint32_t maxTop = lineCount_ > span ? lineCount_ - span : 0;
  if (delta > 0) {
    topLine_ = topLine_ + span > maxTop ? maxTop : topLine_ + span;
  } else {
    topLine_ = topLine_ > span ? topLine_ - span : 0;
  }
  requestUpdate();
}

void PinboardActivity::pageList(const int delta) {
  const int count = static_cast<int>(listItems_.size());
  if (count <= 0 || visibleRows_ <= 0) return;
  const int pages = shelfui::pageCountFor(count, visibleRows_);
  if (pages <= 1) return;
  topIndex_ = shelfui::pageStep(shelfui::pageFor(topIndex_, visibleRows_), pages, delta) * visibleRows_;
  requestUpdate();
}

void PinboardActivity::showNotice(const char* headline, const char* message, const bool unreadable) {
  RenderLock lock(*this);
  noticeHeadline_ = headline;
  noticeMessage_ = message;
  noticeUnreadable_ = unreadable;
  phase_ = Phase::Notice;
}

// --- Drawing -------------------------------------------------------------

void PinboardActivity::render(RenderLock&&) {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  const toybox::Faces faces = phase_ == Phase::Reading ? toybox::readerFaces() : toybox::readingFaces();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer, faces);
  const fui::DeviceContext device = target.deviceContext();
  const fui::ThemeTokens& tokens = toybox::themeTokens();
  const fui::InputSnapshot noInput{};
  interactionsReady_ = false;
  toybox::Frame frame(target, device, noInput, interactions_);
  toybox::Screen screen(frame);

  const char* what = "Pinboard";

  switch (phase_) {
    case Phase::Busy: {
      hnui::NoticeModel model;
      model.headline = busyMessage_;
      hnui::buildNotice(screen, model);
      what = "Pinboard busy";
      break;
    }

    case Phase::List: {
      if (!rowsFitted_) {
        rowLabels_.clear();
        rowValues_.clear();
        listItems_.clear();
        rowLabels_.reserve(bookmarks_.size());
        rowValues_.reserve(bookmarks_.size());
        listItems_.reserve(bookmarks_.size());

        fui::TextStyle titleStyle = tokens.bodyText;
        titleStyle.maxLines = 2;
        const int16_t titleWidth = hnui::listTitleWidth(target, device, tokens);
        for (const pin::Bookmark& bm : bookmarks_) {
          rowLabels_.push_back(hnui::fitLines(target, bm.title.c_str(), titleWidth, 2, titleStyle));
          rowValues_.push_back(pin::domainOf(bm.url));
        }
        // Second pass: ListItem holds pointers, and the push_backs above may
        // have reallocated.
        for (size_t i = 0; i < rowLabels_.size(); ++i) {
          fui::ListItem row;
          row.label = rowLabels_[i].c_str();
          row.value = rowValues_[i].c_str();
          row.actionValue = static_cast<int16_t>(i);
          listItems_.push_back(row);
        }
        rowsFitted_ = true;
      }

      const int16_t rowHeight = hnui::listRowHeight(target, tokens);
      visibleRows_ = fui::listVisibleRows(hnui::listBand(device), rowHeight, tokens.listRowGap);
      if (visibleRows_ > 0) {
        const int pages = shelfui::pageCountFor(static_cast<int>(listItems_.size()), visibleRows_);
        const int maxTop = pages > 0 ? (pages - 1) * visibleRows_ : 0;
        if (topIndex_ > maxTop) topIndex_ = maxTop;
        if (topIndex_ < 0) topIndex_ = 0;
      }

      hnui::ListModel model;
      model.title = "READ LATER";
      model.items = listItems_.empty() ? nullptr : listItems_.data();
      model.count = static_cast<int>(listItems_.size());
      model.selected = selected_;
      model.topIndex = topIndex_;
      if (listItems_.empty()) {
        if (listFailed_) {
          model.emptyHeadline = "COULDN'T LOAD";
          model.emptyMessage = "Pinboard did not answer. Check Wi-Fi and your token, then try again.";
        } else {
          model.emptyHeadline = "NOTHING LOADED YET";
          model.emptyMessage = "Pull your Pinboard read-later list over Wi-Fi.";
        }
        model.emptyActionLabel = "LOAD";
        model.emptyAction = hnui::ActionLoadFrontPage;
      }
      hnui::buildList(screen, model);
      what = "Pinboard list";
      break;
    }

    case Phase::Reading: {
      const fui::Rect body = hnui::readerBody(device);
      const int16_t lineHeight = target.lineHeight(tokens.bodyText.font);
      visibleLines_ = fui::textAreaVisibleLines(body, lineHeight);
      hnui::ReaderBody bodyText;
      bodyText.text = document_.c_str();
      bodyText.style = tokens.bodyText;
      bodyText.wrap = &wrap_;
      lineCount_ = hnui::readerLineCount(target, device, bodyText);
      const uint32_t measured = lineCount_;

      const uint32_t pages = visibleLines_ > 0 ? (lineCount_ + visibleLines_ - 1) / visibleLines_ : 1;
      const uint32_t page = visibleLines_ > 0 ? topLine_ / visibleLines_ + 1 : 1;
      std::snprintf(pageLabel_, sizeof(pageLabel_), "%lu/%lu", static_cast<unsigned long>(page),
                    static_cast<unsigned long>(pages < 1 ? 1 : pages));

      hnui::ReaderModel model;
      model.title = readerTitle_.c_str();
      model.topLine = topLine_;
      model.pageLabel = pageLabel_;
      // Pinboard's reader is article-only: no comments to swap to, and the
      // local SAVE shelf is HackerNews's, not ours (a follow-up here).
      model.showingComments = false;
      model.swapAvailable = false;
      model.canPagePrev = topLine_ > 0;
      model.canPageNext = lineCount_ > topLine_ + visibleLines_;
      model.canSave = false;
      model.saved = false;
      lineCount_ = hnui::buildReader(screen, model, bodyText);
      if (lineCount_ != measured) requestUpdate();
      what = "Pinboard reader";
      break;
    }

    case Phase::Notice: {
      hnui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.mark = noticeUnreadable_ ? &icon_unreadable_32 : nullptr;
      // Every notice that is not a token prompt carries a way back to the list.
      // The token prompt deliberately carries none: there is nothing to tap
      // until the file exists, and Back leaves the app (see loop()).
      if (config_.valid()) {
        model.actionLabel = "BACK";
        model.action = hnui::ActionNoticeBack;
      }
      hnui::buildNotice(screen, model);
      what = "Pinboard notice";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);

  const auto labels = mappedInput.mapLabels("Back", phase_ == Phase::List ? "Open" : "", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
