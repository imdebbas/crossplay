#pragma once

// Save for later: the local half, which is the whole feature.
//
// ---------------------------------------------------------------------------
// Entirely local: once an article's text is on the card it needs no account, no
// token and no service to read. The Pinboard account is only how the LIST of
// things to save is discovered; a saved article is complete on its own and
// survives the day Pinboard, or r.jina.ai, stops answering.
//
// The format is chosen to outlive the code, and is deliberately the same shape
// as Hacker News's (HackerNewsSaved.h): a plain tab-separated index and one
// plain UTF-8 text file per article, so the library is readable in any editor
// and recoverable by hand -- and if this app is ever deleted, the articles are
// still there. There is no reason to invent a second format, so this does not.
// ---------------------------------------------------------------------------
//
// Freestanding: parsing and formatting only. The Activity owns the SD card.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pin {

struct SavedArticle {
  // Derived from the URL, so saving the same page twice updates one entry
  // rather than growing a duplicate. Also the article's filename.
  std::string id;
  std::string title;
  std::string url;
  uint32_t savedAt = 0;
};

// A stable 32-bit FNV-1a of the URL, in lowercase hex. Short enough for a
// filename on a FAT card and stable across reboots, which a counter would not
// be once an entry in the middle is removed.
std::string savedIdFor(std::string_view url);

// The index as it is written to the card. One header line carrying a format
// version, then one tab-separated line per article.
//
// Tab-separated rather than JSON because this file is the thing that has to
// survive: it is readable in any text editor, recoverable by hand if a write is
// ever interrupted, and needs no parser to inspect. Titles are sanitised of
// tabs and newlines on the way in, so a field can never swallow the next one.
std::string serializeSavedIndex(const std::vector<SavedArticle>& articles);

// Returns false only when the text is not this format at all. A single damaged
// line is skipped rather than failing the whole file: losing one entry beats
// losing the library.
bool parseSavedIndex(std::string_view text, std::vector<SavedArticle>& out);

// Strip anything that would break the row format, and collapse whitespace.
// Does NOT fold typographic punctuation, and must not: this runs on the URL
// column as well as the title, and savedIdFor() hashes the URL, so a character
// changed here is an article that can never be unsaved again. The title is
// folded by serializeSavedIndex and parseSavedIndex instead, at both ends, so
// an index written before the fold existed reads correctly.
std::string sanitizeField(std::string_view text);

}  // namespace pin
