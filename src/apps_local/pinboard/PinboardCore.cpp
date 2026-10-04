#include "PinboardCore.h"

#include <cctype>

namespace pin {

namespace {

constexpr const char* kAllBase = "https://api.pinboard.in/v1/posts/all";
constexpr const char* kRecentBase = "https://api.pinboard.in/v1/posts/recent";
constexpr const char* kAddBase = "https://api.pinboard.in/v1/posts/add";
constexpr int kMaxRecent = 100;  // Pinboard's ceiling for posts/recent

std::string_view trim(std::string_view s) {
  size_t a = 0;
  size_t b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

bool startsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

bool parseConfig(std::string_view text, Config& out) {
  out = Config{};
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t nl = text.find('\n', pos);
    const std::string_view raw = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    const std::string_view line = trim(raw);
    pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;

    if (line.empty() || line.front() == '#') continue;
    if (startsWith(line, "tag=")) {
      out.tag = std::string(trim(line.substr(4)));
      continue;
    }
    // The token is the first line carrying a colon (username:TOKEN). Taken once
    // so a stray later line cannot overwrite it.
    if (out.token.empty() && line.find(':') != std::string_view::npos) {
      out.token = std::string(line);
    }
  }
  return out.valid();
}

std::string urlEncode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(s.size() * 3);
  for (const unsigned char c : s) {
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                            c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0F]);
    }
  }
  return out;
}

std::string recentUrl(const Config& cfg, int count) {
  if (count < 1) count = 1;
  if (count > kMaxRecent) count = kMaxRecent;
  std::string url = kRecentBase;
  url += "?format=json&count=";
  url += std::to_string(count);
  url += "&auth_token=";
  url += cfg.token;
  if (!cfg.tag.empty()) {
    url += "&tag=";
    url += urlEncode(cfg.tag);
  }
  return url;
}

std::string allUrl(const Config& cfg) {
  // The token's colon is left as-is: Pinboard's auth_token is literally
  // "username:TOKEN" and its servers expect the colon unencoded.
  std::string url = kAllBase;
  url += "?toread=yes&format=json&auth_token=";
  url += cfg.token;
  if (!cfg.tag.empty()) {
    url += "&tag=";
    url += urlEncode(cfg.tag);
  }
  return url;
}

std::string markReadUrl(const Config& cfg, const Bookmark& bm) {
  std::string url = kAddBase;
  url += "?auth_token=";
  url += cfg.token;
  url += "&format=json&replace=yes&toread=no";
  url += "&url=";
  url += urlEncode(bm.url);
  url += "&description=";
  url += urlEncode(bm.title);
  if (!bm.extended.empty()) {
    url += "&extended=";
    url += urlEncode(bm.extended);
  }
  if (!bm.tags.empty()) {
    url += "&tags=";
    url += urlEncode(bm.tags);
  }
  return url;
}

std::string domainOf(std::string_view url) {
  const size_t scheme = url.find("://");
  std::string_view rest = scheme == std::string_view::npos ? url : url.substr(scheme + 3);
  const size_t slash = rest.find('/');
  std::string_view host = slash == std::string_view::npos ? rest : rest.substr(0, slash);
  const size_t at = host.find('@');  // strip any userinfo
  if (at != std::string_view::npos) host = host.substr(at + 1);
  const size_t colon = host.find(':');  // strip any port
  if (colon != std::string_view::npos) host = host.substr(0, colon);
  if (startsWith(host, "www.")) host = host.substr(4);
  return std::string(host);
}

}  // namespace pin
