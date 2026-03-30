#include "rkstudio/paths.h"

namespace fs = std::filesystem;

namespace rkstudio {

AppPaths DiscoverPaths() {
  const fs::path root = fs::weakly_canonical(fs::path(RKSTUDIO_SOURCE_ROOT));
  const fs::path workspace = root / "workspace";
  const fs::path projects = workspace / "projects";
  const fs::path default_project = projects / "default";
  const fs::path unpack = workspace / "unpack";
  const fs::path logs = workspace / "logs";
  return AppPaths{
      root,
      root / "tools" / "pack",
      root / "tools" / "upgrade",
      workspace,
      projects,
      default_project,
      unpack,
      logs,
      workspace / "rkstudio_state.json",
  };
}

void EnsureAppDirs(const AppPaths& paths) {
  for (const auto& path : {
           paths.workspace,
           paths.projects,
           paths.default_project,
           paths.default_project / "Image",
           paths.unpack,
           paths.logs,
       }) {
    fs::create_directories(path);
  }
}

}  // namespace rkstudio
