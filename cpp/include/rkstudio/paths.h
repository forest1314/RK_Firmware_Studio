#pragma once

#include <filesystem>

namespace rkstudio {

struct AppPaths {
  std::filesystem::path root;
  std::filesystem::path pack_tools;
  std::filesystem::path upgrade_tools;
  std::filesystem::path workspace;
  std::filesystem::path projects;
  std::filesystem::path default_project;
  std::filesystem::path unpack;
  std::filesystem::path logs;
  std::filesystem::path state_file;
};

AppPaths DiscoverPaths();
void EnsureAppDirs(const AppPaths& paths);

}  // namespace rkstudio
