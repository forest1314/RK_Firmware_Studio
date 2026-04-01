#pragma once

#include <optional>
#include <vector>

#include <QString>

namespace rkstudio {

struct ProgressStage {
  const char* needle;
  int percent;
};

int ClampProgressValue(int value);
const std::vector<ProgressStage>& UpgradeProgressStages(const QString& code);
std::optional<int> ExtractUpgradePhasePercent(const QString& code, const QString& text);
std::optional<int> ExtractDisplayedProgressPercent(const QString& text);
std::optional<QString> ExtractProgressLogKey(const QString& line);
QString ExtractProgressDisplayTitle(const QString& line);
std::optional<QString> MatchProgressTargetToken(const QString& line);

}  // namespace rkstudio
