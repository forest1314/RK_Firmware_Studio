#pragma once

#include <string>
#include <utility>
#include <vector>

#include "rkstudio/types.h"

namespace rkstudio {

struct CommandSpec {
  std::string code;
  std::string label;
  std::string group;
  std::string description;
};

std::vector<CommandSpec> GetUpgradeCommandSpecs();
CommandSpec FindUpgradeCommandSpec(const std::string& code);
std::vector<std::string> BuildUpgradeArgs(const std::string& code, const StringMap& values);
std::vector<std::string> SplitShellArgs(const std::string& text);

}  // namespace rkstudio
