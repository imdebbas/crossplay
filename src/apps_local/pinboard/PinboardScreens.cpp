#include "PinboardScreens.h"

#include <FreeInkUIIcon.h>

#include <cstdio>

#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxText.h"
#include "PinboardRows.h"  // pin::kPickedMark, for the value-column width

namespace pbui {
namespace {

constexpr int kBodyTop = toybox::kBodyTop;
constexpr int kFooterHeight = toybox::kPillHeight;

// The band's optional trailing chip. nullptr label draws none. Generalised from
// Hacker News's save chip so the Browse SAVE and the reader's remove chip are
// one drawing rather than two. The icon is the shared saved mark -- a 1-bpp
// mask, so it never fills; the fill and the word are what change.
struct Chip {
  const char* label = nullptr;
  fui::ActionId action = fui::NO_ACTION;
  bool filled = false;
};

void chrome(toybox::Screen& screen, const char* title, const char* rightLabel, const fui::TextStyle* titleText = nullptr,
            const Chip& chip = {}) {
  fui::HeaderProps header;
  header.title = title;
  if (chip.label != nullptr) {
    header.trailingIcon = fui::bitmapFromIcon(icon_saved_32);
    header.trailingLabel = chip.label;
    header.trailingAction = chip.action;
    header.trailingStyles = chip.filled ? toybox::bandFilledStyles() : toybox::bandOutlineStyles();
    header.trailingRadius = toybox::kPillRadius / 2;
  }
  header.rightLabel = rightLabel;
  header.borderEdges = fui::EdgesNone;
  if (titleText != nullptr) header.titleText = *titleText;
  if (rightLabel != nullptr) {
    // Drawn in paper, not ink: the band is solid black, and a label left at the
    // default subtitle colour is black on black. Same trap HackerNewsScreens
    // documents at length.
    header.subtitleText = screen.theme().smallText;
    header.subtitleText.color = fui::Color::White;
    header.subtitleText.align = fui::TextAlign::Right;
  }
  toybox::absoluteChrome(screen);
  toybox::headerBand(screen, header);

  screen.insetContent(fui::Insets{toybox::kGutter * 3, toybox::kMargin, toybox::kMargin, toybox::kMargin});
}

}  // namespace

std::string fitLines(const fui::DrawTarget& target, const char* text, const int16_t width, const int lines,
                     const fui::TextStyle& style) {
  return toybox::fitLines(target, text, width, lines, style);
}

// --- The list ----------------------------------------------------------------

fui::Rect listBand(const fui::DeviceContext& device) {
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

int16_t listRowHeight(const fui::DrawTarget& target, const fui::ThemeTokens& tokens) {
  // Two lines of title plus air. A bookmark's title (or its URL) is the content,
  // and the pick marker rides along as a footnote beside it.
  return static_cast<int16_t>(2 * target.lineHeight(tokens.bodyText.font) + toybox::kGutter);
}

fui::TextStyle listValueStyle(const fui::ThemeTokens& tokens) {
  fui::TextStyle value = tokens.smallText;
  value.font = toybox::kTileFont;
  // Naming the alignment the component applies anyway marks this style as owned,
  // so Screen::list() does not put the theme's value style back (font 0 reads as
  // "unset"); the same subtlety HackerNewsScreens::listCountStyle documents.
  value.align = fui::TextAlign::Right;
  return value;
}

int16_t listTitleWidth(const fui::DrawTarget& target, const fui::DeviceContext& device,
                       const fui::ThemeTokens& tokens) {
  const fui::TextStyle value = listValueStyle(tokens);
  // The widest pick marker, so a title is measured against the space actually
  // left beside it.
  const int16_t valueWidth = target.measureText(value.font, pin::kPickedMark, value).width;
  return static_cast<int16_t>(listBand(device).width - 2 * tokens.listSidePadding - valueWidth - toybox::kGutter);
}

void buildList(toybox::Screen& screen, const ListModel& model) {
  // The SAVE chip only on Browse, only when something is picked. Its count rides
  // in the band's right label.
  char countLabel[16] = "";
  Chip chip;
  const bool showSave = !model.showingSaved && model.pickedCount > 0;
  if (showSave) {
    std::snprintf(countLabel, sizeof(countLabel), "%d picked", model.pickedCount);
    chip.label = "SAVE";
    chip.action = ActionSaveBatch;
    chip.filled = false;
  }
  chrome(screen, model.title, showSave ? countLabel : nullptr, nullptr, chip);

  // The two shelves as a pair of segments: each names where it goes, so the one
  // you are in is inert. takeBottom removes the strip from the content flow so
  // the list below cannot draw rows into it.
  {
    const fui::Rect strip = screen.takeBottom(kFooterHeight, toybox::kGutter);
    const int16_t y = strip.y;
    const int16_t half = static_cast<int16_t>((strip.width - toybox::kGutter) / 2);
    const auto segment = [&screen, y, half](const char* label, const fui::ActionId action, const int16_t x,
                                            const bool here) {
      fui::ButtonProps button;
      button.label = label;
      button.action = here ? fui::NO_ACTION : action;
      button.styles = here ? toybox::invertedStyles() : toybox::rowStyles();
      screen.button(button, fui::makeRect(x, y, half, kFooterHeight));
    };
    segment("BROWSE", ActionShowBrowse, strip.x, !model.showingSaved);
    segment("SAVED", ActionShowSaved, static_cast<int16_t>(strip.x + half + toybox::kGutter), model.showingSaved);
  }

  if (model.count <= 0) {
    if (model.emptyHeadline != nullptr) {
      // Off the band, so ink. Laid out as a block and centred as one, the way
      // buildNotice stacks its own -- centeredText consumes nothing, so two
      // calls would draw at the same y. See HackerNewsScreens for the full trap.
      fui::TextStyle headline = screen.theme().titleText;
      headline.color = fui::Color::Black;
      headline.align = fui::TextAlign::Center;
      headline.maxLines = 2;
      const fui::Rect body = screen.body();
      const int16_t headlineH =
          fui::measureWrappedText(screen.target(), model.emptyHeadline, headline, body.width).height;

      fui::TextStyle message = screen.theme().smallText;
      message.align = fui::TextAlign::Center;
      message.maxLines = 4;
      const bool hasMessage = model.emptyMessage != nullptr;
      const int16_t messageH =
          hasMessage ? fui::measureWrappedText(screen.target(), model.emptyMessage, message, body.width).height : 0;
      const int16_t gap = hasMessage ? toybox::kGutter : 0;

      const bool hasAction = model.emptyActionLabel != nullptr && model.emptyAction != fui::NO_ACTION;
      const int16_t actionH = hasAction ? static_cast<int16_t>(kFooterHeight + toybox::kGutter * 2) : 0;

      int16_t y = static_cast<int16_t>(body.y + (body.height - headlineH - gap - messageH - actionH) / 2);
      screen.target().text(fui::makeRect(body.x, y, body.width, headlineH), model.emptyHeadline, headline);
      y = static_cast<int16_t>(y + headlineH);
      if (hasMessage) {
        y = static_cast<int16_t>(y + gap);
        screen.target().text(fui::makeRect(body.x, y, body.width, messageH), model.emptyMessage, message);
        y = static_cast<int16_t>(y + messageH);
      }
      if (hasAction) {
        y = static_cast<int16_t>(y + toybox::kGutter * 2);
        fui::ButtonProps button;
        button.label = model.emptyActionLabel;
        button.action = model.emptyAction;
        button.styles = toybox::invertedStyles();
        button.radius = static_cast<uint8_t>(toybox::kPillRadius);
        const int16_t width = static_cast<int16_t>(body.width * 3 / 4);
        screen.button(button, fui::makeRect(static_cast<int16_t>(body.x + (body.width - width) / 2), y, width,
                                            static_cast<int16_t>(kFooterHeight)));
      }
    } else {
      screen.centeredText("NOTHING HERE", screen.theme().bodyText);
    }
    return;
  }

  fui::ListProps list;
  list.items = model.items;
  list.count = static_cast<uint16_t>(model.count);
  list.topIndex = static_cast<uint16_t>(model.topIndex);
  list.selectedIndex = static_cast<int16_t>(model.selected);
  list.action = ActionRow;
  list.rowHeight = listRowHeight(screen.target(), screen.theme());
  list.labelText = screen.theme().bodyText;
  list.labelText.maxLines = 2;
  list.valueText = listValueStyle(screen.theme());
  list.balanceWrappedLabelWithValue = false;
  screen.list(list);
}

// --- The reader --------------------------------------------------------------

fui::Rect readerBody(const fui::DeviceContext& device) {
  const int bottom = toybox::kMargin + kFooterHeight + toybox::kGutter;
  return fui::makeRect(toybox::kMargin, kBodyTop, static_cast<int16_t>(device.width - 2 * toybox::kMargin),
                       static_cast<int16_t>(device.height - bottom - kBodyTop));
}

uint32_t readerLineCount(const fui::DrawTarget& target, const fui::DeviceContext& device, ReaderBody& body) {
  if (body.wrap == nullptr) return 0;
  return body.wrap->lineCount(target, readerBody(device).width, body.text, body.style);
}

uint32_t buildReader(toybox::Screen& screen, const ReaderModel& model, ReaderBody& body) {
  // The band carries the article's own title in the bold reading cut, handed
  // over whole so headerBand's one fitter walks the ladder; fitting it twice is
  // the drift HackerNewsScreens documents (card #268).
  fui::TextStyle bandTitle = screen.theme().titleText;
  bandTitle.align = screen.theme().headerTitleAlign;
  bandTitle.maxLines = 1;
  Chip chip;
  if (model.saved) {
    chip.label = "SAVED";
    chip.action = ActionUnsave;
    chip.filled = true;
  }
  chrome(screen, model.title, model.pageLabel, &bandTitle, chip);

  const fui::DeviceContext& device = screen.device();

  // Two page arrows, each half the width. No swap control: there are no
  // comments here, so the reader is only ever the article.
  const int16_t footerY = static_cast<int16_t>(device.height - toybox::kMargin - kFooterHeight);
  const int16_t usable = static_cast<int16_t>(device.width - 2 * toybox::kMargin);
  const int16_t half = static_cast<int16_t>((usable - toybox::kGutter) / 2);

  const auto footerButton = [&screen, footerY](const char* label, const fui::ActionId action, const int16_t x,
                                               const int16_t width, const bool enabled) {
    fui::ButtonProps button;
    button.label = label;
    button.action = enabled ? action : fui::NO_ACTION;
    if (!enabled) button.styles = toybox::disabledButtonStyles();
    screen.button(button, fui::makeRect(x, footerY, width, kFooterHeight));
  };

  const int16_t left = toybox::kMargin;
  footerButton("<", ActionPagePrev, left, half, model.canPagePrev);
  footerButton(">", ActionPageNext, static_cast<int16_t>(left + half + toybox::kGutter), half, model.canPageNext);

  if (body.wrap == nullptr) return 0;
  body.wrap->draw(screen.target(), readerBody(device), body.text, body.style, model.topLine);

  if (model.notice != nullptr && model.notice[0] != '\0') {
    fui::StyleSet noticeStyles = toybox::invertedStyles();
    noticeStyles.normal.border = fui::Paint::solid(fui::Color::White);
    noticeStyles.normal.borderWidth = toybox::kHairline;
    noticeStyles.selected = noticeStyles.focused = noticeStyles.active = noticeStyles.disabled = noticeStyles.normal;
    fui::ToastProps toast;
    toast.message = model.notice;
    toast.styles = noticeStyles;
    toast.text = screen.theme().bodyText;
    toast.text.color = fui::Color::White;
    toast.text.maxLines = 2;
    toast.anchor = fui::ToastAnchor::Center;
    fui::toast(screen.frame(), readerBody(device), toast);
  }
  return body.wrap->lineCount(screen.target(), readerBody(device).width, body.text, body.style);
}

// --- Notices -----------------------------------------------------------------

void buildNotice(toybox::Screen& screen, const NoticeModel& model) {
  chrome(screen, "PINBOARD", nullptr);

  const fui::DeviceContext& device = screen.device();
  const int16_t width = static_cast<int16_t>(device.width - 2 * toybox::kMargin);

  const bool hasAction = model.actionLabel != nullptr && model.action != fui::NO_ACTION;
  if (hasAction) {
    fui::ButtonProps action;
    action.label = model.actionLabel;
    action.action = model.action;
    screen.button(action, fui::makeRect(toybox::kMargin,
                                        static_cast<int16_t>(device.height - toybox::kMargin - toybox::kPillHeight),
                                        width, toybox::kPillHeight));
  }

  int16_t y = kBodyTop;

  if (model.mark != nullptr) {
    const int16_t markSize = toybox::kIconSize * 2;
    screen.target().bitmap(fui::makeRect(toybox::kMargin, y, markSize, markSize), fui::bitmapFromIcon(*model.mark),
                           fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + markSize + toybox::kGutter);
  }

  if (model.headline != nullptr && model.headline[0] != '\0') {
    fui::TextStyle headline = screen.theme().titleText;
    headline.color = fui::Color::Black;
    headline.align = fui::TextAlign::Left;
    headline.maxLines = 2;
    const int16_t headlineHeight = static_cast<int16_t>(2 * screen.target().lineHeight(headline.font));
    screen.target().text(fui::makeRect(toybox::kMargin, y, width, headlineHeight), model.headline, headline);
    y = static_cast<int16_t>(y + headlineHeight + toybox::kGutter);

    screen.target().fill(fui::makeRect(toybox::kMargin, y, width, toybox::kRule), fui::Paint::solid(fui::Color::Black));
    y = static_cast<int16_t>(y + toybox::kRule + toybox::kGutter * 2);
  }

  if (model.message != nullptr && model.message[0] != '\0') {
    const int16_t reserved = hasAction ? static_cast<int16_t>(toybox::kPillHeight + toybox::kGutter) : 0;
    const int16_t bottom = static_cast<int16_t>(device.height - toybox::kMargin - reserved);
    fui::TextAreaProps message;
    message.text = model.message;
    message.showCaret = false;
    message.style = screen.theme().bodyText;
    fui::textArea(screen.frame(), fui::makeRect(toybox::kMargin, y, width, static_cast<int16_t>(bottom - y)), message);
  }
}

}  // namespace pbui
