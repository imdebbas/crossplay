#pragma once

// The saved library and the account token: the directory on the SD card and the
// questions this app asks about it. A sibling of HackerNewsLibrary, which owns
// the same job for that app; PinboardSaved owns the index format, this owns the
// card.
//
// It carries the token as well as the articles because both live in the same
// directory (/.crosspoint/pinboard/) and both are plain reads and writes of one
// file -- there is no second store to justify. Deciding WHAT to save, building
// rows, and the reader stay in the Activity.

#include <string>
#include <vector>

#include "PinboardSaved.h"

namespace pin {

class Library {
 public:
  // Reads the index once. Cheap and idempotent.
  void load();

  const std::vector<SavedArticle>& articles() const { return articles_; }
  bool empty() const { return articles_.empty(); }

  bool contains(const std::string& url) const;

  // Writes the words, then adds or updates the row. The words go first: an index
  // row pointing at a missing file is the one state that makes the library look
  // broken, and it is what a failed write would leave.
  bool save(const std::string& url, const std::string& title, const std::string& text);

  bool remove(const std::string& url);

  // The stored text, or false when the row exists and the file does not.
  bool readArticle(const SavedArticle& article, std::string& out) const;

  // --- The account token -----------------------------------------------------
  // Stored as the raw "user:secret" line in /.crosspoint/pinboard/.token, so
  // clearing the directory clears the credential with the cache. Trimmed of a
  // trailing newline on read.
  std::string loadToken() const;
  bool saveToken(const std::string& token);
  void clearToken();

 private:
  bool writeIndex();

  std::vector<SavedArticle> articles_;
  bool loaded_ = false;
};

}  // namespace pin
