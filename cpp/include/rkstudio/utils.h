#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace rkstudio {

class BackendError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

std::string Trim(const std::string& text);
std::string ToLower(std::string text);
std::string ToUpper(std::string text);
std::filesystem::path ExpandUser(const std::filesystem::path& input);
std::filesystem::path ResolvePath(const std::filesystem::path& input);
std::string FormatFileSize(std::uint64_t size);

}  // namespace rkstudio
