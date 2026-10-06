#include "PinboardCore.h"

#include <Utf8.h>

#include <algorithm>

namespace pin {
namespace {

std::string_view trim(std::string_view s) {
  const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (!s.empty() && blank(s.front())) s.remove_prefix(1);
  while (!s.empty() && blank(s.back())) s.remove_suffix(1);
  return s;
}

int clampResults(int n) {
  if (n < 1) return 1;
  if (n > kMaxResultsCeiling) return kMaxResultsCeiling;
  return n;
}

}  // namespace

AuthToken parseAuthToken(const std::string_view input) {
  AuthToken token;
  const std::string_view trimmed = trim(input);
  const size_t colon = trimmed.find(':');
  // Exactly one well-formed split: a colon with something on each side. A token
  // with no colon, or an empty half, is not a Pinboard token and is refused
  // before anything is stored.
  if (colon == std::string_view::npos || colon == 0 || colon + 1 >= trimmed.size()) return token;
  token.user = std::string(trimmed.substr(0, colon));
  token.secret = std::string(trimmed.substr(colon + 1));
  token.valid = !token.user.empty() && !token.secret.empty();
  return token;
}

std::string authParam(const AuthToken& token) {
  if (!token.valid) return {};
  return token.user + ":" + token.secret;
}

std::string listPath(const AuthToken& token, const int maxResults) {
  const std::string auth = authParam(token);
  if (auth.empty()) return {};
  std::string path = "/v1/posts/all?format=json&auth_token=";
  path += auth;
  path += "&results=";
  path += std::to_string(clampResults(maxResults));
  return path;
}

std::string displayTitle(const Bookmark& bookmark) {
  if (!bookmark.title.empty()) return utf8FoldTypography(bookmark.title);

  // No description: show the URL, cleaned to the part a person reads.
  std::string_view url = bookmark.url;
  for (const std::string_view scheme : {std::string_view("https://"), std::string_view("http://")}) {
    if (url.substr(0, scheme.size()) == scheme) {
      url.remove_prefix(scheme.size());
      break;
    }
  }
  if (url.substr(0, 4) == "www.") url.remove_prefix(4);
  while (!url.empty() && url.back() == '/') url.remove_suffix(1);
  return utf8FoldTypography(std::string(url));
}

std::vector<Bookmark> onlyToRead(const std::vector<Bookmark>& all) {
  std::vector<Bookmark> out;
  out.reserve(all.size());
  for (const Bookmark& b : all) {
    if (b.toRead) out.push_back(b);
  }
  return out;
}

}  // namespace pin
