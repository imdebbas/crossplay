#include "PinboardActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../SilentRestart.h"
#include "../../activities/ActivityResult.h"
#include "../../activities/network/WifiSelectionActivity.h"
#include "../../activities/util/KeyboardEntryActivity.h"
#include "../../components/UITheme.h"
#include "../../network/HttpDownloader.h"
#include "../Shelf.h"
#include "../ShelfScreen.h"
#include "../hackernews/HackerNewsCore.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxTheme.h"
#include "DevMode.h"

namespace {

// The bookmark list, straight from the user's account. The token rides in the
// query string that pin::listPath builds; the host is added here.
constexpr const char* kApiHost = "https://api.pinboard.in";

// The same text extractor Hacker News uses. It answers Markdown for an
// arbitrary page, which is the one job this device cannot do for itself. The
// cost is the same and worth being clear-eyed about: every article saved here
// tells this third party what is being read, and saving stops working the day
// they stop answering. The bookmark list does not depend on it.
constexpr const char* kExtractorPrefix = "https://r.jina.ai/";

// The list JSON goes to the card first so the TLS buffers are freed before
// ArduinoJson allocates, the same order Hacker News and the font downloader use.
constexpr const char* kListTmp = "/pinboard_list.tmp";

// One article into RAM, bounded so a runaway page cannot set the memory ceiling.
constexpr size_t kMaxArticleBytes = 96u * 1024u;

// The token field. A Pinboard token is "user:HEX" and well under this.
constexpr size_t kTokenMaxLen = 128;

}  // namespace

std::unique_ptr<Activity> PinboardActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<PinboardActivity>(renderer, mappedInput);
}

// --- Lifecycle ---------------------------------------------------------------

void PinboardActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  // Nothing touches the radio here, the same feature Hacker News has: the Saved
  // shelf exists precisely for having no network, so the app opens offline and
  // the first thing that genuinely needs the radio is what asks for it.
  library_.load();
  token_ = pin::parseAuthToken(library_.loadToken());
  view_ = pin::ListView::Browse;
  phase_ = Phase::List;
  requestUpdate();
}

void PinboardActivity::onExit() {
  Activity::onExit();
  // The radio comes down before the activity does, unless Developer Mode owns
  // it -- the same handling Hacker News documents.
  if (WiFi.getMode() != WIFI_MODE_NULL && !devmode::holdsRadio()) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
  Storage.remove(kListTmp);
}

void PinboardActivity::ensureConnected(const Pending what, const char* busyMessage) {
  if (WiFi.status() == WL_CONNECTED) {
    request(what, busyMessage);
    return;
  }
  afterConnect_ = what;
  afterConnectMessage_ = busyMessage;
  // phase_ is left alone on purpose; see HackerNewsActivity::ensureConnected for
  // why a Connecting screen can never be drawn and why not touching it makes a
  // cancelled picker free. The Back press belongs to the screen about to leave
  // the top, so it is dropped here rather than paired with a later release.
  backPressSeen_ = false;
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiChosen(!result.isCancelled); });
}

void PinboardActivity::onWifiChosen(const bool connected) {
  if (!connected) {
    // Declining is not wanting out of the app. phase_ still holds whatever was
    // on screen, because raising the picker never changed it.
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
  requestUpdate();
}

// --- Input -------------------------------------------------------------------

void PinboardActivity::loop() {
  namespace fui = freeink::ui;

  // A Back RELEASE only means "go back" if this activity also saw the PRESS;
  // the Wi-Fi picker cancels on the press. Recorded before every early return.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backPressSeen_ = true;

  // The deferred work, one pass after the screen announcing it. Taken first so
  // it cannot be starved by input.
  if (pending_ != Pending::None) {
    const Pending what = pending_;
    pending_ = Pending::None;
    bool ok = false;
    switch (what) {
      case Pending::LoadList:
        ok = fetchList();
        break;
      case Pending::SaveStep:
        ok = saveStep();
        break;
      case Pending::None:
        break;
    }
    if (!ok && phase_ == Phase::Busy) {
      if (what == Pending::LoadList) {
        // Back to Browse, where both segments are, drawn as the list's own empty
        // state. tokenRejected_ was set by fetchList when Pinboard refused the
        // token, and it drives a different empty state from a dropped radio.
        if (!tokenRejected_) fetchFailed_ = true;
        view_ = pin::ListView::Browse;
        phase_ = Phase::List;
      } else {
        // A save batch that hit a dropped connection mid-run. Keep what did
        // arrive and say how far it got, on the Saved shelf where it landed.
        library_.load();
        view_ = pin::ListView::Saved;
        rows_.invalidate();
        char msg[96];
        std::snprintf(msg, sizeof(msg), "Saved %d of %d before the connection dropped.", saved_,
                      static_cast<int>(saveQueue_.size()));
        showNotice("CONNECTION LOST", msg, false, "OK", pbui::ActionNoticeBack);
      }
    }
    requestUpdate();
    return;
  }

  if (backPressSeen_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    backPressSeen_ = false;
    // Back walks out one layer at a time; the shelf owns the last step.
    if (phase_ == Phase::List) {
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

  // The two side keys page the list, the device's only physical buttons.
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
    case pbui::ActionRow:
      if (view_ == pin::ListView::Saved) {
        openSavedArticle(event.value);
      } else {
        togglePick(event.value);
      }
      break;
    case pbui::ActionPagePrev:
      turnPage(-1);
      break;
    case pbui::ActionPageNext:
      turnPage(1);
      break;
    case pbui::ActionShowBrowse:
      view_ = pin::ListView::Browse;
      topIndex_ = 0;
      requestUpdate();
      break;
    case pbui::ActionShowSaved:
      view_ = pin::ListView::Saved;
      topIndex_ = 0;
      requestUpdate();
      break;
    case pbui::ActionSaveBatch:
      startSaveBatch();
      break;
    case pbui::ActionUnsave:
      removeCurrent();
      break;
    case pbui::ActionEnterToken:
      enterToken();
      break;
    case pbui::ActionLoadList:
      fetched_ = false;
      fetchFailed_ = false;
      tokenRejected_ = false;
      ensureConnected(Pending::LoadList, "LOADING YOUR BOOKMARKS");
      break;
    case pbui::ActionNoticeBack:
      returnToList();
      break;
    default:
      break;
  }
}

// --- The token ---------------------------------------------------------------

void PinboardActivity::enterToken() {
  // Prefilled with the current token, so re-entry is an edit (fix a wrong one)
  // and a retry after a blip is just confirming. InputType::Text, not Password:
  // the token is not a secret worth hiding from the person holding the device,
  // and seeing it is what lets them spot a typo.
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Pinboard API token", pin::authParam(token_),
                                              kTokenMaxLen, InputType::Text),
      [this](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        onTokenEntered(std::get<KeyboardResult>(result.data).text);
      });
}

void PinboardActivity::onTokenEntered(const std::string& text) {
  const pin::AuthToken parsed = pin::parseAuthToken(text);
  if (!parsed.valid) {
    showNotice("THAT TOKEN LOOKS WRONG",
               "A Pinboard token looks like username:HEXSTRING. Find yours on pinboard.in under Settings, Password.",
               false, "TRY AGAIN", pbui::ActionEnterToken);
    return;
  }
  token_ = parsed;
  tokenRejected_ = false;
  fetched_ = false;
  library_.saveToken(pin::authParam(token_));
  ensureConnected(Pending::LoadList, "LOADING YOUR BOOKMARKS");
}

// --- Fetching the list -------------------------------------------------------

bool PinboardActivity::fetchList() {
  tokenRejected_ = false;
  if (!token_.valid) return false;

  const std::string url = std::string(kApiHost) + pin::listPath(token_);
  const HttpDownloader::DownloadError err = HttpDownloader::downloadToFile(url, kListTmp);
  if (err == HttpDownloader::UNAUTHORIZED) {
    // Pinboard refused the token itself. A different empty state from a dropped
    // radio: there is no point retrying the same token, so the screen asks for
    // a new one rather than offering TRY AGAIN.
    LOG_ERR("PIN", "list fetch unauthorized: the token was refused");
    tokenRejected_ = true;
    Storage.remove(kListTmp);
    return false;
  }
  if (err != HttpDownloader::OK) {
    LOG_ERR("PIN", "list fetch failed (%d)", static_cast<int>(err));
    Storage.remove(kListTmp);
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("PIN", kListTmp, file)) {
    LOG_ERR("PIN", "could not reopen the list");
    Storage.remove(kListTmp);
    return false;
  }
  // The HTTP client is closed by now, so the TLS buffers are back.
  JsonDocument doc;
  const DeserializationError perr = deserializeJson(doc, file);
  file.close();
  Storage.remove(kListTmp);
  if (perr) {
    LOG_ERR("PIN", "list parse error: %s", perr.c_str());
    return false;
  }

  // posts/all answers a JSON array. Keep only the to-read set -- the read-later
  // queue -- since posts/all has no server-side filter for it.
  bookmarks_.clear();
  const JsonArrayConst all = doc.as<JsonArrayConst>();
  for (JsonObjectConst post : all) {
    const char* href = post["href"] | "";
    if (href[0] == '\0') continue;
    const char* toread = post["toread"] | "no";
    if (std::strcmp(toread, "yes") != 0) continue;
    pin::Bookmark bookmark;
    bookmark.url = href;
    // Stored raw; displayTitle folds it for the reading cut at row-build time,
    // and the URL is never folded because it is the key that is fetched.
    bookmark.title = post["description"] | "";
    bookmark.tags = post["tags"] | "";
    bookmark.toRead = true;
    bookmarks_.push_back(std::move(bookmark));
  }
  // picked_ grows and shrinks with bookmarks_ so the two never disagree.
  picked_.assign(bookmarks_.size(), false);
  fetched_ = true;
  fetchFailed_ = false;
  view_ = pin::ListView::Browse;
  rows_.invalidate();
  selected_ = 0;
  topIndex_ = 0;
  phase_ = Phase::List;
  LOG_INF("PIN", "list: %d to-read bookmarks", static_cast<int>(bookmarks_.size()));
  // An empty list is a successful fetch, not a failure: the empty state says
  // "nothing flagged to read" rather than "could not reach Pinboard".
  return true;
}

// --- Saving a batch ----------------------------------------------------------

int PinboardActivity::pickedCount() const {
  int n = 0;
  for (const bool p : picked_) {
    if (p) ++n;
  }
  return n;
}

void PinboardActivity::startSaveBatch() {
  saveQueue_.clear();
  for (int i = 0; i < static_cast<int>(bookmarks_.size()); ++i) {
    if (i < static_cast<int>(picked_.size()) && picked_[i]) saveQueue_.push_back(i);
  }
  if (saveQueue_.empty()) return;  // SAVE is not drawn with nothing picked
  saveIndex_ = 0;
  saved_ = 0;
  unreadable_ = 0;
  std::snprintf(busyDetail_, sizeof(busyDetail_), "Saving 1 of %d...", static_cast<int>(saveQueue_.size()));
  ensureConnected(Pending::SaveStep, busyDetail_);
}

bool PinboardActivity::saveStep() {
  if (saveIndex_ >= saveQueue_.size()) {
    finishSaveBatch();
    return true;
  }
  const int idx = saveQueue_[saveIndex_];
  if (idx < 0 || idx >= static_cast<int>(bookmarks_.size())) {
    ++saveIndex_;
    request(Pending::SaveStep, busyDetail_);
    return true;
  }
  const pin::Bookmark& bookmark = bookmarks_[static_cast<size_t>(idx)];
  const std::string title = bookmark.title.empty() ? pin::displayTitle(bookmark) : utf8FoldTypography(bookmark.title);

  // The free half of the readability gate: a PDF or a JS-only page cannot become
  // an article however it is fetched, so it is not fetched. Counted as skipped
  // and the batch moves on -- one unreadable bookmark is not a reason to stop.
  if (!hn::urlCanBeArticle(bookmark.url)) {
    LOG_INF("PIN", "skip (not an article): %s", bookmark.url.c_str());
    ++unreadable_;
  } else {
    std::string response;
    std::string url = kExtractorPrefix;
    url += bookmark.url;
    bool overLimit = false;
    const bool ok = HttpDownloader::fetchUrl(url, [&response, &overLimit](const uint8_t* data, const size_t len) {
      if (response.size() + len > kMaxArticleBytes) {
        overLimit = true;
        return false;
      }
      response.append(reinterpret_cast<const char*>(data), len);
      return true;
    });
    // A transport failure (not merely a page that overran the cap) aborts the
    // whole batch: the connection is gone and the rest would fail the same way.
    if (!ok && !overLimit) {
      LOG_ERR("PIN", "article fetch failed: %s", bookmark.url.c_str());
      return false;
    }

    const hn::Extracted extracted = hn::splitExtractorResponse(response);
    if (!hn::readsAsProse(extracted.body)) {
      LOG_INF("PIN", "gate rejected %s (%d prose chars)", bookmark.url.c_str(), hn::proseChars(extracted.body));
      ++unreadable_;
    } else {
      std::string document = extracted.title.empty() ? title : extracted.title;
      document += "\n\n";
      for (const std::string& paragraph : hn::paragraphsFromMarkdown(extracted.body)) {
        document += paragraph;
        document += "\n\n";
      }
      const std::string saveTitle = extracted.title.empty() ? title : extracted.title;
      if (library_.save(bookmark.url, saveTitle, document)) {
        ++saved_;
      } else {
        // A full or unwritable card. Counted with the unreadable ones: the
        // verdict's honest number is how many actually landed.
        LOG_ERR("PIN", "could not save %s", bookmark.url.c_str());
        ++unreadable_;
      }
    }
  }

  ++saveIndex_;
  if (saveIndex_ < saveQueue_.size()) {
    std::snprintf(busyDetail_, sizeof(busyDetail_), "Saving %d of %d...", static_cast<int>(saveIndex_ + 1),
                  static_cast<int>(saveQueue_.size()));
    request(Pending::SaveStep, busyDetail_);
  } else {
    finishSaveBatch();
  }
  return true;
}

void PinboardActivity::finishSaveBatch() {
  // The picks are spent. Clearing them means the Browse list comes back without
  // the boxes ticked, so a second SAVE does not re-fetch what just landed.
  std::fill(picked_.begin(), picked_.end(), false);
  const int attempted = static_cast<int>(saveQueue_.size());
  saveQueue_.clear();
  saveIndex_ = 0;

  library_.load();
  view_ = pin::ListView::Saved;
  rows_.invalidate();

  char msg[160];
  if (unreadable_ == 0) {
    std::snprintf(msg, sizeof(msg), "Saved %d to your device.", saved_);
  } else {
    std::snprintf(msg, sizeof(msg), "Saved %d of %d. %d had no readable article and were skipped.", saved_, attempted,
                  unreadable_);
  }
  showNotice("SAVED", msg, false, "READ THEM", pbui::ActionNoticeBack);
}

// --- Browse selection --------------------------------------------------------

void PinboardActivity::togglePick(const int index) {
  if (index < 0 || index >= static_cast<int>(picked_.size())) return;
  picked_[static_cast<size_t>(index)] = !picked_[static_cast<size_t>(index)];
  // The marker changed while the view did not, which rowsStale cannot see.
  rows_.invalidate();
  requestUpdate();
}

// --- Reading -----------------------------------------------------------------

void PinboardActivity::openSavedArticle(const int index) {
  const auto& saved = library_.articles();
  if (index < 0 || index >= static_cast<int>(saved.size())) return;
  const pin::SavedArticle& article = saved[static_cast<size_t>(index)];
  if (!library_.readArticle(article, document_)) {
    showNotice("NOT THERE", "That article is in the list but its text is missing from the card.", false, "OK",
               pbui::ActionNoticeBack);
    requestUpdate();
    return;
  }
  // Folded on the read, so an article saved before the fold existed loses its
  // glyph holes without a migration (the reading cut has no glyph for curly
  // punctuation). One pass over text about to be word-wrapped anyway.
  document_ = utf8FoldTypography(document_);
  readerUrl_ = article.url;
  showDocument(article.title.c_str());
  requestUpdate();
}

void PinboardActivity::showDocument(const char* title) {
  RenderLock lock(*this);
  readerTitle_ = title != nullptr ? title : "";
  topLine_ = 0;
  phase_ = Phase::Reading;
  actionFailedNotice_ = false;
  // Line counts need a draw target, filled by the first paint; until then the
  // label reads as one page, which is what an unmeasured document looks like.
  lineCount_ = 0;
  visibleLines_ = 0;
}

void PinboardActivity::removeCurrent() {
  if (readerUrl_.empty()) return;
  actionFailedNotice_ = false;
  if (!library_.remove(readerUrl_)) {
    actionFailedNotice_ = true;
    requestUpdate();
    return;
  }
  rows_.invalidate();
  // What you were reading is gone from the shelf, so step back to it rather
  // than show an article that is no longer there.
  view_ = pin::ListView::Saved;
  phase_ = Phase::List;
  requestUpdate();
}

void PinboardActivity::turnPage(const int delta) {
  if (phase_ != Phase::Reading || visibleLines_ == 0) return;
  actionFailedNotice_ = false;
  const uint32_t span = visibleLines_;
  const uint32_t maxTop = lineCount_ > span ? lineCount_ - span : 0;
  if (delta > 0) {
    topLine_ = topLine_ + span > maxTop ? maxTop : topLine_ + span;
  } else {
    topLine_ = topLine_ > span ? topLine_ - span : 0;
  }
  requestUpdate();
}

void PinboardActivity::returnToList() {
  phase_ = Phase::List;
  actionFailedNotice_ = false;
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

void PinboardActivity::showNotice(const char* headline, std::string message, const bool unreadable,
                                  const char* actionLabel, const freeink::ui::ActionId action) {
  RenderLock lock(*this);
  noticeHeadline_ = headline;
  noticeMessage_ = std::move(message);
  noticeUnreadable_ = unreadable;
  noticeActionLabel_ = actionLabel != nullptr ? actionLabel : "";
  noticeAction_ = action;
  phase_ = Phase::Notice;
}

// --- Drawing -----------------------------------------------------------------

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
      pbui::NoticeModel model;
      model.headline = busyMessage_;
      pbui::buildNotice(screen, model);
      what = "PIN busy";
      break;
    }

    case Phase::List: {
      // The one place the rows and the view are reconciled; every path that
      // changes the shelf only sets view_, and this notices (see PinboardRows).
      if (pin::rowsStale(rows_, view_)) {
        pin::buildRows(rows_, view_, bookmarks_, picked_, library_.articles());
      }
      if (!rows_.fitted) {
        rows_.labels.clear();
        rows_.labels.reserve(rows_.size());
        listItems_.clear();
        listItems_.reserve(rows_.size());
        fui::TextStyle titleStyle = tokens.bodyText;
        titleStyle.maxLines = 2;
        const int16_t titleWidth = pbui::listTitleWidth(target, device, tokens);
        for (const std::string& title : rows_.titles) {
          rows_.labels.push_back(pbui::fitLines(target, title.c_str(), titleWidth, 2, titleStyle));
        }
        for (size_t i = 0; i < rows_.labels.size(); ++i) {
          fui::ListItem row;
          row.label = rows_.labels[i].c_str();
          row.value = rows_.values[i].c_str();
          row.actionValue = static_cast<int16_t>(i);
          listItems_.push_back(row);
        }
        rows_.fitted = true;
      }

      const int16_t rowHeight = pbui::listRowHeight(target, tokens);
      visibleRows_ = fui::listVisibleRows(pbui::listBand(device), rowHeight, tokens.listRowGap);
      if (visibleRows_ > 0) {
        const int pages = shelfui::pageCountFor(static_cast<int>(listItems_.size()), visibleRows_);
        const int maxTop = pages > 0 ? (pages - 1) * visibleRows_ : 0;
        if (topIndex_ > maxTop) topIndex_ = maxTop;
        if (topIndex_ < 0) topIndex_ = 0;
      }

      const bool saved = view_ == pin::ListView::Saved;
      pbui::ListModel model;
      model.items = listItems_.empty() ? nullptr : listItems_.data();
      model.count = static_cast<int>(listItems_.size());
      model.selected = -1;  // no row cursor; taps and the side keys do the work
      model.topIndex = topIndex_;
      model.showingSaved = saved;
      model.pickedCount = saved ? 0 : pickedCount();
      if (saved) model.title = "PINBOARD";

      if (listItems_.empty()) {
        if (!saved && tokenRejected_) {
          // Distinct from a dropped radio: the token itself was refused, so the
          // screen asks for a new one rather than offering TRY AGAIN.
          model.emptyHeadline = "CHECK YOUR TOKEN";
          model.emptyMessage = "Pinboard did not accept that token. Re-enter it.";
          model.emptyActionLabel = "RE-ENTER TOKEN";
          model.emptyAction = pbui::ActionEnterToken;
        } else {
          const pin::EmptyState empty = pin::emptyState(view_, token_.valid, fetched_, fetchFailed_);
          model.emptyHeadline = empty.headline;
          model.emptyMessage = empty.message;
          model.emptyActionLabel = empty.actionLabel;
          if (empty.action == pin::EmptyAction::EnterToken) {
            model.emptyAction = pbui::ActionEnterToken;
          } else if (empty.action == pin::EmptyAction::LoadList) {
            model.emptyAction = pbui::ActionLoadList;
          }
        }
      }
      pbui::buildList(screen, model);
      what = saved ? "PIN saved" : "PIN browse";
      break;
    }

    case Phase::Reading: {
      const fui::Rect body = pbui::readerBody(device);
      const int16_t lineHeight = target.lineHeight(tokens.bodyText.font);
      visibleLines_ = fui::textAreaVisibleLines(body, lineHeight);
      pbui::ReaderBody bodyText;
      bodyText.text = document_.c_str();
      bodyText.style = tokens.bodyText;
      bodyText.wrap = &wrap_;
      lineCount_ = pbui::readerLineCount(target, device, bodyText);
      const uint32_t measured = lineCount_;

      const uint32_t pages = visibleLines_ > 0 ? (lineCount_ + visibleLines_ - 1) / visibleLines_ : 1;
      const uint32_t page = visibleLines_ > 0 ? topLine_ / visibleLines_ + 1 : 1;
      std::snprintf(pageLabel_, sizeof(pageLabel_), "%lu/%lu", static_cast<unsigned long>(page),
                    static_cast<unsigned long>(pages < 1 ? 1 : pages));

      pbui::ReaderModel model;
      model.title = readerTitle_.c_str();
      model.topLine = topLine_;
      model.pageLabel = pageLabel_;
      model.canPagePrev = topLine_ > 0;
      model.canPageNext = lineCount_ > topLine_ + visibleLines_;
      model.saved = true;  // the reader only ever shows a saved article here
      model.notice = actionFailedNotice_ ? "Could not remove it." : nullptr;
      lineCount_ = pbui::buildReader(screen, model, bodyText);
      if (lineCount_ != measured) requestUpdate();
      what = "PIN reader";
      break;
    }

    case Phase::Notice: {
      pbui::NoticeModel model;
      model.headline = noticeHeadline_.c_str();
      model.message = noticeMessage_.c_str();
      model.mark = noticeUnreadable_ ? &icon_unreadable_32 : nullptr;
      if (!noticeActionLabel_.empty() && noticeAction_ != fui::NO_ACTION) {
        model.actionLabel = noticeActionLabel_.c_str();
        model.action = noticeAction_;
      }
      pbui::buildNotice(screen, model);
      what = "PIN notice";
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, what);

  const auto labels = mappedInput.mapLabels("Back", phase_ == Phase::List ? "Open" : "", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
