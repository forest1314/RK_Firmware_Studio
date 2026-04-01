#include "rkstudio/qt_progress.h"

#include <cmath>

#include <QRegularExpression>
#include <QStringLiteral>

namespace rkstudio {

namespace {

std::optional<int> ExtractLatestProgressPercent(const QString& text) {
  static const QRegularExpression pattern(QStringLiteral(R"((\d{1,3})%)"));
  auto matches = pattern.globalMatch(text);
  std::optional<int> latest;
  while (matches.hasNext()) {
    const auto match = matches.next();
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    if (ok) {
      latest = ClampProgressValue(value);
    }
  }
  return latest;
}

int ScaleProgressPercent(int value, int from, int to) {
  const int clamped = ClampProgressValue(value);
  if (to <= from) {
    return ClampProgressValue(to);
  }
  const double ratio = static_cast<double>(clamped) / 100.0;
  return ClampProgressValue(static_cast<int>(std::round(from + (to - from) * ratio)));
}

std::optional<int> ExtractPercentForPattern(const QString& text, const QString& pattern) {
  const QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
  auto matches = regex.globalMatch(text);
  std::optional<int> latest;
  while (matches.hasNext()) {
    const auto match = matches.next();
    bool ok = false;
    const int value = match.captured(1).toInt(&ok);
    if (ok) {
      latest = ClampProgressValue(value);
    }
  }
  return latest;
}

}  // namespace

int ClampProgressValue(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}

const std::vector<ProgressStage>& UpgradeProgressStages(const QString& code) {
  static const std::vector<ProgressStage> uf = {
      {"Loading loader...", 2},
      {"Start to upgrade firmware...", 4},
      {"Check Chip Success", 6},
      {"Download Boot Success", 8},
      {"Wait For Loader Success", 10},
      {"Get FlashInfo Success", 12},
      {"Prepare IDB Start", 14},
      {"Prepare IDB Success", 16},
      {"Download IDB Start", 18},
      {"Download IDB Success", 22},
      {"Download Firmware Start", 24},
      {"Start to download ", 24},
      {"Check Image Start", 94},
      {"Check Image Success", 98},
      {"Upgrade firmware ok.", 98},
  };
  static const std::vector<ProgressStage> db = {
      {"Loading loader...", 4},
      {"Test Device Start", 8},
      {"Test Device Success", 28},
      {"Download Boot Start", 40},
      {"Download Boot Success", 94},
  };
  static const std::vector<ProgressStage> ul = {
      {"Loading loader...", 2},
      {"Start to upgrade loader...", 4},
      {"Check Chip Success", 8},
      {"Get FlashInfo Success", 14},
      {"Prepare IDB Start", 18},
      {"Prepare IDB Success", 24},
      {"Download IDB Start", 28},
      {"Download IDB Success", 92},
      {"Upgrade loader ok.", 98},
  };
  static const std::vector<ProgressStage> ef = {
      {"Loading loader...", 2},
      {"Start to erase flash...", 4},
      {"Test Device Start", 6},
      {"Test Device Success", 14},
      {"Get FlashInfo Start", 18},
      {"Get FlashInfo Success", 24},
      {"Prepare IDB Start", 28},
      {"Prepare IDB Success", 34},
      {"Erase IDB Start", 40},
      {"Erase IDB Success", 52},
      {"Erase Flash Start", 56},
      {"Erase Flash Success", 92},
      {"Reset Device Start", 94},
      {"Reset Device Success", 95},
      {"Wait For Maskrom Start", 96},
      {"Wait For Maskrom Success", 97},
      {"Download Boot Start", 98},
      {"Download Boot Success", 99},
  };
  static const std::vector<ProgressStage> di = {
      {"Loading loader...", 2},
      {"Check Chip Success", 6},
      {"Download Boot Success", 10},
      {"Wait For Loader Success", 14},
      {"Get FlashInfo Success", 18},
      {"Write parameter Start", 20},
      {"Write parameter Success", 24},
      {"Start to write ", 24},
      {"Start to check ", 90},
      {"Reset Device Success", 96},
  };
  static const std::vector<ProgressStage> fallback = {
      {"Loading loader...", 8},
      {"Check Chip Success", 18},
      {"Get FlashInfo Success", 32},
      {"Prepare IDB Success", 48},
      {"Download Boot Success", 64},
      {"Download IDB Success", 78},
      {"Download Firmware Success", 92},
      {"Erase Flash Success", 96},
      {"Upgrade firmware ok.", 100},
      {"Upgrade loader ok.", 100},
  };

  if (code == QStringLiteral("UF")) return uf;
  if (code == QStringLiteral("DB")) return db;
  if (code == QStringLiteral("UL")) return ul;
  if (code == QStringLiteral("EF")) return ef;
  if (code == QStringLiteral("DI")) return di;
  return fallback;
}

std::optional<int> ExtractUpgradePhasePercent(const QString& code, const QString& text) {
  if (code == QStringLiteral("UF")) {
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Download Image.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 24, 94);
    }
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Check Image.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 94, 98);
    }
  }
  if (code == QStringLiteral("DI")) {
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Write parameter.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 18, 24);
    }
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Write file.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 24, 90);
    }
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Check file.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 90, 98);
    }
  }
  if (code == QStringLiteral("EF")) {
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Test Device.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 4, 14);
    }
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Erase Flash.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 36, 92);
    }
  }
  if (code == QStringLiteral("UL")) {
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Write parameter.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 20, 34);
    }
  }
  if (code == QStringLiteral("DB")) {
    if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(Test Device.*?\((\d{1,3})%\))")); value.has_value()) {
      return ScaleProgressPercent(*value, 8, 28);
    }
  }
  return std::nullopt;
}

std::optional<int> ExtractDisplayedProgressPercent(const QString& text) {
  if (const auto value = ExtractPercentForPattern(text, QStringLiteral(R"(\((\d{1,3})%\))")); value.has_value()) {
    return value;
  }
  return ExtractLatestProgressPercent(text);
}

std::optional<QString> ExtractProgressLogKey(const QString& line) {
  static const QRegularExpression progress_line_pattern(
      QStringLiteral(R"(^(.*?)(?:\s*\((\d{1,3})%\))\s*$)"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = progress_line_pattern.match(line.trimmed());
  if (!match.hasMatch()) {
    return std::nullopt;
  }
  const QString key = match.captured(1).trimmed();
  if (key.isEmpty()) {
    return std::nullopt;
  }
  return key;
}

QString ExtractProgressDisplayTitle(const QString& line) {
  if (const auto key = ExtractProgressLogKey(line); key.has_value()) {
    return *key;
  }
  QString title = line.trimmed();
  title.remove(QStringLiteral(" Start"));
  title.remove(QStringLiteral(" Success"));
  title.remove(QStringLiteral(" Fail"));
  title.remove(QStringLiteral(" start..."));
  title.remove(QStringLiteral(" start.."));
  title.remove(QStringLiteral(" start."));
  title.remove(QStringLiteral("..."));
  return title.trimmed();
}

std::optional<QString> MatchProgressTargetToken(const QString& line) {
  static const QRegularExpression download_start_pattern(
      QStringLiteral(R"(Download\s+([A-Za-z0-9._-]+)\s+start)"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression start_to_pattern(
      QStringLiteral(R"(Start to (?:write|check)\s+(.+?)(?:\.\.\.|$))"),
      QRegularExpression::CaseInsensitiveOption);
  const auto download_match = download_start_pattern.match(line);
  if (download_match.hasMatch()) {
    return download_match.captured(1).trimmed();
  }
  const auto start_to_match = start_to_pattern.match(line);
  if (start_to_match.hasMatch()) {
    return start_to_match.captured(1).trimmed();
  }
  return std::nullopt;
}

}  // namespace rkstudio
