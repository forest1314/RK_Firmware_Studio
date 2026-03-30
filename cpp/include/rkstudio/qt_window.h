#pragma once

#include "rkstudio/backend.h"

namespace rkstudio {

int RunQtApp(
    int argc,
    char** argv,
    StudioBackend& backend,
    bool smoke_test,
    const std::string& capture_path = "",
    int capture_nav_page = 0);

int GenerateHelpGuideAssets(int argc, char** argv, StudioBackend& backend);

}  // namespace rkstudio
