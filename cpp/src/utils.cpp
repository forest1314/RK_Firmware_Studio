#include "rkstudio/utils.h"

#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace rkstudio {

std::string Trim(const std::string& text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
    ++start;
  }
  std::size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(start, end - start);
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

std::string ToUpper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return text;
}

fs::path ExpandUser(const fs::path& input) {
  const std::string text = input.string();
  if (text.empty() || text[0] != '~') {
    return input;
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || text == "~") {
    return fs::path(home == nullptr ? text : home);
  }
  if (text.size() >= 2 && text[1] == '/') {
    return fs::path(home) / text.substr(2);
  }
  return input;
}

fs::path ResolvePath(const fs::path& input) {
  return fs::weakly_canonical(ExpandUser(input));
}

std::string FormatFileSize(std::uint64_t size) {
  if (size == 0) {
    return "0 B";
  }
  static const std::vector<std::string> units = {"B", "KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(size);
  for (const auto& unit : units) {
    if (value < 1024.0 || unit == units.back()) {
      std::ostringstream stream;
      if (unit == "B") {
        stream << static_cast<std::uint64_t>(value) << ' ' << unit;
      } else {
        stream << std::fixed << std::setprecision(1) << value << ' ' << unit;
      }
      return stream.str();
    }
    value /= 1024.0;
  }
  return std::to_string(size) + " B";
}

}  // namespace rkstudio
