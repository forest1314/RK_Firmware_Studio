#include "rkstudio/qt_window.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <initializer_list>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QSplitter>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace rkstudio {

namespace {

QString Q(const std::string& value) {
  return QString::fromUtf8(value.c_str());
}

QString Q(const char* value) {
  return QString::fromUtf8(value);
}

QString AssetPath(const char* relative_path) {
  return QString::fromUtf8((std::filesystem::path(RKSTUDIO_SOURCE_ROOT) / relative_path).string().c_str());
}

QString ReadTextFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return QString();
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return Q(buffer.str());
}

QString ModeAssetPath(const QString& code) {
  if (code == QStringLiteral("UF")) return AssetPath("rkstudio/assets/mode-uf.svg");
  if (code == QStringLiteral("DB")) return AssetPath("rkstudio/assets/mode-ml.svg");
  if (code == QStringLiteral("UL")) return AssetPath("rkstudio/assets/mode-fl.svg");
  if (code == QStringLiteral("EF")) return AssetPath("rkstudio/assets/mode-ei.svg");
  return AssetPath("rkstudio/assets/mode-pf.svg");
}

QString ShortPathLabel(const std::filesystem::path& path) {
  const QString text = Q(path.string()).trimmed();
  if (text.isEmpty()) {
    return Q("-");
  }
  const QStringList parts = text.split('/', Qt::SkipEmptyParts);
  if (!parts.isEmpty()) {
    return parts.last();
  }
  return text;
}

std::filesystem::path ExpandUserPath(const std::filesystem::path& input) {
  const std::string text = input.string();
  if (text.empty() || text[0] != '~') {
    return input;
  }
  const char* home = std::getenv("HOME");
  if (home == nullptr || text == "~") {
    return std::filesystem::path(home == nullptr ? text : home);
  }
  if (text.size() >= 2 && text[1] == '/') {
    return std::filesystem::path(home) / text.substr(2);
  }
  return input;
}

std::filesystem::path ResolveUiPath(
    const std::filesystem::path& input,
    const std::optional<std::filesystem::path>& base_path = std::nullopt) {
  auto resolved = ExpandUserPath(input);
  if (resolved.is_relative() && base_path.has_value()) {
    resolved = *base_path / resolved;
  }
  return resolved.lexically_normal();
}

struct PartitionSizeInput {
  bool valid = false;
  std::optional<std::uint64_t> sectors;
};

QString FormatSectorCount(std::uint64_t value) {
  return QStringLiteral("0x%1").arg(static_cast<qulonglong>(value), 8, 16, QChar('0'));
}

QString FormatPartitionCapacityText(const PartitionDefinition& definition) {
  if (!definition.size_sectors.has_value()) {
    return QStringLiteral("剩余空间");
  }
  return Q(FormatFileSize(*definition.size_sectors * static_cast<std::uint64_t>(kSectorSize)));
}

QString FormatPartitionCapacityTooltip(const PartitionDefinition& definition) {
  if (!definition.size_sectors.has_value()) {
    return QStringLiteral("grow / 剩余空间");
  }
  return QStringLiteral("%1 / %2 sectors")
      .arg(FormatPartitionCapacityText(definition))
      .arg(FormatSectorCount(*definition.size_sectors));
}

PartitionSizeInput ParsePartitionCapacityInput(const QString& raw_text) {
  QString compact = raw_text.trimmed();
  compact.remove(' ');
  if (compact.isEmpty()) {
    return {};
  }

  const QString lower = compact.toLower();
  if (lower == QStringLiteral("-") || lower == QStringLiteral("grow") || lower == QStringLiteral("auto") ||
      lower == QStringLiteral("remaining") || lower == QStringLiteral("rest") || lower == QStringLiteral("剩余") ||
      lower == QStringLiteral("剩余空间")) {
    return {true, std::nullopt};
  }

  auto parse_integer = [](const QString& text) -> std::optional<std::uint64_t> {
    bool ok = false;
    const auto value = text.toULongLong(&ok, 0);
    if (!ok) {
      return std::nullopt;
    }
    return value;
  };

  for (const QString& suffix : {QStringLiteral("sectors"), QStringLiteral("sector"), QStringLiteral("sec"), QStringLiteral("s")}) {
    if (lower.endsWith(suffix)) {
      const auto sectors = parse_integer(lower.left(lower.size() - suffix.size()));
      if (!sectors.has_value()) {
        return {};
      }
      return {true, *sectors};
    }
  }

  if (lower.startsWith(QStringLiteral("0x"))) {
    const auto sectors = parse_integer(lower);
    if (!sectors.has_value()) {
      return {};
    }
    return {true, *sectors};
  }

  struct UnitRule {
    const char* suffix;
    long double multiplier;
  };
  static const std::vector<UnitRule> units = {
      {"gib", 1024.0L * 1024.0L * 1024.0L},
      {"gb", 1000.0L * 1000.0L * 1000.0L},
      {"g", 1024.0L * 1024.0L * 1024.0L},
      {"mib", 1024.0L * 1024.0L},
      {"mb", 1000.0L * 1000.0L},
      {"m", 1024.0L * 1024.0L},
      {"kib", 1024.0L},
      {"kb", 1000.0L},
      {"k", 1024.0L},
      {"b", 1.0L},
  };

  QString number_text = lower;
  long double multiplier = 1024.0L * 1024.0L;
  for (const auto& unit : units) {
    const QString suffix = QString::fromUtf8(unit.suffix);
    if (lower.endsWith(suffix)) {
      number_text = lower.left(lower.size() - suffix.size());
      multiplier = unit.multiplier;
      break;
    }
  }

  bool ok = false;
  const long double value = number_text.toDouble(&ok);
  if (!ok || value < 0.0L) {
    return {};
  }
  const auto bytes = value * multiplier;
  const auto sectors = static_cast<std::uint64_t>(std::ceil(bytes / static_cast<long double>(kSectorSize)));
  return {true, sectors};
}

QFont BuildUiFont() {
  static const QStringList preferred_families = {
      QStringLiteral("Microsoft YaHei UI"),
      QStringLiteral("Microsoft YaHei"),
      QStringLiteral("Noto Sans CJK SC"),
      QStringLiteral("Noto Sans SC"),
      QStringLiteral("Source Han Sans SC"),
      QStringLiteral("PingFang SC"),
      QStringLiteral("WenQuanYi Micro Hei"),
      QStringLiteral("Sarasa UI SC"),
  };

  const QStringList available = QFontDatabase().families();
  for (const auto& family : preferred_families) {
    if (available.contains(family)) {
      QFont font(family);
      font.setPointSize(10);
      font.setStyleStrategy(QFont::PreferAntialias);
      return font;
    }
  }

  QFont font;
  font.setPointSize(10);
  font.setStyleStrategy(QFont::PreferAntialias);
  return font;
}

QString BuildStyleSheet() {
  return QString::fromUtf8(R"(
    QWidget {
      color: #182116;
      font-size: 12px;
    }
    QLabel {
      background: transparent;
    }
    QMainWindow {
      background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #e8eee0, stop:0.55 #dfe7d5, stop:1 #c8d1bb);
    }
    #RootWidget {
      background: transparent;
    }
    #TopBar, #NavBar, #SummaryBand, #PanelCard, #StatusPill, #StatTile, #StateLegend, #TopStatusStrip, #EmptyStateBox {
      border: 1px solid rgba(70, 86, 53, 0.20);
      border-radius: 10px;
      background: rgba(248, 250, 244, 0.94);
    }
    #TopBar {
      padding: 2px;
    }
    #NavBar {
      border-radius: 8px;
      background: rgba(240, 244, 234, 0.88);
    }
    #SummaryBand {
      border-radius: 8px;
      background: rgba(245, 248, 239, 0.88);
    }
    #BrandMark {
      min-width: 34px;
      max-width: 34px;
      min-height: 34px;
      max-height: 34px;
      border-radius: 9px;
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #66783a, stop:1 #425620);
      color: #f6f8ef;
      font-size: 14px;
      font-weight: 800;
    }
    #BrandTitle {
      font-size: 15px;
      font-weight: 800;
      color: #1b2319;
    }
    #BrandSubtitle {
      font-size: 9px;
      color: #5d6958;
    }
    #StatusPill {
      min-width: 82px;
      padding: 3px 6px;
      border-radius: 7px;
      background: rgba(252, 253, 250, 0.92);
      border: 0;
    }
    #StatusLabel {
      font-size: 9px;
      color: #63705c;
      font-weight: 700;
    }
    #StatusValue {
      font-size: 11px;
      font-weight: 800;
      color: #1a2118;
    }
    #StateLegend {
      border-radius: 7px;
      background: rgba(247, 250, 242, 0.92);
      border: 0;
    }
    #TopStatusStrip {
      border-radius: 7px;
      background: rgba(247, 250, 242, 0.92);
      border: 0;
    }
    #StateLegendTitle {
      font-size: 9px;
      color: #65705f;
      font-weight: 700;
    }
    #StateBadge {
      border-radius: 7px;
      background: rgba(231, 236, 222, 0.92);
      color: #6c7466;
      padding: 3px 6px;
      font-size: 10px;
      font-weight: 800;
    }
    #StateBadge[active="true"] {
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #7a933d, stop:1 #556d22);
      color: #f5f8ef;
    }
    #NavButton {
      border: 0;
      border-radius: 7px;
      min-height: 30px;
      padding: 5px 10px;
      background: transparent;
      color: #5a6654;
      font-size: 10px;
      font-weight: 800;
      text-align: left;
    }
    #NavButton[active="true"] {
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #728b35, stop:1 #4b6120);
      color: #f5f8ef;
    }
    #SummaryItem {
      border-radius: 7px;
      background: rgba(246, 249, 241, 0.96);
      border: 1px solid rgba(70, 86, 53, 0.14);
      padding: 2px;
    }
    #SummaryTitle {
      font-size: 9px;
      color: #63705c;
      font-weight: 700;
    }
    #SummaryValue {
      font-size: 11px;
      font-weight: 800;
      color: #1b2417;
    }
    #HelpLead {
      color: #556150;
      font-size: 11px;
      font-weight: 700;
      line-height: 1.45em;
    }
    #GuideImageFrame {
      border-radius: 10px;
      background: rgba(255, 255, 255, 0.92);
      border: 1px solid rgba(74, 90, 58, 0.14);
    }
    #GuideImageLabel {
      background: transparent;
      color: #62705a;
      font-size: 11px;
      font-weight: 700;
    }
    #GuideStepRow {
      border-radius: 9px;
      background: rgba(251, 252, 248, 0.94);
      border: 1px solid rgba(74, 90, 58, 0.10);
    }
    #GuideStepBadge {
      min-width: 26px;
      max-width: 26px;
      min-height: 26px;
      max-height: 26px;
      border-radius: 13px;
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #7a933d, stop:1 #556d22);
      color: #f6f8ef;
      font-size: 10px;
      font-weight: 800;
      qproperty-alignment: AlignCenter;
    }
    #GuideStepTitle {
      color: #1b2417;
      font-size: 11px;
      font-weight: 800;
    }
    #GuideStepBody {
      color: #5f6b59;
      font-size: 10px;
      font-weight: 700;
    }
    #GuideModeChip {
      border-radius: 9px;
      background: rgba(249, 251, 245, 0.96);
      border: 1px solid rgba(74, 90, 58, 0.12);
    }
    #GuideModeCode {
      color: #4f6521;
      font-size: 15px;
      font-weight: 900;
    }
    #GuideModeTitle {
      color: #1b2417;
      font-size: 11px;
      font-weight: 800;
    }
    #GuideModeBody {
      color: #5f6b59;
      font-size: 10px;
      font-weight: 700;
    }
    #HelpPathValue {
      border-radius: 8px;
      background: rgba(247, 249, 242, 0.96);
      border: 1px solid rgba(74, 90, 58, 0.10);
      color: #2c3827;
      padding: 8px 10px;
      font-family: "JetBrains Mono", "SFMono-Regular", monospace;
      font-size: 11px;
      font-weight: 700;
    }
    #PanelCard {
      border-radius: 10px;
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(250,251,246,0.96), stop:1 rgba(239,243,232,0.96));
    }
    #PanelTitle {
      font-size: 12px;
      font-weight: 800;
      color: #1c2418;
    }
    #PanelChip {
      border-radius: 7px;
      padding: 3px 7px;
      background: rgba(233, 239, 224, 0.92);
      color: #4f6521;
      font-size: 9px;
      font-weight: 800;
    }
    #FieldLabel {
      font-size: 10px;
      font-weight: 700;
      color: #63705c;
    }
    QLineEdit {
      min-height: 24px;
      border-radius: 7px;
      border: 1px solid rgba(74, 90, 58, 0.20);
      background: rgba(255, 255, 255, 0.92);
      padding: 0 6px;
      color: #182116;
      font-size: 12px;
    }
    QComboBox {
      min-height: 24px;
      border-radius: 7px;
      border: 1px solid rgba(74, 90, 58, 0.20);
      background: rgba(255, 255, 255, 0.92);
      padding: 0 6px;
      color: #182116;
      font-size: 12px;
    }
    QComboBox::drop-down {
      border: 0;
      width: 22px;
    }
    #BrowseButton {
      min-height: 24px;
      min-width: 50px;
      border: 0;
      border-radius: 7px;
      background: rgba(229, 235, 219, 0.96);
      color: #50611f;
      padding: 0 8px;
      font-size: 11px;
      font-weight: 800;
    }
    #TableBrowseButton {
      min-height: 22px;
      min-width: 42px;
      border: 0;
      border-radius: 6px;
      background: rgba(229, 235, 219, 0.96);
      color: #50611f;
      padding: 0 6px;
      font-size: 10px;
      font-weight: 800;
    }
    #PartitionPathEdit {
      min-height: 22px;
      border-radius: 6px;
      font-size: 10px;
      padding: 0 4px;
    }
    #ActionButton, #ModeButton, #MiniButton {
      border: 0;
      border-radius: 7px;
      font-weight: 800;
    }
    #ActionButton {
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6f8831, stop:1 #51671f);
      color: #f5f7ef;
      padding: 5px 8px;
      font-size: 11px;
    }
    #MiniButton {
      background: transparent;
      color: #5b6754;
      padding: 4px 8px;
      font-size: 10px;
    }
    #MiniButton[active="true"] {
      background: rgba(219, 230, 187, 0.9);
      color: #4f6521;
    }
    #ModeButton {
      min-height: 38px;
      font-size: 13px;
      color: #4f6521;
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(246,248,241,0.98), stop:1 rgba(227,234,214,0.98));
      border: 1px solid rgba(92, 106, 70, 0.18);
      text-align: center;
    }
    #ModeButton[active="true"] {
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #7a933d, stop:1 #556d22);
      color: #f6f8ef;
    }
    QPlainTextEdit {
      border-radius: 7px;
      border: 1px solid rgba(67, 78, 54, 0.25);
      background: #22281f;
      color: #dde6d4;
      font-family: "JetBrains Mono", "SFMono-Regular", monospace;
      font-size: 10px;
      padding: 6px;
    }
    #OperationLog {
      font-size: 12px;
      padding: 8px;
    }
    #DiagnosticsLog {
      font-size: 10px;
      padding: 6px;
    }
    #LightPlainView {
      border-radius: 7px;
      border: 1px solid rgba(70, 86, 53, 0.18);
      background: rgba(255, 255, 255, 0.82);
      color: #182116;
      font-family: "JetBrains Mono", "SFMono-Regular", monospace;
      font-size: 11px;
      padding: 6px;
    }
    #CommandPreview {
      font-size: 14px;
      font-weight: 700;
    }
    QProgressBar {
      min-height: 18px;
      border-radius: 7px;
      border: 1px solid rgba(74, 90, 58, 0.20);
      background: rgba(255, 255, 255, 0.88);
      color: #1b2318;
      text-align: center;
      font-size: 11px;
      font-weight: 800;
      padding: 1px;
    }
    QProgressBar::chunk {
      border-radius: 6px;
      background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #89a546, stop:1 #5b7426);
    }
    #IssueBox {
      border-radius: 6px;
      background: #f3ecc8;
      color: #755d14;
      padding: 4px 6px;
      border: 1px solid rgba(150, 128, 52, 0.15);
      font-size: 10px;
      font-weight: 700;
    }
    #IssueBox[clean="true"] {
      background: rgba(116, 155, 54, 0.16);
      color: #41601b;
      border: 1px solid rgba(83, 114, 34, 0.12);
    }
    #EmptyStateBox {
      border-radius: 8px;
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(233,238,224,0.96), stop:1 rgba(244,247,239,0.96));
    }
    #EmptyStateTitle {
      color: #55621d;
      font-size: 12px;
      font-weight: 800;
    }
    #EmptyStateBody {
      color: #66725f;
      font-size: 10px;
      font-weight: 700;
    }
    #StatTile {
      border-radius: 7px;
      background: rgba(251, 252, 248, 0.94);
    }
    #StatTileTitle {
      font-size: 10px;
      color: #66725f;
      font-weight: 700;
    }
    #StatTileValue {
      font-size: 12px;
      color: #1b2318;
      font-weight: 800;
    }
    QTableWidget {
      border-radius: 7px;
      border: 1px solid rgba(70, 86, 53, 0.18);
      background: rgba(255, 255, 255, 0.76);
      gridline-color: rgba(100, 112, 86, 0.18);
      selection-background-color: rgba(216, 228, 189, 0.55);
      selection-color: #172114;
      font-size: 10px;
    }
    QHeaderView::section {
      background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(222,230,208,0.95), stop:1 rgba(234,239,226,0.95));
      color: #5f6b58;
      padding: 4px 5px;
      border: 0;
      border-bottom: 1px solid rgba(100, 112, 86, 0.18);
      font-size: 9px;
      font-weight: 800;
    }
    QCheckBox {
      spacing: 0;
    }
    QCheckBox::indicator {
      width: 16px;
      height: 16px;
    }
    QTabWidget::pane {
      border: 0;
      background: transparent;
    }
    QScrollArea {
      border: 0;
      background: transparent;
    }
    QScrollArea > QWidget > QWidget {
      background: transparent;
    }
    QTabBar::tab {
      border-radius: 7px;
      padding: 5px 9px;
      margin-right: 4px;
      background: transparent;
      color: #5b6754;
      font-weight: 800;
      font-size: 11px;
    }
    QTabBar::tab:selected {
      background: rgba(219, 230, 187, 0.9);
      color: #4f6521;
    }
    #MutedNote {
      color: #64705d;
      font-size: 10px;
      font-weight: 700;
    }
  )");
}

struct ProgressStage {
  const char* needle;
  int percent;
};

int ClampProgressValue(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}

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

QString ShellQuote(const QString& text) {
  QString quoted;
  quoted.reserve(text.size() + 8);
  quoted += '\'';
  for (const QChar ch : text) {
    if (ch == '\'') {
      quoted += QStringLiteral("'\\''");
    } else {
      quoted += ch;
    }
  }
  quoted += '\'';
  return quoted;
}

QString BuildScriptCommand(const std::vector<std::string>& argv) {
  QStringList parts;
  for (const auto& arg : argv) {
    parts << ShellQuote(Q(arg));
  }
  return parts.join(' ');
}

QString SanitizeProcessOutput(const QString& raw_text) {
  static const QRegularExpression ansi_pattern(QStringLiteral(R"(\x1B\[[0-9;?]*[ -/]*[@-~])"));
  QString text = raw_text;
  text.replace(ansi_pattern, QString());
  text.remove(QChar('\x07'));
  return text;
}

int FindProcessOutputBoundary(const QString& text) {
  for (int index = 0; index < text.size(); ++index) {
    const QChar ch = text.at(index);
    if (ch == QChar('\n') || ch == QChar('\r')) {
      return index;
    }
  }
  return -1;
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

struct GuideCallout {
  QRect rect;
  QPoint badge_center;
  QString index;
};

enum class GuideBadgeAnchor {
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
  LeftCenter,
  RightCenter,
};

class NativeMainWindow : public QMainWindow {
 public:
  explicit NativeMainWindow(StudioBackend& backend) : backend_(backend) {
    setWindowTitle(Q("RK Firmware Studio"));
    resize(1500, 860);
    setMinimumSize(1280, 720);
    BuildUi();
    ResetTaskProgressUi();
    SwitchNavPage(0);
    UpdateModeLayout();
    ApplyStyle();
    RefreshAll();

    auto* timer = new QTimer(this);
    timer->setInterval(5000);
    connect(timer, &QTimer::timeout, this, [this]() {
      if (!running_task_.has_value()) {
        RefreshRuntimePanels(false);
      }
    });
    timer->start();

  }

  void ActivateNavPage(int index) {
    SwitchNavPage(index);
  }

  std::vector<GuideCallout> BuildOverviewGuideCallouts() const {
    return {
        MakeGuideCallout(WindowRectFor(top_bar_widget_, QMargins(4, 4, 4, 4)), Q("1"), GuideBadgeAnchor::TopRight),
        MakeGuideCallout(WindowRectFor(nav_bar_widget_, QMargins(4, 4, 4, 4)), Q("2"), GuideBadgeAnchor::TopRight),
        MakeGuideCallout(WindowRectFor(mode_bar_widget_, QMargins(4, 4, 4, 4)), Q("3"), GuideBadgeAnchor::TopRight),
        MakeGuideCallout(WindowRectFor(workspace_panel_widget_, QMargins(4, 4, 4, 4)), Q("4"), GuideBadgeAnchor::BottomRight),
        MakeGuideCallout(WindowRectFor(side_panel_widget_, QMargins(4, 4, 4, 4)), Q("5"), GuideBadgeAnchor::BottomRight),
    };
  }

  std::vector<GuideCallout> BuildBurnGuideCallouts() const {
    return {
        MakeGuideCallout(WindowRectFor(mode_bar_widget_, QMargins(4, 4, 4, 4)), Q("1"), GuideBadgeAnchor::TopLeft, QPoint(20, 0)),
        MakeGuideCallout(CombineWindowRects({context_panel_widget_, center_panel_widget_}, QMargins(4, 4, 4, 4)), Q("2"),
                         GuideBadgeAnchor::TopRight),
        MakeGuideCallout(WindowRectFor(save_parameter_button_, QMargins(12, 8, 12, 8)), Q("3"), GuideBadgeAnchor::LeftCenter,
                         QPoint(-18, 0)),
        MakeGuideCallout(WindowRectFor(state_legend_widget_, QMargins(4, 4, 4, 4)), Q("4"), GuideBadgeAnchor::TopRight),
        MakeGuideCallout(WindowRectFor(side_panel_widget_, QMargins(4, 4, 4, 4)), Q("5"), GuideBadgeAnchor::BottomRight),
    };
  }

  std::vector<GuideCallout> BuildPackGuideCallouts() const {
    return {
        MakeGuideCallout(WindowRectFor(pack_generate_card_, QMargins(4, 4, 4, 4)), Q("1"), GuideBadgeAnchor::TopRight),
        MakeGuideCallout(CombineWindowRects({pack_project_input_, pack_output_input_}, QMargins(14, 10, 96, 10)), Q("2"),
                         GuideBadgeAnchor::RightCenter),
        MakeGuideCallout(CombineWindowRects({pack_profile_combo_, pack_chip_code_input_, pack_os_combo_, pack_storage_combo_, pack_build_button_},
                                            QMargins(14, 10, 14, 10)),
                         Q("3"), GuideBadgeAnchor::RightCenter),
        MakeGuideCallout(WindowRectFor(pack_tools_card_, QMargins(4, 4, 4, 4)), Q("4"), GuideBadgeAnchor::BottomRight),
        MakeGuideCallout(WindowRectFor(pack_preview_card_, QMargins(4, 4, 4, 4)), Q("5"), GuideBadgeAnchor::BottomRight),
    };
  }

  std::vector<GuideCallout> BuildDeviceGuideCallouts() const {
    return {
        MakeGuideCallout(WindowRectFor(device_status_card_, QMargins(4, 4, 4, 4)), Q("1"), GuideBadgeAnchor::TopRight),
        MakeGuideCallout(WindowRectFor(device_page_adb_to_loader_button_, QMargins(10, 8, 10, 8)), Q("2"), GuideBadgeAnchor::TopLeft,
                         QPoint(-14, -2)),
        MakeGuideCallout(CombineWindowRects({device_page_list_devices_button_, device_page_read_partitions_button_}, QMargins(10, 8, 10, 8)),
                         Q("3"), GuideBadgeAnchor::TopRight, QPoint(12, -2)),
        MakeGuideCallout(WindowRectFor(device_diag_card_, QMargins(4, 4, 4, 4)), Q("4"), GuideBadgeAnchor::BottomRight),
    };
  }

 private:
  StudioBackend& backend_;
  QString active_mode_ = QStringLiteral("DI");
  int active_nav_page_ = 0;
  std::optional<ProjectContext> context_;
  std::optional<ParameterFileModel> current_parameter_model_;
  std::vector<PartitionRow> current_rows_;
  std::vector<PartitionValidationIssue> current_issues_;
  std::vector<PartitionValidationIssue> baseline_issues_;
  std::map<std::string, std::string> previews_;

  QWidget* top_bar_widget_ = nullptr;
  QWidget* nav_bar_widget_ = nullptr;
  QWidget* state_legend_widget_ = nullptr;
  QWidget* mode_bar_widget_ = nullptr;
  QStackedWidget* workspace_stack_ = nullptr;
  QWidget* workspace_panel_widget_ = nullptr;
  QWidget* side_panel_widget_ = nullptr;
  std::vector<QPushButton*> nav_buttons_;

  QLineEdit* project_input_ = nullptr;
  QLineEdit* parameter_input_ = nullptr;
  QLineEdit* loader_input_ = nullptr;
  QLineEdit* update_input_ = nullptr;
  QLineEdit* global_args_input_ = nullptr;
  bool parameter_override_enabled_ = false;
  bool loader_override_enabled_ = false;
  bool update_override_enabled_ = false;

  QLabel* status_device_value_ = nullptr;
  QLabel* status_project_value_ = nullptr;
  QLabel* status_template_value_ = nullptr;
  QLabel* status_profile_value_ = nullptr;
  QPushButton* top_adb_to_loader_button_ = nullptr;
  QLabel* mode_bar_hint_ = nullptr;

  QLabel* summary_mode_value_ = nullptr;
  QLabel* summary_project_state_value_ = nullptr;
  QLabel* summary_partition_value_ = nullptr;
  QLabel* summary_issue_value_ = nullptr;

  QLabel* partition_count_chip_ = nullptr;
  QLabel* mtdparts_chip_ = nullptr;
  QLabel* table_summary_ = nullptr;
  QLabel* empty_table_note_ = nullptr;
  QLabel* issue_box_ = nullptr;
  QLabel* center_title_label_ = nullptr;
  QLabel* center_hint_label_ = nullptr;
  QLabel* action_mode_body_label_ = nullptr;
  QWidget* mode_info_container_ = nullptr;
  QWidget* mode_empty_space_ = nullptr;
  QPushButton* save_parameter_button_ = nullptr;
  QPushButton* mode_execute_button_ = nullptr;
  QWidget* partition_editor_container_ = nullptr;

  QLabel* device_primary_value_ = nullptr;
  QLabel* device_updated_value_ = nullptr;
  QLabel* device_detail_value_ = nullptr;
  QLabel* device_error_value_ = nullptr;
  QLabel* side_queue_value_ = nullptr;
  QLabel* side_partition_value_ = nullptr;
  QLabel* side_issue_value_ = nullptr;
  QLabel* task_progress_title_label_ = nullptr;
  QLabel* task_progress_status_label_ = nullptr;
  QLabel* task_step_title_label_ = nullptr;
  QProgressBar* task_progress_bar_ = nullptr;
  QProgressBar* step_progress_bar_ = nullptr;

  QPlainTextEdit* command_preview_ = nullptr;
  QPlainTextEdit* operation_log_ = nullptr;
  QPlainTextEdit* diagnostics_output_ = nullptr;
  QTableWidget* partition_table_ = nullptr;
  QTabWidget* side_tabs_ = nullptr;
  std::map<QString, QLabel*> top_state_badges_;

  std::map<QString, QPushButton*> mode_buttons_;
  QWidget* project_field_box_ = nullptr;
  QWidget* parameter_field_box_ = nullptr;
  QWidget* loader_field_box_ = nullptr;
  QWidget* update_field_box_ = nullptr;
  QWidget* global_args_field_box_ = nullptr;
  QLabel* context_title_label_ = nullptr;
  QLabel* context_note_label_ = nullptr;
  QWidget* context_panel_widget_ = nullptr;
  QWidget* center_panel_widget_ = nullptr;
  QProcess* task_process_ = nullptr;
  std::optional<CommandTask> running_task_;
  std::optional<DeviceStateSnapshot> device_snapshot_;
  QString progress_task_title_;
  QString progress_task_status_;
  QString progress_step_title_;
  QString running_command_code_;
  QString task_output_tail_;
  QString process_output_buffer_;
  QString replaceable_progress_log_key_;
  QString current_progress_target_label_;
  QStringList operation_log_lines_;
  QString cached_diagnostics_text_;
  int running_task_percent_ = 0;
  int running_step_percent_ = 0;
  int running_stage_index_ = -1;
  int running_step_index_ = -1;
  int operation_log_rendered_lines_ = 0;
  bool has_replaceable_progress_log_line_ = false;
  bool replaceable_progress_log_from_carriage_return_ = false;
  QPushButton* running_task_button_ = nullptr;
  std::chrono::steady_clock::time_point last_diagnostics_refresh_ = std::chrono::steady_clock::time_point::min();

  QLineEdit* pack_project_input_ = nullptr;
  QComboBox* pack_profile_combo_ = nullptr;
  QLineEdit* pack_chip_code_input_ = nullptr;
  QComboBox* pack_os_combo_ = nullptr;
  QComboBox* pack_storage_combo_ = nullptr;
  QLineEdit* pack_output_input_ = nullptr;
  QLabel* pack_status_label_ = nullptr;
  QPlainTextEdit* package_preview_view_ = nullptr;
  QWidget* pack_generate_card_ = nullptr;
  QWidget* pack_preview_card_ = nullptr;
  QWidget* pack_tools_card_ = nullptr;
  QPushButton* pack_build_button_ = nullptr;
  QLineEdit* unpack_input_pack_ = nullptr;
  QLineEdit* unpack_output_pack_ = nullptr;
  QPlainTextEdit* merge_inputs_pack_ = nullptr;
  QLineEdit* merge_output_pack_ = nullptr;
  QLineEdit* unmerge_input_pack_ = nullptr;
  QLineEdit* unmerge_output_pack_ = nullptr;

  QLabel* device_page_primary_label_ = nullptr;
  QLabel* device_page_detail_label_ = nullptr;
  QPlainTextEdit* device_page_diagnostics_view_ = nullptr;
  QWidget* device_status_card_ = nullptr;
  QWidget* device_diag_card_ = nullptr;
  QPushButton* device_page_adb_to_loader_button_ = nullptr;
  QPushButton* device_page_list_devices_button_ = nullptr;
  QPushButton* device_page_read_partitions_button_ = nullptr;

  QPlainTextEdit* advanced_overview_view_ = nullptr;
  QLabel* help_local_docs_label_ = nullptr;

  void BuildUi() {
    auto* root = new QWidget();
    root->setObjectName("RootWidget");
    auto* root_layout = new QVBoxLayout(root);
    root_layout->setContentsMargins(8, 8, 8, 8);
    root_layout->setSpacing(6);

    top_bar_widget_ = BuildTopBar();
    root_layout->addWidget(top_bar_widget_);
    nav_bar_widget_ = BuildNavBar();
    root_layout->addWidget(nav_bar_widget_);
    mode_bar_widget_ = BuildModeBar();
    root_layout->addWidget(mode_bar_widget_);

    auto* splitter = new QSplitter(Qt::Horizontal);
    workspace_stack_ = new QStackedWidget();
    workspace_panel_widget_ = BuildWorkspacePanel();
    workspace_stack_->addWidget(workspace_panel_widget_);
    workspace_stack_->addWidget(BuildPackPage());
    workspace_stack_->addWidget(BuildDevicePage());
    workspace_stack_->addWidget(BuildHelpPage());
    splitter->addWidget(workspace_stack_);
    side_panel_widget_ = BuildSidePanel();
    splitter->addWidget(side_panel_widget_);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({1120, 360});
    root_layout->addWidget(splitter, 1);

    setCentralWidget(root);
  }

  QWidget* BuildWorkspacePanel() {
    auto* card = new QFrame();
    card->setObjectName("PanelCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(0);

    auto* workspace_splitter = new QSplitter(Qt::Vertical);
    workspace_splitter->setHandleWidth(4);
    workspace_splitter->setChildrenCollapsible(false);
    context_panel_widget_ = BuildContextPanel();
    context_panel_widget_->setMinimumHeight(112);
    workspace_splitter->addWidget(context_panel_widget_);
    center_panel_widget_ = BuildCenterPanel();
    workspace_splitter->addWidget(center_panel_widget_);
    workspace_splitter->setStretchFactor(0, 0);
    workspace_splitter->setStretchFactor(1, 1);
    workspace_splitter->setSizes({124, 752});
    layout->addWidget(workspace_splitter, 1);
    return card;
  }

  QWidget* BuildModeBar() {
    auto* frame = new QFrame();
    frame->setObjectName("SummaryBand");
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(6, 5, 6, 5);
    layout->setSpacing(5);

    const std::vector<std::pair<QString, QString>> modes = {
        {Q("UF"), Q("整包烧录")},
        {Q("DB"), Q("下载 Boot")},
        {Q("UL"), Q("烧录 Loader")},
        {Q("EF"), Q("擦除 Flash")},
        {Q("DI"), Q("分区烧录")},
    };
    for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
      auto* button = new QPushButton(modes[static_cast<std::size_t>(i)].first + "\n" + modes[static_cast<std::size_t>(i)].second);
      button->setObjectName("ModeButton");
      button->setProperty("active", modes[static_cast<std::size_t>(i)].first == active_mode_);
      button->setMinimumHeight(42);
      button->setIcon(QIcon(ModeAssetPath(modes[static_cast<std::size_t>(i)].first)));
      button->setIconSize(QSize(14, 14));
      layout->addWidget(button);
      mode_buttons_[modes[static_cast<std::size_t>(i)].first] = button;
      connect(button, &QPushButton::clicked, this, [this, code = modes[static_cast<std::size_t>(i)].first]() {
        active_mode_ = code;
        UpdateModeButtons();
        UpdateModeLayout();
        RefreshProjectPanels();
      });
    }

    auto* hint_frame = new QFrame();
    hint_frame->setObjectName("SummaryItem");
    auto* hint_layout = new QVBoxLayout(hint_frame);
    hint_layout->setContentsMargins(8, 5, 8, 5);
    hint_layout->setSpacing(1);
    auto* top = new QLabel(Q("当前功能"));
    top->setObjectName("SummaryTitle");
    mode_bar_hint_ = new QLabel(Q("分区烧录: 从参数表和分区镜像表执行 DI。"));
    mode_bar_hint_->setObjectName("SummaryValue");
    mode_bar_hint_->setWordWrap(true);
    hint_layout->addWidget(top);
    hint_layout->addWidget(mode_bar_hint_);
    layout->addWidget(hint_frame, 1);
    return frame;
  }

  QWidget* BuildTopBar() {
    auto* frame = new QFrame();
    frame->setObjectName("TopBar");
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(6, 5, 6, 5);
    layout->setSpacing(6);

    auto* brand_layout = new QHBoxLayout();
    brand_layout->setSpacing(8);
    auto* brand_mark = new QLabel(Q("RK"));
    brand_mark->setObjectName("BrandMark");
    brand_mark->setAlignment(Qt::AlignCenter);
    brand_layout->addWidget(brand_mark);

    auto* brand_text_layout = new QVBoxLayout();
    brand_text_layout->setContentsMargins(0, 0, 0, 0);
    brand_text_layout->setSpacing(1);
    auto* title = new QLabel(Q("RK Firmware Studio"));
    title->setObjectName("BrandTitle");
    auto* subtitle = new QLabel(Q("作者: forest"));
    subtitle->setObjectName("BrandSubtitle");
    brand_text_layout->addWidget(title);
    brand_text_layout->addWidget(subtitle);
    brand_layout->addLayout(brand_text_layout);
    brand_layout->addStretch(1);
    layout->addLayout(brand_layout, 0);

    auto* state_legend = new QFrame();
    state_legend_widget_ = state_legend;
    state_legend->setObjectName("StateLegend");
    auto* state_legend_layout = new QHBoxLayout(state_legend);
    state_legend_layout->setContentsMargins(7, 4, 7, 4);
    state_legend_layout->setSpacing(4);
    auto* legend_title = new QLabel(Q("全局设备状态"));
    legend_title->setObjectName("StateLegendTitle");
    state_legend_layout->addWidget(legend_title);
    const QList<QString> states = {Q("未连接"), Q("maskrom"), Q("loader"), Q("adb")};
    for (const auto& state : states) {
      auto* badge = new QLabel(state);
      badge->setObjectName("StateBadge");
      badge->setProperty("active", state == Q("未连接"));
      badge->setAlignment(Qt::AlignCenter);
      state_legend_layout->addWidget(badge);
      top_state_badges_[state] = badge;
    }
    layout->addWidget(state_legend, 1);

    auto* status_strip = new QFrame();
    status_strip->setObjectName("TopStatusStrip");
    auto* status_layout = new QHBoxLayout(status_strip);
    status_layout->setContentsMargins(4, 3, 4, 3);
    status_layout->setSpacing(4);
    auto add_status = [&](const QString& label, QLabel** value_slot) {
      auto* pill = new QFrame();
      pill->setObjectName("StatusPill");
      auto* pill_layout = new QVBoxLayout(pill);
      pill_layout->setContentsMargins(7, 4, 7, 4);
      pill_layout->setSpacing(1);
      auto* top = new QLabel(label);
      top->setObjectName("StatusLabel");
      auto* value = new QLabel(Q("-"));
      value->setObjectName("StatusValue");
      value->setWordWrap(true);
      value->setMaximumWidth(82);
      pill_layout->addWidget(top);
      pill_layout->addWidget(value);
      status_layout->addWidget(pill);
      *value_slot = value;
    };
    add_status(Q("设备状态"), &status_device_value_);
    add_status(Q("工程目录"), &status_project_value_);
    add_status(Q("模板数量"), &status_template_value_);
    add_status(Q("芯片配置"), &status_profile_value_);
    status_layout->addStretch(1);
    top_adb_to_loader_button_ = new QPushButton(Q("ADB 切到 Loader"));
    top_adb_to_loader_button_->setObjectName("ActionButton");
    top_adb_to_loader_button_->setEnabled(false);
    top_adb_to_loader_button_->setMinimumWidth(140);
    top_adb_to_loader_button_->setMinimumHeight(30);
    top_adb_to_loader_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top_adb_to_loader_button_->setToolTip(Q("设备处于 adb 状态时，发送 adb reboot bootloader。"));
    status_layout->addWidget(top_adb_to_loader_button_);
    connect(top_adb_to_loader_button_, &QPushButton::clicked, this, [this]() { TriggerAdbToLoader(); });
    layout->addWidget(status_strip, 0);
    return frame;
  }

  QWidget* BuildNavBar() {
    auto* frame = new QFrame();
    frame->setObjectName("NavBar");
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    const QStringList names = {Q("烧录中心"), Q("打包解包"), Q("设备信息"), Q("帮助说明")};
    const QStringList icons = {
        AssetPath("rkstudio/assets/nav-burn.svg"),
        AssetPath("rkstudio/assets/nav-pack.svg"),
        AssetPath("rkstudio/assets/nav-info.svg"),
        AssetPath("rkstudio/assets/nav-help.svg"),
    };
    for (int i = 0; i < names.size(); ++i) {
      auto* button = new QPushButton(names[i]);
      button->setObjectName("NavButton");
      button->setProperty("active", i == 0);
      button->setIcon(QIcon(icons[i]));
      button->setIconSize(QSize(16, 16));
      layout->addWidget(button);
      nav_buttons_.push_back(button);
      connect(button, &QPushButton::clicked, this, [this, i]() { SwitchNavPage(i); });
    }
    layout->addStretch(1);
    return frame;
  }

  QWidget* BuildPackPage() {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    QVBoxLayout* pack_layout = nullptr;
    auto* pack_card = BuildCard(Q("生成 update.img"), &pack_layout);
    pack_generate_card_ = pack_card;
    auto* pack_grid = new QGridLayout();
    pack_grid->setHorizontalSpacing(8);
    pack_grid->setVerticalSpacing(6);
    pack_grid->setColumnStretch(1, 1);
    pack_grid->setColumnStretch(3, 1);

    pack_project_input_ = new QLineEdit();
    pack_project_input_->setPlaceholderText(Q("选择封包工程目录"));
    pack_grid->addWidget(new QLabel(Q("工程目录")), 0, 0);
    pack_grid->addWidget(pack_project_input_, 0, 1, 1, 2);
    auto* pack_project_browse = new QPushButton(Q("浏览"));
    pack_project_browse->setObjectName("BrowseButton");
    pack_grid->addWidget(pack_project_browse, 0, 3);
    connect(pack_project_browse, &QPushButton::clicked, this, [this]() {
      const QString selected = QFileDialog::getExistingDirectory(
          this, Q("选择封包工程目录"), pack_project_input_ == nullptr ? QString() : pack_project_input_->text().trimmed());
      if (!selected.isEmpty()) {
        pack_project_input_->setText(selected);
        RefreshPackPage();
      }
    });
    connect(pack_project_input_, &QLineEdit::editingFinished, this, [this]() { RefreshPackPage(); });

    pack_profile_combo_ = new QComboBox();
    for (const auto& profile : backend_.pack_profiles()) {
      const int index = pack_profile_combo_->count();
      pack_profile_combo_->addItem(Q(profile.display_name()));
      pack_profile_combo_->setItemData(index, Q(profile.chip_code), Qt::UserRole);
      pack_profile_combo_->setItemData(index, Q(profile.default_os_type), Qt::UserRole + 1);
      pack_profile_combo_->setItemData(index, Q(profile.name), Qt::UserRole + 2);
    }
    pack_chip_code_input_ = new QLineEdit();
    pack_os_combo_ = new QComboBox();
    pack_os_combo_->addItems({Q("ANDROIDOS"), Q("RKOS")});
    pack_storage_combo_ = new QComboBox();
    pack_storage_combo_->addItems(
        {Q(""), Q("FLASH"), Q("EMMC"), Q("SPINOR"), Q("SPINAND"), Q("SD"), Q("SATA"), Q("PCIE"), Q("UFS"), Q("RVD")});

    pack_grid->addWidget(new QLabel(Q("芯片配置")), 1, 0);
    pack_grid->addWidget(pack_profile_combo_, 1, 1);
    pack_grid->addWidget(new QLabel(Q("Chip Code")), 1, 2);
    pack_grid->addWidget(pack_chip_code_input_, 1, 3);
    pack_grid->addWidget(new QLabel(Q("OS Type")), 2, 0);
    pack_grid->addWidget(pack_os_combo_, 2, 1);
    pack_grid->addWidget(new QLabel(Q("封包存储")), 2, 2);
    pack_grid->addWidget(pack_storage_combo_, 2, 3);

    pack_output_input_ = new QLineEdit();
    pack_output_input_->setPlaceholderText(Q("选择 update.img 输出路径"));
    pack_grid->addWidget(new QLabel(Q("输出文件")), 3, 0);
    pack_grid->addWidget(pack_output_input_, 3, 1, 1, 2);
    auto* pack_output_browse = new QPushButton(Q("浏览"));
    pack_output_browse->setObjectName("BrowseButton");
    pack_grid->addWidget(pack_output_browse, 3, 3);
    connect(pack_output_browse, &QPushButton::clicked, this, [this]() {
      const QString current = pack_output_input_ == nullptr ? QString() : pack_output_input_->text().trimmed();
      const QString selected = QFileDialog::getSaveFileName(this, Q("选择 update.img 输出路径"), current, Q("Image (*.img);;All Files (*)"));
      if (!selected.isEmpty()) {
        pack_output_input_->setText(selected);
      }
    });

    connect(pack_profile_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
      if (index < 0) {
        return;
      }
      const QString chip = pack_profile_combo_->itemData(index, Qt::UserRole).toString();
      const QString os_type = pack_profile_combo_->itemData(index, Qt::UserRole + 1).toString();
      if (pack_chip_code_input_ != nullptr && pack_chip_code_input_->text().trimmed().isEmpty()) {
        pack_chip_code_input_->setText(chip);
      }
      if (pack_os_combo_ != nullptr) {
        const int os_index = pack_os_combo_->findText(os_type);
        if (os_index >= 0) {
          pack_os_combo_->setCurrentIndex(os_index);
        }
      }
    });
    if (pack_profile_combo_->count() > 0) {
      pack_profile_combo_->setCurrentIndex(0);
      pack_chip_code_input_->setText(pack_profile_combo_->itemData(0, Qt::UserRole).toString());
    }

    pack_layout->addLayout(pack_grid);
    auto* pack_actions = new QHBoxLayout();
    auto* pack_refresh = new QPushButton(Q("检查工程"));
    pack_refresh->setObjectName("MiniButton");
    auto* pack_build = new QPushButton(Q("生成 update.img"));
    pack_build_button_ = pack_build;
    pack_build->setObjectName("ActionButton");
    pack_actions->addWidget(pack_refresh);
    pack_actions->addWidget(pack_build);
    pack_actions->addStretch(1);
    pack_layout->addLayout(pack_actions);
    pack_status_label_ = new QLabel(Q("等待读取封包工程。"));
    pack_status_label_->setObjectName("MutedNote");
    pack_status_label_->setWordWrap(true);
    pack_layout->addWidget(pack_status_label_);
    connect(pack_refresh, &QPushButton::clicked, this, [this]() { RefreshPackPage(); });
    connect(pack_build, &QPushButton::clicked, this, [this, pack_build]() {
      try {
        const QString project_text = pack_project_input_ == nullptr ? QString() : pack_project_input_->text().trimmed();
        if (project_text.isEmpty()) {
          throw BackendError("请先选择封包工程目录。");
        }
        const auto project_path = ResolveUiPath(std::filesystem::path(project_text.toStdString()));
        const QString output_text = pack_output_input_ == nullptr ? QString() : pack_output_input_->text().trimmed();
        const auto output_path = output_text.isEmpty()
                                     ? (project_path / "update.img")
                                     : ResolveUiPath(std::filesystem::path(output_text.toStdString()), project_path);
        const CommandTask task = backend_.create_pack_task(
            project_path,
            pack_chip_code_input_ == nullptr ? std::string() : pack_chip_code_input_->text().trimmed().toStdString(),
            output_path,
            pack_os_combo_ == nullptr ? std::string("ANDROIDOS") : pack_os_combo_->currentText().trimmed().toStdString(),
            pack_storage_combo_ == nullptr ? std::string() : pack_storage_combo_->currentText().trimmed().toStdString());
        StartCommandTask(task, pack_build);
      } catch (const std::exception& error) {
        AppendOperationLog(Q(error.what()));
        if (pack_status_label_ != nullptr) {
          pack_status_label_->setText(Q(error.what()));
        }
      }
    });
    layout->addWidget(pack_card);

    QVBoxLayout* preview_layout = nullptr;
    auto* preview_card = BuildCard(Q("package-file 预览"), &preview_layout);
    pack_preview_card_ = preview_card;
    package_preview_view_ = new QPlainTextEdit();
    package_preview_view_->setObjectName("LightPlainView");
    package_preview_view_->setReadOnly(true);
    package_preview_view_->setMinimumHeight(220);
    preview_layout->addWidget(package_preview_view_);
    layout->addWidget(preview_card);

    QVBoxLayout* tools_layout = nullptr;
    auto* tools_card = BuildCard(Q("解包 / 合并 / 拆分"), &tools_layout);
    pack_tools_card_ = tools_card;
    auto* tools_grid = new QGridLayout();
    tools_grid->setHorizontalSpacing(8);
    tools_grid->setVerticalSpacing(6);
    tools_grid->setColumnStretch(1, 1);

    unpack_input_pack_ = new QLineEdit();
    unpack_output_pack_ = new QLineEdit();
    AddPathRow(tools_grid, 0, Q("解包输入"), unpack_input_pack_, [this]() {
      const QString selected = QFileDialog::getOpenFileName(
          this, Q("选择待解包 update.img"), unpack_input_pack_ == nullptr ? QString() : unpack_input_pack_->text().trimmed());
      if (!selected.isEmpty()) {
        unpack_input_pack_->setText(selected);
      }
    });
    AddPathRow(tools_grid, 1, Q("解包输出目录"), unpack_output_pack_, [this]() {
      const QString selected = QFileDialog::getExistingDirectory(
          this, Q("选择解包输出目录"), unpack_output_pack_ == nullptr ? QString() : unpack_output_pack_->text().trimmed());
      if (!selected.isEmpty()) {
        unpack_output_pack_->setText(selected);
      }
    });
    auto* unpack_button = new QPushButton(Q("解包 update.img"));
    unpack_button->setObjectName("ActionButton");
    tools_grid->addWidget(unpack_button, 2, 1, 1, 2);
    connect(unpack_button, &QPushButton::clicked, this, [this, unpack_button]() {
      try {
        const CommandTask task = backend_.create_unpack_task(
            unpack_input_pack_ == nullptr ? std::filesystem::path() : std::filesystem::path(unpack_input_pack_->text().trimmed().toStdString()),
            unpack_output_pack_ == nullptr ? std::filesystem::path() : std::filesystem::path(unpack_output_pack_->text().trimmed().toStdString()));
        StartCommandTask(task, unpack_button);
      } catch (const std::exception& error) {
        AppendOperationLog(Q(error.what()));
      }
    });

    auto* merge_title = new QLabel(Q("合并输入文件"));
    merge_title->setObjectName("FieldLabel");
    tools_grid->addWidget(merge_title, 3, 0);
    merge_inputs_pack_ = new QPlainTextEdit();
    merge_inputs_pack_->setObjectName("LightPlainView");
    merge_inputs_pack_->setMinimumHeight(100);
    tools_grid->addWidget(merge_inputs_pack_, 4, 1, 1, 2);
    auto* merge_add = new QPushButton(Q("添加输入文件"));
    merge_add->setObjectName("MiniButton");
    tools_grid->addWidget(merge_add, 5, 1, 1, 2);
    connect(merge_add, &QPushButton::clicked, this, [this]() {
      const QStringList files = QFileDialog::getOpenFileNames(
          this, Q("选择多个待合并固件"), QString(), Q("Image (*.img *.bin);;All Files (*)"));
      if (files.isEmpty() || merge_inputs_pack_ == nullptr) {
        return;
      }
      QString text = merge_inputs_pack_->toPlainText().trimmed();
      if (!text.isEmpty()) {
        text += Q("\n");
      }
      text += files.join(Q("\n"));
      merge_inputs_pack_->setPlainText(text);
    });

    merge_output_pack_ = new QLineEdit();
    AddPathRow(tools_grid, 6, Q("合并输出"), merge_output_pack_, [this]() {
      const QString selected = QFileDialog::getSaveFileName(
          this, Q("选择合并输出文件"), merge_output_pack_ == nullptr ? QString() : merge_output_pack_->text().trimmed(),
          Q("Image (*.img);;All Files (*)"));
      if (!selected.isEmpty()) {
        merge_output_pack_->setText(selected);
      }
    });
    auto* merge_button = new QPushButton(Q("执行合并"));
    merge_button->setObjectName("ActionButton");
    tools_grid->addWidget(merge_button, 7, 1, 1, 2);
    connect(merge_button, &QPushButton::clicked, this, [this, merge_button]() {
      try {
        std::vector<std::filesystem::path> inputs;
        if (merge_inputs_pack_ != nullptr) {
          const QStringList lines = merge_inputs_pack_->toPlainText().split('\n', Qt::SkipEmptyParts);
          for (const auto& line : lines) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
              inputs.emplace_back(trimmed.toStdString());
            }
          }
        }
        const CommandTask task = backend_.create_merge_task(
            merge_output_pack_ == nullptr ? std::filesystem::path() : std::filesystem::path(merge_output_pack_->text().trimmed().toStdString()),
            inputs);
        StartCommandTask(task, merge_button);
      } catch (const std::exception& error) {
        AppendOperationLog(Q(error.what()));
      }
    });

    unmerge_input_pack_ = new QLineEdit();
    unmerge_output_pack_ = new QLineEdit();
    AddPathRow(tools_grid, 8, Q("待拆分固件"), unmerge_input_pack_, [this]() {
      const QString selected = QFileDialog::getOpenFileName(
          this, Q("选择待拆分固件"), unmerge_input_pack_ == nullptr ? QString() : unmerge_input_pack_->text().trimmed(),
          Q("Image (*.img *.bin);;All Files (*)"));
      if (!selected.isEmpty()) {
        unmerge_input_pack_->setText(selected);
      }
    });
    AddPathRow(tools_grid, 9, Q("拆分输出目录"), unmerge_output_pack_, [this]() {
      const QString selected = QFileDialog::getExistingDirectory(
          this, Q("选择拆分输出目录"), unmerge_output_pack_ == nullptr ? QString() : unmerge_output_pack_->text().trimmed());
      if (!selected.isEmpty()) {
        unmerge_output_pack_->setText(selected);
      }
    });
    auto* unmerge_button = new QPushButton(Q("执行拆分"));
    unmerge_button->setObjectName("ActionButton");
    tools_grid->addWidget(unmerge_button, 10, 1, 1, 2);
    connect(unmerge_button, &QPushButton::clicked, this, [this, unmerge_button]() {
      try {
        const CommandTask task = backend_.create_unmerge_task(
            unmerge_input_pack_ == nullptr ? std::filesystem::path() : std::filesystem::path(unmerge_input_pack_->text().trimmed().toStdString()),
            unmerge_output_pack_ == nullptr ? std::filesystem::path() : std::filesystem::path(unmerge_output_pack_->text().trimmed().toStdString()));
        StartCommandTask(task, unmerge_button);
      } catch (const std::exception& error) {
        AppendOperationLog(Q(error.what()));
      }
    });

    tools_layout->addLayout(tools_grid);
    layout->addWidget(tools_card, 1);
    return WrapScrollPage(content);
  }

  QWidget* BuildDevicePage() {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    QVBoxLayout* status_layout = nullptr;
    auto* status_card = BuildCard(Q("设备状态"), &status_layout);
    device_status_card_ = status_card;
    device_page_primary_label_ = new QLabel(Q("未连接"));
    device_page_primary_label_->setObjectName("PanelTitle");
    device_page_detail_label_ = new QLabel(Q("等待检测。"));
    device_page_detail_label_->setObjectName("MutedNote");
    device_page_detail_label_->setWordWrap(true);
    status_layout->addWidget(device_page_primary_label_);
    status_layout->addWidget(device_page_detail_label_);
    auto* status_actions = new QHBoxLayout();
    auto* refresh_state = new QPushButton(Q("刷新状态"));
    refresh_state->setObjectName("MiniButton");
    device_page_adb_to_loader_button_ = new QPushButton(Q("ADB 切到 Loader"));
    device_page_adb_to_loader_button_->setObjectName("ActionButton");
    device_page_adb_to_loader_button_->setEnabled(false);
    auto* list_devices = new QPushButton(Q("列出设备"));
    device_page_list_devices_button_ = list_devices;
    list_devices->setObjectName("ActionButton");
    auto* read_partitions = new QPushButton(Q("读取分区表"));
    device_page_read_partitions_button_ = read_partitions;
    read_partitions->setObjectName("ActionButton");
    status_actions->addWidget(refresh_state);
    status_actions->addWidget(device_page_adb_to_loader_button_);
    status_actions->addWidget(list_devices);
    status_actions->addWidget(read_partitions);
    status_actions->addStretch(1);
    status_layout->addLayout(status_actions);
    connect(refresh_state, &QPushButton::clicked, this, [this]() { RefreshRuntimePanels(true); });
    connect(device_page_adb_to_loader_button_, &QPushButton::clicked, this, [this]() { TriggerAdbToLoader(); });
    connect(list_devices, &QPushButton::clicked, this, [this, list_devices]() {
      try {
        StartCommandTask(backend_.create_upgrade_task("LD", {}, CurrentGlobalArgs()), list_devices);
      } catch (const std::exception& error) {
        AppendOperationLog(Q(error.what()));
      }
    });
    connect(read_partitions, &QPushButton::clicked, this, [this, read_partitions]() {
      try {
        StartCommandTask(backend_.create_upgrade_task("PL", {}, CurrentGlobalArgs()), read_partitions);
      } catch (const std::exception& error) {
        AppendOperationLog(Q(error.what()));
      }
    });
    layout->addWidget(status_card);

    QVBoxLayout* diag_layout = nullptr;
    auto* diag_card = BuildCard(Q("环境诊断"), &diag_layout);
    device_diag_card_ = diag_card;
    auto* diag_hint = new QLabel(Q("用于定位设备枚举、权限和工具链缺失问题，只读检查，不写板。"));
    diag_hint->setObjectName("MutedNote");
    diag_hint->setWordWrap(true);
    diag_layout->addWidget(diag_hint);
    device_page_diagnostics_view_ = new QPlainTextEdit();
    device_page_diagnostics_view_->setObjectName("LightPlainView");
    device_page_diagnostics_view_->setReadOnly(true);
    device_page_diagnostics_view_->setMinimumHeight(260);
    diag_layout->addWidget(device_page_diagnostics_view_);
    layout->addWidget(diag_card, 1);
    return WrapScrollPage(content);
  }

  QWidget* BuildGuideImageFrame(const QString& asset_path, const QSize& size = QSize(980, 562)) {
    auto* frame = new QFrame();
    frame->setObjectName("GuideImageFrame");
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(0);
    auto* label = new QLabel();
    label->setObjectName("GuideImageLabel");
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumHeight(size.height());
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    const QPixmap pixmap = QIcon(asset_path).pixmap(size);
    if (pixmap.isNull()) {
      label->setText(Q("未找到引导图: ") + asset_path);
    } else {
      label->setPixmap(pixmap);
    }
    layout->addWidget(label);
    return frame;
  }

  QWidget* BuildGuideStepRow(const QString& index, const QString& title, const QString& body) {
    auto* frame = new QFrame();
    frame->setObjectName("GuideStepRow");
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* badge = new QLabel(index);
    badge->setObjectName("GuideStepBadge");
    layout->addWidget(badge, 0, Qt::AlignTop);

    auto* text_layout = new QVBoxLayout();
    text_layout->setContentsMargins(0, 0, 0, 0);
    text_layout->setSpacing(2);
    auto* title_label = new QLabel(title);
    title_label->setObjectName("GuideStepTitle");
    title_label->setWordWrap(true);
    auto* body_label = new QLabel(body);
    body_label->setObjectName("GuideStepBody");
    body_label->setWordWrap(true);
    text_layout->addWidget(title_label);
    text_layout->addWidget(body_label);
    layout->addLayout(text_layout, 1);
    return frame;
  }

  QWidget* BuildGuideModeChip(const QString& code, const QString& title, const QString& body) {
    auto* frame = new QFrame();
    frame->setObjectName("GuideModeChip");
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(9, 8, 9, 8);
    layout->setSpacing(3);
    auto* code_label = new QLabel(code);
    code_label->setObjectName("GuideModeCode");
    auto* title_label = new QLabel(title);
    title_label->setObjectName("GuideModeTitle");
    auto* body_label = new QLabel(body);
    body_label->setObjectName("GuideModeBody");
    body_label->setWordWrap(true);
    layout->addWidget(code_label);
    layout->addWidget(title_label);
    layout->addWidget(body_label);
    return frame;
  }

  QWidget* BuildAdvancedPage() {
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    QVBoxLayout* intro_layout = nullptr;
    auto* intro_card = BuildCard(Q("高级工具"), &intro_layout);
    auto* intro = new QLabel(Q("这里先放原生 C++ 版的环境概览与工具路径，后续再把完整命令树和原生命令入口迁过来。"));
    intro->setObjectName("MutedNote");
    intro->setWordWrap(true);
    intro_layout->addWidget(intro);
    layout->addWidget(intro_card);

    QVBoxLayout* overview_layout = nullptr;
    auto* overview_card = BuildCard(Q("环境概览"), &overview_layout);
    advanced_overview_view_ = new QPlainTextEdit();
    advanced_overview_view_->setObjectName("LightPlainView");
    advanced_overview_view_->setReadOnly(true);
    advanced_overview_view_->setMinimumHeight(420);
    overview_layout->addWidget(advanced_overview_view_);
    layout->addWidget(overview_card, 1);
    return WrapScrollPage(content);
  }

  QWidget* BuildHelpPage() {
    auto* content = new QWidget();
    content->setMinimumWidth(980);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);

    QVBoxLayout* top_layout = nullptr;
    auto* top_card = BuildCard(Q("图文导览"), &top_layout);
    auto* help_hint = new QLabel(
        Q("这页已经改成按截图分区的上手导览。建议从整页布局开始看，再按 烧录中心、打包解包、设备信息 三段依次理解整个软件。"));
    help_hint->setObjectName("HelpLead");
    help_hint->setWordWrap(true);
    top_layout->addWidget(help_hint);
    layout->addWidget(top_card);

    QVBoxLayout* overview_layout = nullptr;
    auto* overview_card = BuildCard(Q("01. 先看整页布局"), &overview_layout);
    auto* overview_hint = new QLabel(Q("先认清顶部状态、页面导航、模式条、中间工作区和右侧任务中心。后面所有功能都围绕这 5 个区域展开。"));
    overview_hint->setObjectName("HelpLead");
    overview_hint->setWordWrap(true);
    overview_layout->addWidget(overview_hint);
    overview_layout->addWidget(BuildGuideImageFrame(AssetPath("rkstudio/assets/guide-overview.png")));
    overview_layout->addWidget(BuildGuideStepRow(Q("01"), Q("顶部状态永远先看"),
                                                 Q("这里会告诉你设备当前是 未连接、maskrom、loader 还是 adb。只有先看对状态，后面刷写动作才不会选错。")));
    overview_layout->addWidget(BuildGuideStepRow(Q("02"), Q("导航只是切页"),
                                                 Q("烧录中心做刷写，打包解包做 update.img 相关处理，设备信息做检测和切换，帮助说明只负责引导。")));
    overview_layout->addWidget(BuildGuideStepRow(Q("03"), Q("模式条决定当前动作"),
                                                 Q("UF / DB / UL / EF / DI 五种动作在这里切换。切换后中间界面会跟着收缩，只留下真正要填的参数。")));
    overview_layout->addWidget(BuildGuideStepRow(Q("04"), Q("中间区域负责输入和表格"),
                                                 Q("DI 模式下这里会显示 parameter、分区镜像表和写回按钮；其它模式只显示当前功能必要的路径和参数。")));
    overview_layout->addWidget(BuildGuideStepRow(Q("05"), Q("右侧任务中心负责反馈"),
                                                 Q("执行后重点看总任务、当前阶段和操作日志。不要只看按钮，要盯右侧确认当前到底写到哪一步。")));
    layout->addWidget(overview_card);

    QVBoxLayout* workflow_layout = nullptr;
    auto* workflow_card = BuildCard(Q("02. 新手推荐顺序"), &workflow_layout);
    auto* workflow_hint = new QLabel(Q("如果你第一次接板，按下面的顺序走最稳，能避开大部分模式不匹配问题。"));
    workflow_hint->setObjectName("HelpLead");
    workflow_hint->setWordWrap(true);
    workflow_layout->addWidget(workflow_hint);
    workflow_layout->addWidget(BuildGuideStepRow(Q("01"), Q("先准备输入"),
                                                 Q("整包烧录准备 update.img；分区烧录准备工程目录、parameter.txt 和分区镜像；修 Loader 则准备 Loader 文件。")));
    workflow_layout->addWidget(BuildGuideStepRow(Q("02"), Q("再看设备模式"),
                                                 Q("maskrom 一般先 DB 拉到 loader；adb 可直接点 ADB 切到 Loader；loader 最适合 DI 分区烧录。")));
    workflow_layout->addWidget(BuildGuideStepRow(Q("03"), Q("只选一种动作执行"),
                                                 Q("整包就 UF，拉通信就 DB，修 Loader 就 UL，清空存储就 EF，局部升级就 DI。不要把它们混在一次任务里。")));
    workflow_layout->addWidget(BuildGuideStepRow(Q("04"), Q("执行后盯右侧日志"),
                                                 Q("当前阶段会显示正在处理的镜像或动作，日志末尾会给出更细的百分比与错误点。失败时先看这里。")));
    layout->addWidget(workflow_card);

    QVBoxLayout* burn_layout = nullptr;
    auto* burn_card = BuildCard(Q("03. 烧录中心怎么用"), &burn_layout);
    auto* burn_hint = new QLabel(Q("烧录中心是主战场。下面用 DI 场景讲结构，同时把 UF / DB / UL / EF / DI 的用途一起讲清楚。"));
    burn_hint->setObjectName("HelpLead");
    burn_hint->setWordWrap(true);
    burn_layout->addWidget(burn_hint);
    burn_layout->addWidget(BuildGuideImageFrame(AssetPath("rkstudio/assets/guide-burn.png")));
    burn_layout->addWidget(BuildGuideStepRow(Q("01"), Q("先选模式，再看界面是否收缩正确"),
                                             Q("例如 UF 只该看到 update.img；EF 只该看到擦除相关参数；DI 才会显示 parameter 和分区镜像表。")));
    burn_layout->addWidget(BuildGuideStepRow(Q("02"), Q("DI 先导入 parameter，再检查分区表"),
                                             Q("分区表会按 parameter 自动展开。每一行都能单独启停、改镜像路径、改包大小，并与 mtdparts 同步。")));
    burn_layout->addWidget(BuildGuideStepRow(Q("03"), Q("写回 parameter 是人工确认动作"),
                                             Q("你改了包大小或分区参数后，只有点 写回 parameter 才会真正回写到 parameter.txt；否则只是界面内预览。")));
    burn_layout->addWidget(BuildGuideStepRow(Q("04"), Q("执行前一定先看设备模式"),
                                             Q("DI 需要 loader；DB 需要 maskrom；UF / UL / EF 需要 Rockusb 通道；不匹配时软件会先拦截并弹窗提醒。")));
    burn_layout->addWidget(BuildGuideStepRow(Q("05"), Q("执行后右侧看镜像名和百分比"),
                                             Q("日志会显示当前正在处理的镜像名，不再只是 Write file。总任务和当前阶段一起看，能判断卡在哪一步。")));
    burn_layout->addWidget(BuildGuideStepRow(
        Q("06"), Q("修改 rootfs 大小时就改 rootfs 这一行"),
        Q("在分区镜像表里找到 rootfs，直接修改 包大小 或 mtdparts 参数，两个字段会实时互相换算；常见写法可以填 512M、1024M、0x00100000s 或 -。确认总容量没有越界后，点 写回 parameter 把结果落到 parameter.txt，后续再烧录时根目录大小就会按新的分区布局生效。")));

    auto* mode_grid = new QGridLayout();
    mode_grid->setHorizontalSpacing(8);
    mode_grid->setVerticalSpacing(8);
    mode_grid->addWidget(BuildGuideModeChip(Q("UF"), Q("整包烧录"), Q("直接写 update.img，适合量产或完整重刷。")), 0, 0);
    mode_grid->addWidget(BuildGuideModeChip(Q("DB"), Q("下载 Boot"), Q("设备在 maskrom 时先把板子拉到 loader。")), 0, 1);
    mode_grid->addWidget(BuildGuideModeChip(Q("UL"), Q("烧录 Loader"), Q("单独修复启动链或 Loader/IDB。")), 0, 2);
    mode_grid->addWidget(BuildGuideModeChip(Q("EF"), Q("擦除 Flash"), Q("破坏性动作，适合重刷前清空存储。")), 1, 0);
    mode_grid->addWidget(BuildGuideModeChip(Q("DI"), Q("分区烧录"), Q("最灵活，适合局部升级、调试、救砖。")), 1, 1, 1, 2);
    burn_layout->addLayout(mode_grid);
    layout->addWidget(burn_card);

    QVBoxLayout* pack_layout = nullptr;
    auto* pack_card = BuildCard(Q("04. 打包解包怎么用"), &pack_layout);
    auto* pack_hint = new QLabel(Q("这一页负责生成 update.img，或者把大镜像拆开、合并、再整理，不直接碰设备。"));
    pack_hint->setObjectName("HelpLead");
    pack_hint->setWordWrap(true);
    pack_layout->addWidget(pack_hint);
    pack_layout->addWidget(BuildGuideImageFrame(AssetPath("rkstudio/assets/guide-pack.png")));
    pack_layout->addWidget(BuildGuideStepRow(Q("01"), Q("先看上半区的生成卡片"),
                                             Q("这一块负责生成 update.img。工程目录里应该有 package-file 和素材镜像，输出文件决定最终整包的落点。")));
    pack_layout->addWidget(BuildGuideStepRow(Q("02"), Q("工程目录和输出路径决定输入与产物"),
                                             Q("如果目录里没有 package-file 或镜像素材，下面的预览和封包命令都不会完整；输出路径则决定 update.img 最后写到哪里。")));
    pack_layout->addWidget(BuildGuideStepRow(Q("03"), Q("芯片、OS、存储参数只影响封包命令"),
                                             Q("它们会进入 rkImageMaker。除非你明确知道目标板的要求，否则跟工程预设保持一致，然后再点 生成 update.img。")));
    pack_layout->addWidget(BuildGuideStepRow(Q("04"), Q("下半区工具卡片负责 解包 / 合并 / 拆分"),
                                             Q("手里只有 update.img 时先解包；需要把多个文件按顺序拼成一个镜像时用合并；拿到组合镜像想重新拆回目录时用拆分。")));
    pack_layout->addWidget(BuildGuideStepRow(Q("05"), Q("package-file 预览先确认清单内容"),
                                             Q("真正开始封包前，先看这里确认 package-file 是否已经被读到，能避免素材缺失或条目顺序错误。")));
    layout->addWidget(pack_card);

    QVBoxLayout* device_layout = nullptr;
    auto* device_card = BuildCard(Q("05. 设备信息怎么用"), &device_layout);
    auto* device_hint = new QLabel(Q("设备信息页不做真正刷写，主要负责检测、切换、只读查询和环境诊断。"));
    device_hint->setObjectName("HelpLead");
    device_hint->setWordWrap(true);
    device_layout->addWidget(device_hint);
    device_layout->addWidget(BuildGuideImageFrame(AssetPath("rkstudio/assets/guide-device.png")));
    device_layout->addWidget(BuildGuideStepRow(Q("01"), Q("刷新状态先确认当前主模式"),
                                               Q("这里会同步显示 adb、loader、maskrom 或未连接，还会带出更细的设备详情。")));
    device_layout->addWidget(BuildGuideStepRow(Q("02"), Q("ADB 切到 Loader 是常用桥接动作"),
                                               Q("当设备当前只能 adb 通信时，直接点按钮发送 adb reboot bootloader，不用自己再开终端。")));
    device_layout->addWidget(BuildGuideStepRow(Q("03"), Q("列出设备 / 读取分区表用于只读确认"),
                                               Q("它们适合确认 upgrade_tool 能不能正常看见设备，以及板子的分区信息能不能被工具读出来。")));
    device_layout->addWidget(BuildGuideStepRow(Q("04"), Q("环境诊断专门查权限与工具链"),
                                               Q("这里会检查 adb、lsusb、upgrade_tool、USB 节点等。遇到识别失败或权限问题，先看这一块。")));
    layout->addWidget(device_card);

    QVBoxLayout* docs_layout = nullptr;
    auto* docs_card = BuildCard(Q("06. 本地文档路径"), &docs_layout);
    auto* docs_hint = new QLabel(Q("图文导览负责上手；如果还要看原始 Markdown 或官方命令 PDF，下面是本地路径。"));
    docs_hint->setObjectName("HelpLead");
    docs_hint->setWordWrap(true);
    docs_layout->addWidget(docs_hint);
    help_local_docs_label_ = new QLabel();
    help_local_docs_label_->setObjectName("HelpPathValue");
    help_local_docs_label_->setWordWrap(true);
    help_local_docs_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    docs_layout->addWidget(help_local_docs_label_);
    layout->addWidget(docs_card);
    layout->addStretch(1);
    return WrapScrollPage(content);
  }

  QFrame* BuildCard(const QString& title, QVBoxLayout** layout_slot = nullptr) {
    auto* card = new QFrame();
    card->setObjectName("PanelCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    if (!title.isEmpty()) {
      auto* heading = new QLabel(title);
      heading->setObjectName("PanelTitle");
      layout->addWidget(heading);
    }
    if (layout_slot != nullptr) {
      *layout_slot = layout;
    }
    return card;
  }

  QWidget* WrapScrollPage(QWidget* content) {
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
  }

  void AddPathRow(
      QGridLayout* grid,
      int row,
      const QString& label,
      QLineEdit* input,
      const std::function<void()>& browse_action,
      bool align_top = false) {
    auto* title = new QLabel(label);
    title->setObjectName("FieldLabel");
    title->setAlignment(align_top ? (Qt::AlignLeft | Qt::AlignTop) : (Qt::AlignLeft | Qt::AlignVCenter));
    grid->addWidget(title, row, 0);
    grid->addWidget(input, row, 1);
    auto* browse = new QPushButton(Q("浏览"));
    browse->setObjectName("BrowseButton");
    grid->addWidget(browse, row, 2);
    connect(browse, &QPushButton::clicked, this, browse_action);
  }

  void UpdateNavButtons() {
    for (int index = 0; index < static_cast<int>(nav_buttons_.size()); ++index) {
      auto* button = nav_buttons_[static_cast<std::size_t>(index)];
      button->setProperty("active", index == active_nav_page_);
      button->style()->unpolish(button);
      button->style()->polish(button);
      button->update();
    }
  }

  void SyncAdbToLoaderButtonState() {
    const QString state = device_snapshot_.has_value() ? Q(device_snapshot_->primary_state) : Q("未连接");
    const bool enabled = state == Q("adb") && !running_task_.has_value();
    const QString tooltip =
        state == Q("adb")
            ? Q("发送 adb reboot bootloader，把当前 adb 设备切换到 loader。")
            : Q("仅当设备当前处于 adb 状态时可用。");
    if (top_adb_to_loader_button_ != nullptr) {
      top_adb_to_loader_button_->setEnabled(enabled);
      top_adb_to_loader_button_->setToolTip(tooltip);
    }
    if (device_page_adb_to_loader_button_ != nullptr) {
      device_page_adb_to_loader_button_->setEnabled(enabled);
      device_page_adb_to_loader_button_->setToolTip(tooltip);
    }
  }

  void TriggerAdbToLoader() {
    if (running_task_.has_value()) {
      const QString message = Q("当前有任务正在运行，暂时不能执行 ADB 切换 Loader。");
      AppendOperationLog(message);
      ShowTransientIssue(message);
      QMessageBox::warning(this, Q("任务正在执行"), message);
      return;
    }

    const auto snapshot = RefreshDeviceSnapshot();
    const QString state = snapshot.has_value() ? Q(snapshot->primary_state) : Q("未连接");
    if (state != Q("adb")) {
      const QString message = Q("当前设备不是 adb 状态，无法切换到 loader。当前状态: ") + state;
      AppendOperationLog(message);
      ShowTransientIssue(message);
      QMessageBox::warning(this, Q("无法切换到 Loader"), message);
      return;
    }

    try {
      const QString serial = Q(backend_.reboot_adb_device_to_loader());
      const QString message = Q("已发送 adb reboot bootloader: ") + serial + Q("，等待设备进入 loader。");
      AppendOperationLog(message);
      ShowTransientIssue(message);
      QTimer::singleShot(1200, this, [this]() { RefreshRuntimePanels(true); });
      QTimer::singleShot(2600, this, [this]() { RefreshRuntimePanels(true); });
      QTimer::singleShot(4200, this, [this]() { RefreshRuntimePanels(true); });
    } catch (const std::exception& error) {
      const QString message = Q(error.what());
      AppendOperationLog(message);
      ShowTransientIssue(message);
      QMessageBox::warning(this, Q("切换到 Loader 失败"), message);
    }
  }

  void SwitchNavPage(int index) {
    if (workspace_stack_ == nullptr) {
      return;
    }
    if (index < 0 || index >= workspace_stack_->count()) {
      return;
    }
    active_nav_page_ = index;
    workspace_stack_->setCurrentIndex(index);
    if (mode_bar_widget_ != nullptr) {
      mode_bar_widget_->setVisible(index == 0);
    }
    UpdateNavButtons();
    if (index == 1) {
      RefreshPackPage();
    } else if (index == 2) {
      RefreshRuntimePanels(true);
    } else if (index == 3) {
      RefreshHelpPage();
    }
  }

  QWidget* BuildSummaryBand() {
    auto* frame = new QFrame();
    frame->setObjectName("SummaryBand");
    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(5, 4, 5, 4);
    layout->setSpacing(4);
    auto add_item = [&](const QString& title, QLabel** value_slot, int stretch = 0) {
      auto* card = new QFrame();
      card->setObjectName("SummaryItem");
      auto* card_layout = new QVBoxLayout(card);
      card_layout->setContentsMargins(6, 4, 6, 4);
      card_layout->setSpacing(1);
      auto* top = new QLabel(title);
      top->setObjectName("SummaryTitle");
      auto* value = new QLabel(Q("-"));
      value->setObjectName("SummaryValue");
      value->setWordWrap(true);
      card_layout->addWidget(top);
      card_layout->addWidget(value);
      layout->addWidget(card, stretch);
      *value_slot = value;
    };
    add_item(Q("当前模式"), &summary_mode_value_);
    add_item(Q("工程状态"), &summary_project_state_value_);
    add_item(Q("分区摘要"), &summary_partition_value_);
    add_item(Q("最近提示"), &summary_issue_value_, 1);
    return frame;
  }

  QWidget* BuildContextPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 5, 6, 5);
    layout->setSpacing(3);

    auto* header = new QHBoxLayout();
    context_title_label_ = new QLabel(Q("功能参数"));
    context_title_label_->setObjectName("PanelTitle");
    auto* refresh = new QPushButton(Q("刷新"));
    refresh->setObjectName("ActionButton");
    header->addWidget(context_title_label_);
    header->addStretch(1);
    header->addWidget(refresh);
    layout->addLayout(header);

    auto add_field = [&](const QString& label, bool read_only, const QString& placeholder, bool show_browse,
                         bool pick_dir, QWidget** field_box_slot) -> QLineEdit* {
      auto* field_box = new QWidget();
      auto* field_box_layout = new QHBoxLayout(field_box);
      field_box_layout->setContentsMargins(0, 0, 0, 0);
      field_box_layout->setSpacing(4);
      auto* caption = new QLabel(label);
      caption->setObjectName("FieldLabel");
      caption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      caption->setFixedWidth(72);
      auto* input = new QLineEdit();
      input->setReadOnly(read_only);
      input->setPlaceholderText(placeholder);
      field_box_layout->addWidget(caption);
      field_box_layout->addWidget(input, 1);
      if (show_browse) {
        auto* browse = new QPushButton(Q("浏览"));
        browse->setObjectName("BrowseButton");
        field_box_layout->addWidget(browse, 0);
        connect(browse, &QPushButton::clicked, this, [this, input, pick_dir, label]() {
          QString selected;
          const QString start_path = input->text().trimmed();
          if (pick_dir) {
            selected = QFileDialog::getExistingDirectory(this, label, start_path);
          } else {
            selected = QFileDialog::getOpenFileName(this, label, start_path);
          }
          if (!selected.isEmpty()) {
            input->setText(selected);
            if (input == parameter_input_) {
              parameter_override_enabled_ = true;
            } else if (input == loader_input_) {
              loader_override_enabled_ = true;
            } else if (input == update_input_) {
              update_override_enabled_ = true;
            }
            RefreshProjectPanels();
          }
        });
      }
      layout->addWidget(field_box);
      *field_box_slot = field_box;
      return input;
    };
    project_input_ = add_field(Q("工程目录"), false, Q("提供对应的工程目录路径"), true, true, &project_field_box_);
    parameter_input_ = add_field(Q("parameter"), false, Q("提供对应的 parameter 文件路径"), true, false, &parameter_field_box_);
    loader_input_ = add_field(Q("Loader"), false, Q("提供对应的 Loader 文件路径"), true, false, &loader_field_box_);
    update_input_ = add_field(Q("update.img"), false, Q("提供对应的 update.img 文件路径"), true, false, &update_field_box_);
    global_args_input_ = add_field(Q("全局参数"), false, Q("提供全局命令参数，例如 -s 5"), false, false, &global_args_field_box_);
    global_args_input_->setPlaceholderText(Q("-s 5"));

    context_note_label_ = new QLabel(Q("当前模式只显示执行该功能所需的输入项。"));
    context_note_label_->setObjectName("MutedNote");
    context_note_label_->setWordWrap(false);
    layout->addWidget(context_note_label_);

    connect(refresh, &QPushButton::clicked, this, [this]() { RefreshAll(); });
    connect(project_input_, &QLineEdit::editingFinished, this, [this]() { RefreshProjectPanels(); });
    connect(parameter_input_, &QLineEdit::editingFinished, this, [this]() {
      parameter_override_enabled_ = !parameter_input_->text().trimmed().isEmpty();
      RefreshProjectPanels();
    });
    connect(loader_input_, &QLineEdit::editingFinished, this, [this]() {
      loader_override_enabled_ = !loader_input_->text().trimmed().isEmpty();
      RefreshProjectPanels();
    });
    connect(update_input_, &QLineEdit::editingFinished, this, [this]() {
      update_override_enabled_ = !update_input_->text().trimmed().isEmpty();
      RefreshProjectPanels();
    });
    connect(global_args_input_, &QLineEdit::editingFinished, this, [this]() { RefreshProjectPanels(); });
    return panel;
  }

  QWidget* BuildCenterPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 5, 6, 5);
    layout->setSpacing(3);

    auto* header = new QHBoxLayout();
    center_title_label_ = new QLabel(Q("分区镜像表"));
    center_title_label_->setObjectName("PanelTitle");
    partition_count_chip_ = new QLabel(Q("0 项"));
    partition_count_chip_->setObjectName("PanelChip");
    mtdparts_chip_ = new QLabel(Q("mtdparts"));
    mtdparts_chip_->setObjectName("PanelChip");
    mode_execute_button_ = new QPushButton(Q("执行分区烧录"));
    mode_execute_button_->setObjectName("ActionButton");
    save_parameter_button_ = new QPushButton(Q("写回 parameter"));
    save_parameter_button_->setObjectName("ActionButton");
    save_parameter_button_->setEnabled(false);
    header->addWidget(center_title_label_);
    header->addStretch(1);
    header->addWidget(partition_count_chip_);
    header->addWidget(mtdparts_chip_);
    header->addWidget(mode_execute_button_);
    header->addWidget(save_parameter_button_);
    layout->addLayout(header);

    partition_editor_container_ = new QWidget();
    auto* partition_layout = new QVBoxLayout(partition_editor_container_);
    partition_layout->setContentsMargins(0, 0, 0, 0);
    partition_layout->setSpacing(3);

    table_summary_ = new QLabel(Q("等待后端数据"));
    table_summary_->setObjectName("MutedNote");
    partition_layout->addWidget(table_summary_);

    empty_table_note_ = new QLabel(
        Q("<div style='text-align:center'>"
          "<div id='EmptyStateTitle'>当前没有可显示的分区表</div>"
          "<div id='EmptyStateBody'>请先准备 package-file、parameter.txt 和镜像素材，中心区域会自动切换为分区表。</div>"
          "</div>"));
    empty_table_note_->setObjectName("EmptyStateBox");
    empty_table_note_->setWordWrap(true);
    empty_table_note_->setAlignment(Qt::AlignCenter);
    empty_table_note_->setMinimumHeight(330);
    partition_layout->addWidget(empty_table_note_);

    partition_table_ = new QTableWidget(0, 5);
    partition_table_->setHorizontalHeaderLabels({Q("启用"), Q("名称"), Q("文件路径"), Q("包大小"), Q("mtdparts 参数")});
    partition_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    partition_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    partition_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    partition_table_->setWordWrap(false);
    partition_table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    partition_table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    partition_table_->setTextElideMode(Qt::ElideMiddle);
    partition_table_->verticalHeader()->setVisible(false);
    partition_table_->verticalHeader()->setDefaultSectionSize(28);
    partition_table_->horizontalHeader()->setStretchLastSection(false);
    partition_table_->horizontalHeader()->setMinimumSectionSize(36);
    partition_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    partition_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    partition_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    partition_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    partition_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    partition_table_->setColumnWidth(0, 52);
    partition_table_->setColumnWidth(1, 86);
    partition_table_->setColumnWidth(2, 380);
    partition_table_->setColumnWidth(3, 116);
    partition_layout->addWidget(partition_table_, 1);

    issue_box_ = new QLabel(Q("等待后端数据"));
    issue_box_->setObjectName("IssueBox");
    issue_box_->setWordWrap(true);
    issue_box_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    issue_box_->setMinimumHeight(24);
    issue_box_->setMaximumHeight(38);
    partition_layout->addWidget(issue_box_);
    layout->addWidget(partition_editor_container_, 1);

    mode_empty_space_ = new QWidget();
    mode_empty_space_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    layout->addWidget(mode_empty_space_, 1);

    mode_info_container_ = new QWidget();
    auto* mode_info_layout = new QVBoxLayout(mode_info_container_);
    mode_info_layout->setContentsMargins(0, 0, 0, 0);
    mode_info_layout->setSpacing(3);
    center_hint_label_ = new QLabel(Q("从 parameter.txt 和分区镜像表组织 DI 命令，适合精细化分区烧录。"));
    center_hint_label_->setObjectName("MutedNote");
    center_hint_label_->setWordWrap(true);
    action_mode_body_label_ =
        new QLabel(Q("切换 UF / DB / UL / EF / DI 后，仅保留当前操作所需的路径输入、执行按钮和右侧日志窗口。"));
    action_mode_body_label_->setObjectName("MutedNote");
    action_mode_body_label_->setWordWrap(true);
    mode_info_layout->addWidget(center_hint_label_);
    mode_info_layout->addWidget(action_mode_body_label_);
    layout->addWidget(mode_info_container_);

    connect(mode_execute_button_, &QPushButton::clicked, this, [this]() { ExecuteActiveMode(); });
    connect(save_parameter_button_, &QPushButton::clicked, this, [this]() { SaveParameterFile(); });
    return panel;
  }

  QWidget* BuildSidePanel() {
    auto* card = new QFrame();
    card->setObjectName("PanelCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel(Q("任务中心"));
    title->setObjectName("PanelTitle");
    header->addWidget(title);
    header->addStretch(1);
    layout->addLayout(header);

    auto* stats_layout = new QHBoxLayout();
    stats_layout->setSpacing(4);
    auto add_stat_tile = [&](const QString& title_text, QLabel** value_slot) {
      auto* tile = new QFrame();
      tile->setObjectName("StatTile");
      auto* tile_layout = new QVBoxLayout(tile);
      tile_layout->setContentsMargins(6, 4, 6, 4);
      tile_layout->setSpacing(1);
      auto* title_label = new QLabel(title_text);
      title_label->setObjectName("StatTileTitle");
      auto* value = new QLabel(Q("-"));
      value->setObjectName("StatTileValue");
      tile_layout->addWidget(title_label);
      tile_layout->addWidget(value);
      stats_layout->addWidget(tile);
      *value_slot = value;
    };
    add_stat_tile(Q("待处理"), &side_queue_value_);
    add_stat_tile(Q("分区数"), &side_partition_value_);
    add_stat_tile(Q("告警"), &side_issue_value_);
    layout->addLayout(stats_layout);

    auto* progress_frame = new QFrame();
    progress_frame->setObjectName("SummaryBand");
    auto* progress_layout = new QVBoxLayout(progress_frame);
    progress_layout->setContentsMargins(6, 6, 6, 6);
    progress_layout->setSpacing(4);
    auto* progress_title = new QLabel(Q("执行进度"));
    progress_title->setObjectName("PanelTitle");
    progress_layout->addWidget(progress_title);
    task_progress_title_label_ = new QLabel(Q("总任务: 空闲"));
    task_progress_title_label_->setObjectName("FieldLabel");
    task_progress_title_label_->setWordWrap(true);
    progress_layout->addWidget(task_progress_title_label_);
    task_progress_bar_ = new QProgressBar();
    task_progress_bar_->setRange(0, 100);
    task_progress_bar_->setTextVisible(true);
    progress_layout->addWidget(task_progress_bar_);
    task_step_title_label_ = new QLabel(Q("当前阶段: 等待任务"));
    task_step_title_label_->setObjectName("FieldLabel");
    task_step_title_label_->setWordWrap(true);
    progress_layout->addWidget(task_step_title_label_);
    step_progress_bar_ = new QProgressBar();
    step_progress_bar_->setRange(0, 100);
    step_progress_bar_->setTextVisible(true);
    progress_layout->addWidget(step_progress_bar_);
    task_progress_status_label_ = new QLabel(Q("未执行任务"));
    task_progress_status_label_->setObjectName("MutedNote");
    task_progress_status_label_->setWordWrap(true);
    progress_layout->addWidget(task_progress_status_label_);
    layout->addWidget(progress_frame);

    side_tabs_ = new QTabWidget();
    auto* logs_page = new QWidget();
    auto* logs_layout = new QVBoxLayout(logs_page);
    logs_layout->setContentsMargins(0, 2, 0, 0);
    logs_layout->setSpacing(3);
    auto* logs_header = new QHBoxLayout();
    auto* logs_title = new QLabel(Q("操作日志"));
    logs_title->setObjectName("PanelTitle");
    auto* clear_logs = new QPushButton(Q("清空日志"));
    clear_logs->setObjectName("MiniButton");
    logs_header->addWidget(logs_title);
    logs_header->addStretch(1);
    logs_header->addWidget(clear_logs);
    operation_log_ = new QPlainTextEdit();
    operation_log_->setObjectName("OperationLog");
    operation_log_->setReadOnly(true);
    logs_layout->addLayout(logs_header);
    logs_layout->addWidget(operation_log_, 1);
    connect(clear_logs, &QPushButton::clicked, this, [this]() {
      operation_log_lines_.clear();
      operation_log_rendered_lines_ = 0;
      if (operation_log_ != nullptr) {
        operation_log_->clear();
      }
    });

    auto* state_page = new QWidget();
    auto* state_layout = new QVBoxLayout(state_page);
    state_layout->setContentsMargins(0, 2, 0, 0);
    state_layout->setSpacing(4);
    auto add_state_card = [&](const QString& title_text, QLabel** value_slot) {
      auto* frame = new QFrame();
      frame->setObjectName("PanelCard");
      auto* frame_layout = new QVBoxLayout(frame);
      frame_layout->setContentsMargins(6, 4, 6, 4);
      frame_layout->setSpacing(1);
      auto* title_label = new QLabel(title_text);
      title_label->setObjectName("FieldLabel");
      auto* value = new QLabel(Q("-"));
      value->setWordWrap(true);
      value->setObjectName("SummaryValue");
      frame_layout->addWidget(title_label);
      frame_layout->addWidget(value);
      state_layout->addWidget(frame);
      *value_slot = value;
    };
    add_state_card(Q("主状态"), &device_primary_value_);
    add_state_card(Q("更新时间"), &device_updated_value_);
    add_state_card(Q("状态摘要"), &device_detail_value_);
    add_state_card(Q("异常"), &device_error_value_);
    auto* diagnostics_title = new QLabel(Q("环境诊断"));
    diagnostics_title->setObjectName("PanelTitle");
    diagnostics_output_ = new QPlainTextEdit();
    diagnostics_output_->setObjectName("DiagnosticsLog");
    diagnostics_output_->setReadOnly(true);
    diagnostics_output_->setMinimumHeight(170);
    state_layout->addWidget(diagnostics_title);
    state_layout->addWidget(diagnostics_output_, 1);
    state_layout->addStretch(1);

    side_tabs_->addTab(logs_page, Q("操作日志"));
    side_tabs_->addTab(state_page, Q("消息中心"));
    layout->addWidget(side_tabs_, 1);
    return card;
  }

  void ApplyStyle() {
    qApp->setStyleSheet(BuildStyleSheet());
  }

  int TaskStepCount(const CommandTask& task) const {
    return std::max(1, static_cast<int>(task.steps.size()));
  }

  QString DetectRunningCommandCode(const CommandTask& task, const CommandStep& step) const {
    const QString task_label = Q(task.label).trimmed();
    if (task_label.startsWith(Q("upgrade_tool "))) {
      return task_label.section(' ', -1).trimmed();
    }
    const QString step_label = Q(step.label).trimmed();
    const QString code = step_label.section(' ', 0, 0).trimmed();
    if (!code.isEmpty() && code.size() <= 4 && code == code.toUpper()) {
      return code;
    }
    return QString();
  }

  void RenderTaskProgressUi() {
    if (task_progress_title_label_ != nullptr) {
      task_progress_title_label_->setText(progress_task_title_.isEmpty() ? Q("总任务: 空闲") : progress_task_title_);
    }
    if (task_step_title_label_ != nullptr) {
      task_step_title_label_->setText(progress_step_title_.isEmpty() ? Q("当前阶段: 等待任务") : progress_step_title_);
    }
    if (task_progress_status_label_ != nullptr) {
      task_progress_status_label_->setText(progress_task_status_.isEmpty() ? Q("未执行任务") : progress_task_status_);
    }
    if (task_progress_bar_ != nullptr) {
      task_progress_bar_->setValue(ClampProgressValue(running_task_percent_));
      task_progress_bar_->setFormat(QStringLiteral("%1%").arg(ClampProgressValue(running_task_percent_)));
    }
    if (step_progress_bar_ != nullptr) {
      step_progress_bar_->setValue(ClampProgressValue(running_step_percent_));
      step_progress_bar_->setFormat(QStringLiteral("%1%").arg(ClampProgressValue(running_step_percent_)));
    }
  }

  void ResetTaskProgressUi() {
    progress_task_title_ = Q("总任务: 空闲");
    progress_step_title_ = Q("当前阶段: 等待任务");
    progress_task_status_ = Q("未执行任务");
    running_command_code_.clear();
    task_output_tail_.clear();
    process_output_buffer_.clear();
    replaceable_progress_log_key_.clear();
    current_progress_target_label_.clear();
    running_task_percent_ = 0;
    running_step_percent_ = 0;
    running_stage_index_ = -1;
    has_replaceable_progress_log_line_ = false;
    replaceable_progress_log_from_carriage_return_ = false;
    RenderTaskProgressUi();
  }

  void BeginTaskProgress(const CommandTask& task) {
    progress_task_title_ = Q("总任务: ") + Q(task.label);
    progress_step_title_ = Q("当前阶段: 等待启动");
    progress_task_status_ = Q("准备启动");
    running_command_code_.clear();
    task_output_tail_.clear();
    process_output_buffer_.clear();
    replaceable_progress_log_key_.clear();
    current_progress_target_label_.clear();
    running_task_percent_ = 0;
    running_step_percent_ = 0;
    running_stage_index_ = -1;
    has_replaceable_progress_log_line_ = false;
    replaceable_progress_log_from_carriage_return_ = false;
    RenderTaskProgressUi();
  }

  void BeginTaskStepProgress(const CommandTask& task, const CommandStep& step) {
    progress_task_title_ =
        Q("总任务: ") + Q(task.label) + Q(" (") + QString::number(running_step_index_ + 1) + Q("/") + QString::number(TaskStepCount(task)) + Q(")");
    progress_step_title_ = Q("当前阶段: ") + Q(step.label);
    progress_task_status_ = Q("执行中");
    running_command_code_ = DetectRunningCommandCode(task, step);
    task_output_tail_.clear();
    process_output_buffer_.clear();
    replaceable_progress_log_key_.clear();
    current_progress_target_label_.clear();
    running_stage_index_ = -1;
    running_step_percent_ = 0;
    has_replaceable_progress_log_line_ = false;
    replaceable_progress_log_from_carriage_return_ = false;
    running_task_percent_ = ClampProgressValue(static_cast<int>(
        std::floor(100.0 * std::max(running_step_index_, 0) / static_cast<double>(TaskStepCount(task)))));
    RenderTaskProgressUi();
  }

  void ApplyRunningStepPercent(int step_percent, bool allow_reset = false) {
    const int clamped = ClampProgressValue(step_percent);
    running_step_percent_ = allow_reset ? clamped : ClampProgressValue(std::max(running_step_percent_, clamped));
    progress_task_status_ = Q("执行中");
    RenderTaskProgressUi();
  }

  void ApplyRunningTaskPercent(int task_percent) {
    running_task_percent_ = ClampProgressValue(std::max(running_task_percent_, task_percent));
    progress_task_status_ = Q("执行中");
    RenderTaskProgressUi();
  }

  std::optional<int> InferStageProgressFromOutput(const QString& text) {
    if (running_command_code_.isEmpty()) {
      return std::nullopt;
    }
    const auto& stages = UpgradeProgressStages(running_command_code_);
    std::optional<int> latest;
    for (int index = running_stage_index_ + 1; index < static_cast<int>(stages.size()); ++index) {
      if (text.contains(QString::fromUtf8(stages[static_cast<std::size_t>(index)].needle), Qt::CaseInsensitive)) {
        running_stage_index_ = index;
        latest = ClampProgressValue(stages[static_cast<std::size_t>(index)].percent);
      }
    }
    return latest;
  }

  void ReplaceLastOperationLogLine(const QString& line) {
    if (line.isEmpty()) {
      return;
    }
    if (operation_log_lines_.isEmpty()) {
      operation_log_lines_.append(line);
    } else {
      operation_log_lines_.last() = line;
    }
    ReplaceLastOperationLogLineInView(line);
  }

  void AppendProcessLogLine(const QString& line, bool from_carriage_return) {
    const auto key = ExtractProgressLogKey(line);
    bool replace_last = false;
    if (has_replaceable_progress_log_line_) {
      if (from_carriage_return || replaceable_progress_log_from_carriage_return_) {
        replace_last = true;
      } else if (key.has_value() && replaceable_progress_log_key_ == *key) {
        replace_last = true;
      }
    }
    if (replace_last) {
      ReplaceLastOperationLogLine(line);
    } else {
      AppendOperationLog(line);
    }
    has_replaceable_progress_log_line_ = from_carriage_return || key.has_value();
    replaceable_progress_log_from_carriage_return_ = from_carriage_return;
    replaceable_progress_log_key_ = key.value_or(QString());
  }

  QString ParameterProgressLabel() const {
    if (const auto parameter_path = CurrentParameterPath(); parameter_path.has_value()) {
      return ShortPathLabel(*parameter_path);
    }
    return Q("parameter.txt");
  }

  QString UpdateImageProgressLabel() const {
    if (const auto update_path = CurrentUpdateImagePath(); update_path.has_value()) {
      return ShortPathLabel(*update_path);
    }
    return Q("update.img");
  }

  QString ResolveProgressTargetLabel(const QString& token) const {
    QString cleaned = token.trimmed();
    cleaned.remove(QStringLiteral("..."));
    if (cleaned.isEmpty()) {
      return QString();
    }
    if (cleaned.compare(Q("parameter"), Qt::CaseInsensitive) == 0 ||
        cleaned.compare(Q("parameter.txt"), Qt::CaseInsensitive) == 0) {
      return ParameterProgressLabel();
    }
    if (cleaned.compare(Q("image"), Qt::CaseInsensitive) == 0 ||
        cleaned.compare(Q("update.img"), Qt::CaseInsensitive) == 0) {
      return UpdateImageProgressLabel();
    }
    for (const auto& row : current_rows_) {
      const QString row_name = Q(row.name).trimmed();
      const QString entry_name = Q(row.package_entry_name).trimmed();
      if (row_name.compare(cleaned, Qt::CaseInsensitive) == 0 || entry_name.compare(cleaned, Qt::CaseInsensitive) == 0) {
        if (!row.file_path.empty()) {
          return ShortPathLabel(std::filesystem::path(row.file_path));
        }
        if (!entry_name.isEmpty()) {
          return entry_name;
        }
        return row_name;
      }
    }
    if (cleaned.contains(QChar('/')) || cleaned.contains(QChar('\\')) || cleaned.contains(QChar('.'))) {
      return ShortPathLabel(std::filesystem::path(cleaned.toStdString()));
    }
    return cleaned;
  }

  void UpdateCurrentProgressTarget(const QString& line) {
    if (line.contains(Q("Write parameter"), Qt::CaseInsensitive)) {
      current_progress_target_label_ = ParameterProgressLabel();
      return;
    }
    if (line.contains(Q("Download Image"), Qt::CaseInsensitive) || line.contains(Q("Check Image"), Qt::CaseInsensitive)) {
      current_progress_target_label_ = UpdateImageProgressLabel();
      return;
    }
    if (const auto token = MatchProgressTargetToken(line); token.has_value()) {
      const QString resolved = ResolveProgressTargetLabel(*token);
      if (!resolved.isEmpty()) {
        current_progress_target_label_ = resolved;
      }
    }
  }

  QString FormatProgressLogLine(const QString& line) const {
    const auto displayed_percent = ExtractDisplayedProgressPercent(line);
    if (!displayed_percent.has_value()) {
      return line;
    }
    const QString percent_text = QStringLiteral(" (%1%)").arg(*displayed_percent);
    if (line.contains(Q("Write parameter"), Qt::CaseInsensitive)) {
      return ParameterProgressLabel() + percent_text;
    }
    if (line.contains(Q("Write file"), Qt::CaseInsensitive) && !current_progress_target_label_.isEmpty()) {
      return current_progress_target_label_ + percent_text;
    }
    if (line.contains(Q("Check file"), Qt::CaseInsensitive) && !current_progress_target_label_.isEmpty()) {
      return current_progress_target_label_ + Q(" 校验") + percent_text;
    }
    if (line.contains(Q("Download Image"), Qt::CaseInsensitive)) {
      return UpdateImageProgressLabel() + percent_text;
    }
    if (line.contains(Q("Check Image"), Qt::CaseInsensitive)) {
      return UpdateImageProgressLabel() + Q(" 校验") + percent_text;
    }
    return line;
  }

  void UpdateTaskProgressFromLine(const QString& line, const QString& display_line) {
    if (!running_task_.has_value()) {
      return;
    }

    if (const auto displayed_percent = ExtractDisplayedProgressPercent(line); displayed_percent.has_value()) {
      ApplyRunningStepPercent(*displayed_percent, true);
      progress_step_title_ = Q("当前阶段: ") + ExtractProgressDisplayTitle(display_line);
      if (const auto task_percent = ExtractUpgradePhasePercent(running_command_code_, line); task_percent.has_value()) {
        ApplyRunningTaskPercent(*task_percent);
      }
      return;
    }

    if (line.contains(Q(" Start"), Qt::CaseInsensitive) || line.contains(Q(" start"), Qt::CaseInsensitive) ||
        line.contains(Q("Loading loader"), Qt::CaseInsensitive)) {
      progress_step_title_ = Q("当前阶段: ") + ExtractProgressDisplayTitle(line);
      ApplyRunningStepPercent(0, true);
    }

    if (line.contains(Q(" Success"), Qt::CaseInsensitive) || line.contains(Q(" ok."), Qt::CaseInsensitive)) {
      progress_step_title_ = Q("当前阶段: ") + ExtractProgressDisplayTitle(line);
      ApplyRunningStepPercent(100, true);
    }

    if (const auto stage_percent = InferStageProgressFromOutput(line); stage_percent.has_value()) {
      ApplyRunningTaskPercent(*stage_percent);
    }
  }

  void UpdateTaskProgressFromOutput(const QString& output) {
    if (!running_task_.has_value() || output.isEmpty()) {
      return;
    }
    process_output_buffer_ += output;
    while (true) {
      const int boundary_pos = FindProcessOutputBoundary(process_output_buffer_);
      if (boundary_pos < 0) {
        break;
      }
      const bool from_carriage_return = process_output_buffer_.at(boundary_pos) == QChar('\r');
      QString line = process_output_buffer_.left(boundary_pos).trimmed();
      process_output_buffer_.remove(0, boundary_pos + 1);
      if (line.isEmpty()) {
        if (!from_carriage_return && replaceable_progress_log_from_carriage_return_) {
          has_replaceable_progress_log_line_ = false;
          replaceable_progress_log_from_carriage_return_ = false;
          replaceable_progress_log_key_.clear();
        }
        continue;
      }
      UpdateCurrentProgressTarget(line);
      const QString display_line = FormatProgressLogLine(line);
      UpdateTaskProgressFromLine(line, display_line);
      AppendProcessLogLine(display_line, from_carriage_return);
    }
  }

  void MarkTaskStepCompleted(const CommandTask& task) {
    running_step_percent_ = 100;
    running_task_percent_ = ClampProgressValue(static_cast<int>(
        std::round(100.0 * std::min(running_step_index_ + 1, TaskStepCount(task)) / static_cast<double>(TaskStepCount(task)))));
    progress_task_status_ =
        (running_step_index_ + 1 >= TaskStepCount(task)) ? Q("任务完成") : Q("步骤完成，准备进入下一步");
    RenderTaskProgressUi();
  }

  bool StartCommandTask(const CommandTask& task, QPushButton* trigger_button = nullptr) {
    if (running_task_.has_value() || task_process_ != nullptr) {
      AppendOperationLog(Q("已有任务正在运行，请等待当前任务结束。"));
      return false;
    }
    running_task_ = task;
    running_step_index_ = 0;
    running_task_button_ = trigger_button;
    if (running_task_button_ != nullptr) {
      running_task_button_->setEnabled(false);
    }
    SyncAdbToLoaderButtonState();
    BeginTaskProgress(task);
    AppendOperationLog(Q("开始执行: " + running_task_->label));
    StartNextTaskStep();
    return true;
  }

  bool IsPartitionMode() const {
    return active_mode_ == Q("DI");
  }

  bool UsesLoaderPath() const {
    return active_mode_ == Q("DB") || active_mode_ == Q("UL") || active_mode_ == Q("EF");
  }

  bool UsesUpdateImage() const {
    return active_mode_ == Q("UF");
  }

  QString ActiveModeTitle() const {
    if (active_mode_ == Q("UF")) return Q("整包烧录");
    if (active_mode_ == Q("DB")) return Q("下载 Boot");
    if (active_mode_ == Q("UL")) return Q("烧录 Loader");
    if (active_mode_ == Q("EF")) return Q("擦除 Flash");
    return Q("分区镜像表");
  }

  QString ActiveModeHint() const {
    if (active_mode_ == Q("UF")) return Q("UF 仅要求 update.img 和可选全局参数，不显示分区镜像表。");
    if (active_mode_ == Q("DB")) return Q("DB 只需要 Loader 文件，用于下载 Boot 建立临时通信。");
    if (active_mode_ == Q("UL")) return Q("UL 只需要 Loader 文件，用于向目标存储写入 Loader。");
    if (active_mode_ == Q("EF")) return Q("EF 需要 Loader 文件；当前 upgrade_tool 会带着它执行整机擦除，不显示 parameter 或分区镜像表。");
    return Q("DI 保留工程目录、parameter.txt 和分区镜像表，可直接编辑并写回 parameter.txt。");
  }

  QString ActiveModeBody() const {
    if (active_mode_ == Q("UF")) return Q("整包烧录只保留 update.img 输入、执行按钮和右侧日志。");
    if (active_mode_ == Q("DB")) return Q("下载 Boot 只保留 Loader 输入，适合先拉起通信链路。");
    if (active_mode_ == Q("UL")) return Q("烧录 Loader 只保留 Loader 输入，适合单独修复加载器。");
    if (active_mode_ == Q("EF")) return Q("擦除模式保留 Loader 输入、执行按钮和右侧日志，执行结果直接看日志输出。");
    return Q("分区烧录支持逐项启停、单独改路径、包大小与 mtdparts 参数双向同步。");
  }

  QString ActiveExecuteLabel() const {
    if (active_mode_ == Q("UF")) return Q("执行整包烧录");
    if (active_mode_ == Q("DB")) return Q("执行下载 Boot");
    if (active_mode_ == Q("UL")) return Q("执行烧录 Loader");
    if (active_mode_ == Q("EF")) return Q("执行擦除 Flash");
    return Q("执行分区烧录");
  }

  void UpdateModeLayout() {
    if (mode_bar_hint_ != nullptr) {
      mode_bar_hint_->setText(ActiveModeHint());
    }
    if (context_title_label_ != nullptr) {
      context_title_label_->setText(IsPartitionMode() ? Q("分区参数") : Q("功能参数"));
    }
    if (context_note_label_ != nullptr) {
      context_note_label_->setText(ActiveModeBody());
    }
    if (project_field_box_ != nullptr) {
      project_field_box_->setVisible(IsPartitionMode());
    }
    if (parameter_field_box_ != nullptr) {
      parameter_field_box_->setVisible(IsPartitionMode());
    }
    if (loader_field_box_ != nullptr) {
      loader_field_box_->setVisible(UsesLoaderPath());
    }
    if (update_field_box_ != nullptr) {
      update_field_box_->setVisible(UsesUpdateImage());
    }
    if (global_args_field_box_ != nullptr) {
      global_args_field_box_->setVisible(true);
    }
    if (center_title_label_ != nullptr) {
      center_title_label_->setText(ActiveModeTitle());
    }
    if (center_hint_label_ != nullptr) {
      center_hint_label_->setText(ActiveModeHint());
    }
    if (action_mode_body_label_ != nullptr) {
      action_mode_body_label_->setText(ActiveModeBody());
    }
    if (partition_editor_container_ != nullptr) {
      partition_editor_container_->setVisible(IsPartitionMode());
    }
    if (mode_empty_space_ != nullptr) {
      mode_empty_space_->setVisible(!IsPartitionMode());
    }
    if (mode_info_container_ != nullptr) {
      mode_info_container_->setVisible(!IsPartitionMode());
    }
    if (partition_count_chip_ != nullptr) {
      partition_count_chip_->setVisible(IsPartitionMode());
    }
    if (mtdparts_chip_ != nullptr) {
      mtdparts_chip_->setVisible(IsPartitionMode());
    }
    if (save_parameter_button_ != nullptr) {
      save_parameter_button_->setVisible(IsPartitionMode());
    }
    if (mode_execute_button_ != nullptr) {
      mode_execute_button_->setText(ActiveExecuteLabel());
    }
    UpdateModeButtons();
    UpdateSaveParameterButton();
  }

  void RenderOperationLog() {
    if (operation_log_ == nullptr) {
      return;
    }
    operation_log_->setPlainText(operation_log_lines_.join(QStringLiteral("\n")));
    operation_log_->moveCursor(QTextCursor::End);
    operation_log_rendered_lines_ = operation_log_lines_.size();
  }

  void AppendOperationLogLinesToView(const QStringList& lines) {
    if (operation_log_ == nullptr || lines.isEmpty()) {
      return;
    }
    if (operation_log_rendered_lines_ != operation_log_lines_.size() - lines.size()) {
      RenderOperationLog();
      return;
    }
    QTextCursor cursor = operation_log_->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (operation_log_rendered_lines_ > 0) {
      cursor.insertText(QStringLiteral("\n"));
    }
    for (int index = 0; index < lines.size(); ++index) {
      cursor.insertText(lines[index]);
      if (index + 1 < lines.size()) {
        cursor.insertText(QStringLiteral("\n"));
      }
    }
    operation_log_->setTextCursor(cursor);
    operation_log_->moveCursor(QTextCursor::End);
    operation_log_rendered_lines_ += lines.size();
  }

  void ReplaceLastOperationLogLineInView(const QString& line) {
    if (operation_log_ == nullptr) {
      return;
    }
    if (operation_log_lines_.isEmpty() || operation_log_rendered_lines_ != operation_log_lines_.size()) {
      RenderOperationLog();
      return;
    }
    QTextCursor cursor = operation_log_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(line);
    operation_log_->setTextCursor(cursor);
    operation_log_->moveCursor(QTextCursor::End);
  }

  void AppendOperationLog(const QString& text) {
    if (text.isEmpty()) {
      return;
    }
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));
    QStringList lines = normalized.split(QChar('\n'));
    while (!lines.isEmpty() && lines.back().isEmpty()) {
      lines.removeLast();
    }
    if (lines.isEmpty()) {
      return;
    }
    operation_log_lines_.append(lines);
    static constexpr int kMaxOperationLogLines = 2000;
    bool trimmed = false;
    while (operation_log_lines_.size() > kMaxOperationLogLines) {
      operation_log_lines_.removeFirst();
      trimmed = true;
    }
    if (trimmed) {
      RenderOperationLog();
      return;
    }
    AppendOperationLogLinesToView(lines);
  }

  std::optional<std::filesystem::path> CurrentProjectPath() const {
    const std::string text = project_input_->text().trimmed().toStdString();
    if (text.empty()) {
      return std::nullopt;
    }
    return std::filesystem::path(text);
  }

  std::optional<std::filesystem::path> EffectiveProjectPath() const {
    if (const auto project_path = CurrentProjectPath(); project_path.has_value()) {
      return project_path;
    }
    if (const auto parameter_path = InputPath(parameter_input_); parameter_path.has_value()) {
      auto root = parameter_path->parent_path();
      if (root.filename() == "Image" && root.has_parent_path()) {
        root = root.parent_path();
      }
      return root;
    }
    return std::nullopt;
  }

  std::optional<std::filesystem::path> InputPath(QLineEdit* input) const {
    const std::string text = input->text().trimmed().toStdString();
    if (text.empty()) {
      return std::nullopt;
    }
    return std::filesystem::path(text);
  }

  std::string CurrentGlobalArgs() const {
    return global_args_input_->text().trimmed().toStdString();
  }

  std::optional<std::filesystem::path> PartitionPathBase() const {
    if (context_.has_value() && !context_->project_dir.empty()) {
      return context_->project_dir;
    }
    return EffectiveProjectPath();
  }

  std::optional<std::filesystem::path> CurrentParameterPath() const {
    if (const auto parameter_path = InputPath(parameter_input_); parameter_path.has_value()) {
      return ResolveUiPath(*parameter_path, CurrentProjectPath());
    }
    if (current_parameter_model_.has_value() && !current_parameter_model_->path.empty()) {
      return current_parameter_model_->path;
    }
    if (context_.has_value() && !context_->parameter_file.empty()) {
      return context_->parameter_file;
    }
    return std::nullopt;
  }

  std::optional<std::filesystem::path> CurrentLoaderPath() const {
    if (const auto loader_path = InputPath(loader_input_); loader_path.has_value()) {
      return ResolveUiPath(*loader_path, CurrentProjectPath());
    }
    if (context_.has_value() && !context_->loader_file.empty()) {
      return context_->loader_file;
    }
    return std::nullopt;
  }

  std::optional<std::filesystem::path> CurrentUpdateImagePath() const {
    if (const auto update_path = InputPath(update_input_); update_path.has_value()) {
      return ResolveUiPath(*update_path, CurrentProjectPath());
    }
    if (context_.has_value() && !context_->update_image.empty()) {
      return context_->update_image;
    }
    return std::nullopt;
  }

  std::uint64_t DetectFileSize(const std::string& file_path) const {
    const std::string trimmed = QString::fromStdString(file_path).trimmed().toStdString();
    if (trimmed.empty()) {
      return 0;
    }
    const auto candidate = ResolveUiPath(std::filesystem::path(trimmed), PartitionPathBase());
    if (!std::filesystem::is_regular_file(candidate)) {
      return 0;
    }
    return std::filesystem::file_size(candidate);
  }

  void SetLineValid(QLineEdit* edit, bool valid) const {
    if (valid) {
      edit->setStyleSheet("");
    } else {
      edit->setStyleSheet("border: 1px solid rgba(188, 79, 51, 0.92);");
    }
  }

  QRect WindowRectFor(const QWidget* widget, const QMargins& margins = QMargins()) const {
    if (widget == nullptr) {
      return QRect();
    }
    const QRect rect(widget->mapTo(this, QPoint(0, 0)), widget->size());
    if (!rect.isValid()) {
      return QRect();
    }
    return rect.marginsAdded(margins);
  }

  QRect CombineWindowRects(std::initializer_list<const QWidget*> widgets, const QMargins& margins = QMargins()) const {
    QRect result;
    bool has_rect = false;
    for (const QWidget* widget : widgets) {
      const QRect rect = WindowRectFor(widget);
      if (!rect.isValid()) {
        continue;
      }
      result = has_rect ? result.united(rect) : rect;
      has_rect = true;
    }
    if (!has_rect) {
      return QRect();
    }
    return result.marginsAdded(margins);
  }

  QPoint ClampBadgeCenter(const QPoint& point) const {
    static constexpr int kBadgeRadius = 24;
    const QRect safe_area = rect().adjusted(kBadgeRadius, kBadgeRadius, -kBadgeRadius, -kBadgeRadius);
    return QPoint(std::clamp(point.x(), safe_area.left(), safe_area.right()),
                  std::clamp(point.y(), safe_area.top(), safe_area.bottom()));
  }

  GuideCallout MakeGuideCallout(
      const QRect& rect,
      const QString& index,
      GuideBadgeAnchor anchor,
      const QPoint& delta = QPoint()) const {
    static constexpr int kBadgeInset = 28;
    const QRect normalized = rect.normalized();
    if (!normalized.isValid()) {
      return {normalized, QPoint(), index};
    }

    QPoint badge_center = normalized.center();
    switch (anchor) {
      case GuideBadgeAnchor::TopLeft:
        badge_center = QPoint(normalized.left() + kBadgeInset, normalized.top() + kBadgeInset);
        break;
      case GuideBadgeAnchor::TopRight:
        badge_center = QPoint(normalized.right() - kBadgeInset, normalized.top() + kBadgeInset);
        break;
      case GuideBadgeAnchor::BottomLeft:
        badge_center = QPoint(normalized.left() + kBadgeInset, normalized.bottom() - kBadgeInset);
        break;
      case GuideBadgeAnchor::BottomRight:
        badge_center = QPoint(normalized.right() - kBadgeInset, normalized.bottom() - kBadgeInset);
        break;
      case GuideBadgeAnchor::LeftCenter:
        badge_center = QPoint(normalized.left() + kBadgeInset, normalized.center().y());
        break;
      case GuideBadgeAnchor::RightCenter:
        badge_center = QPoint(normalized.right() - kBadgeInset, normalized.center().y());
        break;
    }
    return {normalized, ClampBadgeCenter(badge_center + delta), index};
  }

  void ShowTransientIssue(const QString& message) {
    issue_box_->setProperty("clean", false);
    issue_box_->setText(message);
    issue_box_->style()->unpolish(issue_box_);
    issue_box_->style()->polish(issue_box_);
  }

  void ApplyDeviceSnapshot(const DeviceStateSnapshot& snapshot) {
    device_snapshot_ = snapshot;
    status_device_value_->setText(Q(snapshot.primary_state));
    device_primary_value_->setText(Q(snapshot.primary_state));
    device_updated_value_->setText(Q(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString()));
    device_detail_value_->setText(Q(snapshot.detail_text));
    device_error_value_->setText(Q(snapshot.last_error.empty() ? "无" : snapshot.last_error));
    UpdateTopStateBadges(Q(snapshot.primary_state));
    if (device_page_primary_label_ != nullptr) {
      device_page_primary_label_->setText(Q(snapshot.primary_state));
    }
    if (device_page_detail_label_ != nullptr) {
      device_page_detail_label_->setText(Q(snapshot.detail_text));
    }
    SyncAdbToLoaderButtonState();
  }

  void ApplyDisconnectedDeviceState(const QString& error_text) {
    device_snapshot_.reset();
    status_device_value_->setText(Q("未连接"));
    device_primary_value_->setText(Q("未连接"));
    device_updated_value_->setText(Q("-"));
    device_detail_value_->setText(Q("-"));
    device_error_value_->setText(error_text.isEmpty() ? Q("无") : error_text);
    UpdateTopStateBadges(Q("未连接"));
    if (device_page_primary_label_ != nullptr) {
      device_page_primary_label_->setText(Q("未连接"));
    }
    if (device_page_detail_label_ != nullptr) {
      device_page_detail_label_->setText(error_text.isEmpty() ? Q("等待检测。") : error_text);
    }
    SyncAdbToLoaderButtonState();
  }

  std::optional<DeviceStateSnapshot> RefreshDeviceSnapshot() {
    try {
      const auto snapshot = backend_.detect_device_state();
      ApplyDeviceSnapshot(snapshot);
      return snapshot;
    } catch (const std::exception& error) {
      ApplyDisconnectedDeviceState(Q(error.what()));
      return std::nullopt;
    }
  }

  std::optional<QString> ValidateActiveModeDeviceState() {
    const auto snapshot = RefreshDeviceSnapshot();
    const QString state = snapshot.has_value() ? Q(snapshot->primary_state) : Q("未连接");

    if (active_mode_ == Q("DB")) {
      if (state != Q("maskrom")) {
        return Q("DB 需要设备处于 maskrom。当前状态是 ") + state +
               Q("。请先让板子进入 maskrom，再执行下载 Boot。");
      }
      return std::nullopt;
    }

    if (active_mode_ == Q("DI")) {
      if (state != Q("loader")) {
        return Q("DI 需要设备处于 loader。当前状态是 ") + state +
               Q("。如果板子还在 maskrom，请先执行 DB 把它拉到 loader。");
      }
      return std::nullopt;
    }

    if (state != Q("maskrom") && state != Q("loader")) {
      QString action = Q("当前操作");
      if (active_mode_ == Q("UF")) action = Q("UF 整包烧录");
      if (active_mode_ == Q("UL")) action = Q("UL 烧录 Loader");
      if (active_mode_ == Q("EF")) action = Q("EF 擦除 Flash");
      return action + Q(" 需要 Rockusb 通道（maskrom 或 loader）。当前状态是 ") + state +
             Q("，请先让设备进入 maskrom 或 loader。");
    }

    return std::nullopt;
  }

  void UpdateSaveParameterButton() {
    if (save_parameter_button_ == nullptr) {
      return;
    }
    const bool can_save = IsPartitionMode() && CurrentParameterPath().has_value() && !current_rows_.empty();
    save_parameter_button_->setEnabled(can_save);
  }

  QString BuildIssueSummaryText(bool empty_state) const {
    if (current_issues_.empty()) {
      return empty_state ? Q("当前工程为空态。") : Q("当前分区表没有发现容量越界或镜像缺失问题。");
    }
    const QString first = Q(current_issues_.front().message).simplified();
    if (current_issues_.size() == 1) {
      return first;
    }
    return QStringLiteral("发现 %1 项待处理问题，优先修正: %2")
        .arg(static_cast<int>(current_issues_.size()))
        .arg(first);
  }

  void RebuildCurrentIssues() {
    current_issues_ = baseline_issues_;
    if (!current_rows_.empty()) {
      const auto validation_issues = backend_.validate_partition_rows(current_rows_);
      current_issues_.insert(current_issues_.end(), validation_issues.begin(), validation_issues.end());
    }
  }

  void RefreshPartitionEditingState() {
    RebuildCurrentIssues();
    FillPartitionTable();
    BuildFallbackPreviews(CurrentGlobalArgs());
    UpdateSaveParameterButton();
    UpdatePreview();
    UpdateSummaries();
  }

  void SetPartitionRowEnabled(int row_index, bool enabled) {
    if (row_index < 0 || row_index >= static_cast<int>(current_rows_.size())) {
      return;
    }
    current_rows_[static_cast<std::size_t>(row_index)].enabled = enabled;
    RefreshPartitionEditingState();
  }

  void SetPartitionRowPath(int row_index, const QString& path_text) {
    if (row_index < 0 || row_index >= static_cast<int>(current_rows_.size())) {
      return;
    }
    auto& row = current_rows_[static_cast<std::size_t>(row_index)];
    const std::string trimmed = path_text.trimmed().toStdString();
    if (trimmed.empty()) {
      row.file_path.clear();
      row.file_size = 0;
    } else {
      const auto resolved = ResolveUiPath(std::filesystem::path(trimmed), PartitionPathBase());
      row.file_path = resolved.string();
      row.file_size = DetectFileSize(row.file_path);
    }
    RefreshPartitionEditingState();
  }

  void BrowsePartitionRowPath(int row_index) {
    if (row_index < 0 || row_index >= static_cast<int>(current_rows_.size())) {
      return;
    }
    const auto& row = current_rows_[static_cast<std::size_t>(row_index)];
    QString start_path = Q(row.file_path);
    if (start_path.trimmed().isEmpty()) {
      if (const auto base_path = PartitionPathBase(); base_path.has_value()) {
        start_path = Q(base_path->string());
      }
    }
    const QString selected = QFileDialog::getOpenFileName(this, Q("选择分区镜像文件"), start_path.trimmed());
    if (!selected.isEmpty()) {
      SetPartitionRowPath(row_index, selected);
    }
  }

  void SetPartitionRowSpec(int row_index, const QString& spec_text, QLineEdit* spec_edit, QLineEdit* size_edit) {
    if (row_index < 0 || row_index >= static_cast<int>(current_rows_.size())) {
      return;
    }
    auto updated_rows = current_rows_;
    updated_rows[static_cast<std::size_t>(row_index)].partition_spec = spec_text.trimmed().toStdString();
    try {
      updated_rows = backend_.refresh_partition_rows(updated_rows, false);
      SetLineValid(spec_edit, true);
      SetLineValid(size_edit, true);
      current_rows_ = updated_rows;
      RefreshPartitionEditingState();
    } catch (const std::exception& error) {
      SetLineValid(spec_edit, false);
      ShowTransientIssue(Q(error.what()));
    }
  }

  void SetPartitionRowCapacity(int row_index, const QString& size_text, QLineEdit* size_edit, QLineEdit* spec_edit) {
    if (row_index < 0 || row_index >= static_cast<int>(current_rows_.size())) {
      return;
    }
    const auto parsed_size = ParsePartitionCapacityInput(size_text);
    if (!parsed_size.valid) {
      SetLineValid(size_edit, false);
      ShowTransientIssue(Q("包大小格式无效，请输入例如 64M、0x00020000s 或 -"));
      return;
    }

    try {
      auto updated_rows = current_rows_;
      auto definition = backend_.parse_partition_definition(updated_rows[static_cast<std::size_t>(row_index)].partition_spec);
      definition.size_sectors = parsed_size.sectors;
      updated_rows[static_cast<std::size_t>(row_index)].partition_spec = definition.to_spec();
      updated_rows = backend_.refresh_partition_rows(updated_rows, false);
      SetLineValid(size_edit, true);
      SetLineValid(spec_edit, true);
      current_rows_ = updated_rows;
      RefreshPartitionEditingState();
    } catch (const std::exception& error) {
      SetLineValid(size_edit, false);
      ShowTransientIssue(Q(error.what()));
    }
  }

  void SaveParameterFile() {
    if (current_rows_.empty()) {
      ShowTransientIssue(Q("当前没有可写回的分区表。"));
      return;
    }

    const auto parameter_path = CurrentParameterPath();
    if (!parameter_path.has_value()) {
      ShowTransientIssue(Q("当前没有有效的 parameter.txt 路径。"));
      return;
    }

    try {
      ParameterFileModel model = current_parameter_model_.has_value() ? *current_parameter_model_ : backend_.load_parameter_file(*parameter_path);
      auto normalized_rows = backend_.refresh_partition_rows(current_rows_, false);
      const auto saved_path = backend_.save_parameter_file(model, normalized_rows, *parameter_path);
      current_parameter_model_ = backend_.load_parameter_file(saved_path);
      current_rows_ = normalized_rows;
      parameter_input_->setText(Q(saved_path.string()));
      parameter_override_enabled_ = true;
      if (context_.has_value()) {
        context_->parameter_file = saved_path;
      }
      mtdparts_chip_->setText(Q("mtdparts=" + current_parameter_model_->mtdparts_target));
      RefreshPartitionEditingState();
      table_summary_->setText(table_summary_->text() + Q(" 已写回 parameter.txt。"));
    } catch (const std::exception& error) {
      ShowTransientIssue(Q(std::string("写回 parameter.txt 失败: ") + error.what()));
    }
  }

  std::optional<CommandTask> BuildActiveModeTask(QString* error_text = nullptr) {
    auto set_error = [&](const QString& message) -> std::optional<CommandTask> {
      if (error_text != nullptr) {
        *error_text = message;
      }
      return std::nullopt;
    };

    const std::string global_args = CurrentGlobalArgs();
    try {
      if (active_mode_ == Q("UF")) {
        const auto update_path = CurrentUpdateImagePath();
        if (!update_path.has_value() || !std::filesystem::is_regular_file(*update_path)) {
          return set_error(Q("UF 需要一个有效的 update.img。"));
        }
        return backend_.create_upgrade_task("UF", {{"firmware", update_path->string()}, {"noreset", "false"}}, global_args);
      }
      if (active_mode_ == Q("DB")) {
        const auto loader_path = CurrentLoaderPath();
        if (!loader_path.has_value() || !std::filesystem::is_regular_file(*loader_path)) {
          return set_error(Q("DB 需要一个有效的 Loader 文件。"));
        }
        return backend_.create_upgrade_task("DB", {{"loader", loader_path->string()}}, global_args);
      }
      if (active_mode_ == Q("UL")) {
        const auto loader_path = CurrentLoaderPath();
        if (!loader_path.has_value() || !std::filesystem::is_regular_file(*loader_path)) {
          return set_error(Q("UL 需要一个有效的 Loader 文件。"));
        }
        return backend_.create_upgrade_task(
            "UL", {{"loader", loader_path->string()}, {"storage", ""}, {"noreset", "false"}}, global_args);
      }
      if (active_mode_ == Q("EF")) {
        const auto loader_path = CurrentLoaderPath();
        if (!loader_path.has_value() || !std::filesystem::is_regular_file(*loader_path)) {
          return set_error(Q("EF 需要一个有效的 Loader 文件。"));
        }
        return backend_.create_upgrade_task("EF", {{"loader_or_firmware", loader_path->string()}}, global_args);
      }

      const auto parameter_path = CurrentParameterPath();
      if (!parameter_path.has_value() || !std::filesystem::is_regular_file(*parameter_path)) {
        return set_error(Q("DI 需要一个有效的 parameter.txt。"));
      }
      auto rows = backend_.refresh_partition_rows(current_rows_, false);
      const auto issues = backend_.validate_partition_rows(rows);
      if (!issues.empty()) {
        return set_error(Q("DI 已取消：请先修正分区表或镜像文件错误。"));
      }
      current_rows_ = rows;
      return backend_.create_di_task_from_rows(*parameter_path, current_rows_, global_args);
    } catch (const std::exception& error) {
      return set_error(Q(error.what()));
    }
  }

  void CleanupTaskOutputs(const CommandTask& task) {
    for (const auto& path : task.cleanup_paths) {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  }

  void FinishRunningTask(int return_code, const QString& message) {
    if (running_task_.has_value() && return_code == 0) {
      CleanupTaskOutputs(*running_task_);
      running_task_percent_ = 100;
      running_step_percent_ = 100;
      progress_task_status_ = Q("任务完成");
    } else if (return_code != 0) {
      progress_task_status_ = Q("任务失败，返回码: ") + QString::number(return_code);
    }
    RenderTaskProgressUi();
    AppendOperationLog(message);
    running_task_.reset();
    running_step_index_ = -1;
    running_command_code_.clear();
    task_output_tail_.clear();
    process_output_buffer_.clear();
    replaceable_progress_log_key_.clear();
    current_progress_target_label_.clear();
    running_stage_index_ = -1;
    has_replaceable_progress_log_line_ = false;
    replaceable_progress_log_from_carriage_return_ = false;
    if (task_process_ != nullptr) {
      task_process_->deleteLater();
      task_process_ = nullptr;
    }
    if (running_task_button_ != nullptr) {
      running_task_button_->setEnabled(true);
      running_task_button_ = nullptr;
    }
    SyncAdbToLoaderButtonState();
    RefreshPackPage();
    RefreshRuntimePanels(true);
  }

  void StartNextTaskStep() {
    if (!running_task_.has_value()) {
      return;
    }
    if (running_step_index_ >= static_cast<int>(running_task_->steps.size())) {
      FinishRunningTask(0, Q("任务完成"));
      return;
    }

    const auto step = running_task_->steps[static_cast<std::size_t>(running_step_index_)];
    BeginTaskStepProgress(*running_task_, step);
    AppendOperationLog(Q(backend_.preview_task(CommandTask{step.label, {step}, {}})));
    task_process_ = new QProcess(this);
    task_process_->setProcessChannelMode(QProcess::MergedChannels);
    if (!step.cwd.empty()) {
      task_process_->setWorkingDirectory(Q(step.cwd.string()));
    }
    if (!step.argv.empty()) {
      static const QString script_program = Q("/usr/bin/script");
      if (std::filesystem::exists(std::filesystem::path(script_program.toStdString()))) {
        task_process_->setProgram(script_program);
        task_process_->setArguments({Q("-qefc"), BuildScriptCommand(step.argv), Q("/dev/null")});
      } else {
        task_process_->setProgram(Q(step.argv.front()));
        QStringList arguments;
        for (std::size_t i = 1; i < step.argv.size(); ++i) {
          arguments << Q(step.argv[i]);
        }
        task_process_->setArguments(arguments);
      }
    }
    connect(task_process_, &QProcess::readyReadStandardOutput, this, [this]() {
      if (task_process_ == nullptr) {
        return;
      }
      const QString output = QString::fromUtf8(task_process_->readAllStandardOutput());
      const QString sanitized = SanitizeProcessOutput(output);
      if (!sanitized.isEmpty()) {
        UpdateTaskProgressFromOutput(sanitized);
      }
    });
    connect(task_process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exit_code, QProcess::ExitStatus exit_status) {
              if (task_process_ != nullptr) {
                const QString trailing_output = QString::fromUtf8(task_process_->readAllStandardOutput());
                const QString sanitized = SanitizeProcessOutput(trailing_output);
                if (!sanitized.isEmpty()) {
                  UpdateTaskProgressFromOutput(sanitized + Q("\n"));
                } else if (!process_output_buffer_.isEmpty()) {
                  UpdateTaskProgressFromOutput(Q("\n"));
                }
              }
              if (exit_status != QProcess::NormalExit || exit_code != 0) {
                FinishRunningTask(exit_code == 0 ? -1 : exit_code,
                                  Q("任务失败，返回码: " + std::to_string(exit_code)));
                return;
              }
              if (running_task_.has_value()) {
                MarkTaskStepCompleted(*running_task_);
              }
              if (task_process_ != nullptr) {
                task_process_->deleteLater();
                task_process_ = nullptr;
              }
              ++running_step_index_;
              StartNextTaskStep();
            });
    task_process_->start();
    if (!task_process_->waitForStarted(3000)) {
      FinishRunningTask(-1, Q("任务启动失败: 无法拉起外部工具进程。"));
    }
  }

  void ExecuteActiveMode() {
    if (const auto state_error = ValidateActiveModeDeviceState(); state_error.has_value()) {
      AppendOperationLog(*state_error);
      ShowTransientIssue(*state_error);
      QMessageBox::warning(this, Q("设备模式不匹配"), *state_error);
      return;
    }

    QString error_text;
    const auto task = BuildActiveModeTask(&error_text);
    if (!task.has_value()) {
      if (!error_text.isEmpty()) {
        AppendOperationLog(error_text);
        ShowTransientIssue(error_text);
      }
      return;
    }
    StartCommandTask(*task, mode_execute_button_);
  }

  void ClearAutoDetectedInputs() {
    if (!parameter_override_enabled_) {
      parameter_input_->clear();
    }
    if (!loader_override_enabled_) {
      loader_input_->clear();
    }
    if (!update_override_enabled_) {
      update_input_->clear();
    }
  }

  void BuildFallbackPreviews(const std::string& global_args) {
    previews_.clear();

    auto set_preview = [&](const std::string& key, const std::function<CommandTask()>& builder,
                           const std::string& fallback_text) {
      try {
        previews_[key] = builder ? backend_.preview_task(builder()) : fallback_text;
      } catch (const std::exception& error) {
        previews_[key] = error.what();
      }
    };

    const auto loader_path = CurrentLoaderPath();
    const auto update_path = CurrentUpdateImagePath();
    const auto parameter_path = CurrentParameterPath();

    if (update_path.has_value()) {
      set_preview("UF",
                  [&]() {
                    return backend_.create_upgrade_task(
                        "UF", {{"firmware", update_path->string()}, {"noreset", "false"}}, global_args);
                  },
                  "");
    } else {
      previews_["UF"] = "请先准备有效的 update.img 文件。";
    }

    if (loader_path.has_value()) {
      set_preview("DB", [&]() { return backend_.create_upgrade_task("DB", {{"loader", loader_path->string()}}, global_args); }, "");
      set_preview(
          "UL",
          [&]() {
            return backend_.create_upgrade_task(
                "UL", {{"loader", loader_path->string()}, {"storage", ""}, {"noreset", "false"}}, global_args);
          },
          "");
      set_preview(
          "EF",
          [&]() { return backend_.create_upgrade_task("EF", {{"loader_or_firmware", loader_path->string()}}, global_args); },
          "");
    } else {
      previews_["DB"] = "请先准备 Loader 文件。";
      previews_["UL"] = "请先准备 Loader 文件。";
      previews_["EF"] = "请先准备 Loader 文件。";
    }

    if (parameter_path.has_value() && !current_rows_.empty()) {
      bool has_enabled_rows = false;
      for (const auto& row : current_rows_) {
        if (row.enabled && !row.file_path.empty()) {
          has_enabled_rows = true;
          break;
        }
      }
      if (has_enabled_rows) {
        try {
          previews_["DI"] = backend_.preview_task(backend_.create_di_task_from_rows(*parameter_path, current_rows_, global_args));
        } catch (const std::exception& error) {
          previews_["DI"] = error.what();
        }
      } else {
        previews_["DI"] = "已解析 parameter.txt，但当前还没有匹配到可烧录镜像。";
      }
    } else if (parameter_path.has_value()) {
      previews_["DI"] = "已选择 parameter.txt，等待解析分区信息。";
    } else {
      previews_["DI"] = "请先准备 parameter.txt 和分区镜像。";
    }
  }

  void RefreshAll() {
    RefreshProjectPanels();
    RefreshPackPage();
    RefreshHelpPage();
    RefreshRuntimePanels(true);
  }

  void RefreshPackPage() {
    if (pack_project_input_ == nullptr) {
      return;
    }

    const QString default_project = Q(backend_.paths().default_project.string());
    if (pack_project_input_->text().trimmed().isEmpty()) {
      pack_project_input_->setText(default_project);
    }

    const std::filesystem::path project_path = ResolveUiPath(std::filesystem::path(pack_project_input_->text().trimmed().toStdString()));
    if (pack_output_input_ != nullptr && pack_output_input_->text().trimmed().isEmpty()) {
      pack_output_input_->setText(Q((project_path / "update.img").string()));
    }
    if (unpack_output_pack_ != nullptr && unpack_output_pack_->text().trimmed().isEmpty()) {
      unpack_output_pack_->setText(Q((backend_.paths().unpack / "current").string()));
    }
    if (merge_output_pack_ != nullptr && merge_output_pack_->text().trimmed().isEmpty()) {
      merge_output_pack_->setText(Q((backend_.paths().workspace / "merged.img").string()));
    }
    if (unmerge_output_pack_ != nullptr && unmerge_output_pack_->text().trimmed().isEmpty()) {
      unmerge_output_pack_->setText(Q((backend_.paths().unpack / "unmerged").string()));
    }

    try {
      backend_.create_project(project_path);
      const std::filesystem::path package_file = project_path / "package-file";
      if (std::filesystem::is_regular_file(package_file)) {
        if (package_preview_view_ != nullptr) {
          package_preview_view_->setPlainText(ReadTextFile(package_file));
        }
        const auto [missing, existing] = backend_.inspect_project(project_path);
        if (pack_status_label_ != nullptr) {
          pack_status_label_->setText(
              Q("工程已加载: 已就绪 " + std::to_string(existing.size()) + " 项，缺失 " + std::to_string(missing.size()) + " 项素材。"));
        }
      } else {
        if (package_preview_view_ != nullptr) {
          package_preview_view_->setPlainText(Q("# 当前工程还没有 package-file\n\n请先在工程目录中准备 package-file 和镜像素材。"));
        }
        if (pack_status_label_ != nullptr) {
          pack_status_label_->setText(Q("当前工程还没有 package-file。"));
        }
      }
    } catch (const std::exception& error) {
      if (package_preview_view_ != nullptr) {
        package_preview_view_->setPlainText(Q(error.what()));
      }
      if (pack_status_label_ != nullptr) {
        pack_status_label_->setText(Q(error.what()));
      }
    }
  }

  void RefreshAdvancedPage() {
    if (advanced_overview_view_ == nullptr) {
      return;
    }
    std::ostringstream lines;
    const auto overview = backend_.overview();
    lines << "根目录: " << backend_.paths().root.string() << "\n";
    lines << "封包工具目录: " << overview.at("pack_tools") << "\n";
    lines << "升级工具目录: " << overview.at("upgrade_tools") << "\n";
    lines << "默认工程目录: " << overview.at("default_project") << "\n";
    lines << "日志目录: " << overview.at("logs") << "\n\n";
    lines << "mkupdate 配置数: " << overview.at("pack_profiles") << "\n";
    lines << "package-file 模板数: " << overview.at("package_templates") << "\n\n";
    lines << "建议流程:\n";
    lines << "1. 在 打包解包 页准备工程并生成 update.img。\n";
    lines << "2. 在 烧录中心 页执行 UF / DI / DB / UL / EF。\n";
    lines << "3. 如遇设备异常，到 设备信息 页查看环境诊断。\n";
    advanced_overview_view_->setPlainText(Q(lines.str()));
  }

  void RefreshHelpPage() {
    if (help_local_docs_label_ == nullptr) {
      return;
    }
    const auto& paths = backend_.paths();
    const std::filesystem::path guide_path = paths.root / "GUIDE_ZH.md";
    const std::filesystem::path pdf_path = paths.upgrade_tools / "命令行开发工具使用文档.pdf";
    QString text = Q("GUIDE_ZH.md\n") + Q(guide_path.string()) + Q("\n\n命令 PDF\n") + Q(pdf_path.string());
    help_local_docs_label_->setText(text);
  }

  void RefreshProjectPanels() {
    const auto project_path = EffectiveProjectPath();
    status_project_value_->setText(project_path.has_value() ? ShortPathLabel(*project_path) : Q("未选择"));
    status_template_value_->setText(Q(std::to_string(backend_.package_templates().size())));
    status_profile_value_->setText(Q(std::to_string(backend_.pack_profiles().size())));

    previews_.clear();
    current_rows_.clear();
    current_issues_.clear();
    baseline_issues_.clear();
    context_.reset();
    current_parameter_model_.reset();

    const std::string global_args = CurrentGlobalArgs();
    if (!project_path.has_value()) {
      mtdparts_chip_->setText(Q("mtdparts"));
      if (IsPartitionMode()) {
        baseline_issues_.push_back({"", "DI 需要工程目录，或由 parameter.txt 自动推断工程根目录。"});
        RebuildCurrentIssues();
      }
      FillPartitionTable();
      BuildFallbackPreviews(global_args);
      UpdateSaveParameterButton();
      UpdatePreview();
      UpdateSummaries();
      return;
    }

    if (!IsPartitionMode()) {
      try {
        const auto context = backend_.resolve_project_context(*project_path);
        context_ = context;
        current_parameter_model_ = backend_.load_parameter_file(context.parameter_file);
        if (!parameter_override_enabled_) {
          parameter_input_->setText(Q(context.parameter_file.string()));
        }
        if (!loader_override_enabled_) {
          loader_input_->setText(Q(context.loader_file.string()));
        }
        if (!update_override_enabled_) {
          update_input_->setText(Q(context.update_image.string()));
        }
      } catch (const std::exception&) {
      }
      mtdparts_chip_->setText(Q("mtdparts"));
      FillPartitionTable();
      BuildFallbackPreviews(global_args);
      UpdateSaveParameterButton();
      UpdatePreview();
      UpdateSummaries();
      return;
    }

    try {
      ProjectContext context;
      context.project_dir = *project_path;

      try {
        context = backend_.resolve_project_context(*project_path);
      } catch (const std::exception& error) {
        if (!InputPath(parameter_input_).has_value()) {
          throw;
        }
        context.project_dir = *project_path;
        baseline_issues_.push_back({"", "工程上下文不完整，已按手工选择的 parameter.txt 继续解析: " + std::string(error.what())});
      }

      if (const auto parameter_path = InputPath(parameter_input_); parameter_path.has_value()) {
        context.parameter_file = *parameter_path;
      }
      if (const auto loader_path = InputPath(loader_input_); loader_path.has_value()) {
        context.loader_file = *loader_path;
      }
      if (const auto update_path = InputPath(update_input_); update_path.has_value()) {
        context.update_image = *update_path;
      }

      auto [model, rows] = backend_.load_partition_rows(context.project_dir, context.parameter_file);
      if (context.parameter_file.empty()) {
        context.parameter_file = model.path;
      }
      context_ = context;
      current_parameter_model_ = model;
      current_rows_ = rows;
      RebuildCurrentIssues();
      if (!parameter_override_enabled_) {
        parameter_input_->setText(Q(model.path.string()));
      }
      if (!loader_override_enabled_) {
        loader_input_->setText(Q(context.loader_file.string()));
      }
      if (!update_override_enabled_) {
        update_input_->setText(Q(context.update_image.string()));
      }
      mtdparts_chip_->setText(Q("mtdparts=" + model.mtdparts_target));

      FillPartitionTable();
      BuildFallbackPreviews(global_args);
    } catch (const std::exception& error) {
      ClearAutoDetectedInputs();
      mtdparts_chip_->setText(Q("mtdparts"));
      baseline_issues_.push_back({"", error.what()});
      RebuildCurrentIssues();
      FillPartitionTable();
      BuildFallbackPreviews(global_args);
      UpdateSaveParameterButton();
    }

    UpdateSaveParameterButton();
    UpdatePreview();
    UpdateSummaries();
  }

  void ApplyDiagnosticsText(const QString& diagnostics) {
    cached_diagnostics_text_ = diagnostics;
    if (diagnostics_output_ != nullptr) {
      diagnostics_output_->setPlainText(diagnostics);
    }
    if (device_page_diagnostics_view_ != nullptr) {
      device_page_diagnostics_view_->setPlainText(diagnostics);
    }
  }

  bool ShouldRefreshDiagnostics(bool force) const {
    if (running_task_.has_value()) {
      return false;
    }
    if (force) {
      return true;
    }
    if (active_nav_page_ != 2) {
      return false;
    }
    if (cached_diagnostics_text_.isEmpty()) {
      return true;
    }
    static constexpr auto kDiagnosticsRefreshInterval = std::chrono::seconds(20);
    return std::chrono::steady_clock::now() - last_diagnostics_refresh_ >= kDiagnosticsRefreshInterval;
  }

  void RefreshRuntimePanels(bool force_diagnostics = false) {
    if (!running_task_.has_value()) {
      (void)RefreshDeviceSnapshot();
    }
    if (!ShouldRefreshDiagnostics(force_diagnostics)) {
      return;
    }

    try {
      ApplyDiagnosticsText(Q(backend_.collect_environment_diagnostics()));
    } catch (const std::exception& error) {
      ApplyDiagnosticsText(Q(error.what()));
    }
    last_diagnostics_refresh_ = std::chrono::steady_clock::now();
  }

  void BuildPreviews(const std::string& global_args) {
    if (!context_.has_value()) {
      return;
    }
    const auto& context = *context_;
    auto set_preview = [&](const std::string& key, const std::function<CommandTask()>& builder) {
      try {
        previews_[key] = backend_.preview_task(builder());
      } catch (const std::exception& error) {
        previews_[key] = error.what();
      }
    };
    set_preview("UF", [&]() { return backend_.create_upgrade_task("UF", {{"firmware", context.update_image.string()}, {"noreset", "false"}}, global_args); });
    set_preview("DB", [&]() { return backend_.create_upgrade_task("DB", {{"loader", context.loader_file.string()}}, global_args); });
    set_preview("UL", [&]() { return backend_.create_upgrade_task("UL", {{"loader", context.loader_file.string()}, {"storage", ""}, {"noreset", "false"}}, global_args); });
    set_preview("EF", [&]() { return backend_.create_upgrade_task("EF", {{"loader_or_firmware", context.loader_file.string()}}, global_args); });
    set_preview("DI", [&]() { return backend_.create_di_task_from_rows(context.parameter_file, current_rows_, global_args); });
  }

  void FillPartitionTable() {
    if (current_rows_.empty()) {
      empty_table_note_->show();
      partition_table_->hide();
      partition_table_->clearContents();
      partition_table_->clearSpans();
      partition_table_->setRowCount(0);
      partition_count_chip_->setText(Q("0 项"));
      table_summary_->setText(Q("当前工程没有可用分区，中心区域保持空表态。"));
      issue_box_->setProperty("clean", current_issues_.empty());
      issue_box_->setText(BuildIssueSummaryText(true));
      issue_box_->style()->unpolish(issue_box_);
      issue_box_->style()->polish(issue_box_);
      return;
    }

    empty_table_note_->hide();
    partition_table_->show();
    partition_table_->clearContents();
    partition_table_->clearSpans();
    partition_table_->setRowCount(static_cast<int>(current_rows_.size()));
    int enabled_count = 0;
    int bound_count = 0;
    for (int row_index = 0; row_index < static_cast<int>(current_rows_.size()); ++row_index) {
      const auto& row = current_rows_[static_cast<std::size_t>(row_index)];
      if (row.enabled) {
        ++enabled_count;
      }
      if (!row.file_path.empty()) {
        ++bound_count;
      }

      auto* enabled_box = new QCheckBox();
      enabled_box->setChecked(row.enabled);
      auto* enabled_host = new QWidget();
      auto* enabled_layout = new QHBoxLayout(enabled_host);
      enabled_layout->setContentsMargins(0, 0, 0, 0);
      enabled_layout->setSpacing(0);
      enabled_layout->addStretch(1);
      enabled_layout->addWidget(enabled_box);
      enabled_layout->addStretch(1);

      auto* name_item = new QTableWidgetItem(Q(row.name));
      auto* spec_edit = new QLineEdit(Q(row.partition_spec));
      spec_edit->setObjectName("PartitionPathEdit");
      spec_edit->setToolTip(Q(row.partition_spec));

      auto* path_edit = new QLineEdit();
      path_edit->setObjectName("PartitionPathEdit");
      path_edit->setText(Q(row.file_path));
      path_edit->setPlaceholderText(Q("手动指定该分区镜像路径"));
      path_edit->setToolTip(Q(row.file_path));
      auto* browse = new QPushButton(Q("浏览"));
      browse->setObjectName("TableBrowseButton");
      auto* path_host = new QWidget();
      auto* path_layout = new QHBoxLayout(path_host);
      path_layout->setContentsMargins(2, 1, 2, 1);
      path_layout->setSpacing(3);
      path_layout->addWidget(path_edit, 1);
      path_layout->addWidget(browse, 0);

      QString size_text = Q("-");
      QString size_tooltip = Q("-");
      try {
        const auto definition = backend_.parse_partition_definition(row.partition_spec);
        size_text = FormatPartitionCapacityText(definition);
        size_tooltip = FormatPartitionCapacityTooltip(definition);
      } catch (const std::exception& error) {
        size_text = Q(error.what());
        size_tooltip = size_text;
      }
      auto* size_edit = new QLineEdit(size_text);
      size_edit->setObjectName("PartitionPathEdit");
      size_edit->setPlaceholderText(Q("例如 64M / 0x00020000s / -"));
      size_edit->setToolTip(size_tooltip);

      name_item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);

      partition_table_->setCellWidget(row_index, 0, enabled_host);
      partition_table_->setItem(row_index, 1, name_item);
      partition_table_->setCellWidget(row_index, 2, path_host);
      partition_table_->setCellWidget(row_index, 3, size_edit);
      partition_table_->setCellWidget(row_index, 4, spec_edit);
      partition_table_->setRowHeight(row_index, 28);

      connect(enabled_box, &QCheckBox::toggled, this, [this, row_index](bool checked) {
        SetPartitionRowEnabled(row_index, checked);
      });
      connect(path_edit, &QLineEdit::textChanged, this, [this, path_edit]() { SetLineValid(path_edit, true); });
      connect(path_edit, &QLineEdit::editingFinished, this, [this, row_index, path_edit]() {
        SetPartitionRowPath(row_index, path_edit->text());
      });
      connect(browse, &QPushButton::clicked, this, [this, row_index]() { BrowsePartitionRowPath(row_index); });
      connect(size_edit, &QLineEdit::textChanged, this, [this, size_edit]() { SetLineValid(size_edit, true); });
      connect(size_edit, &QLineEdit::editingFinished, this, [this, row_index, size_edit, spec_edit]() {
        SetPartitionRowCapacity(row_index, size_edit->text(), size_edit, spec_edit);
      });
      connect(spec_edit, &QLineEdit::textChanged, this, [this, spec_edit]() { SetLineValid(spec_edit, true); });
      connect(spec_edit, &QLineEdit::editingFinished, this, [this, row_index, spec_edit, size_edit]() {
        SetPartitionRowSpec(row_index, spec_edit->text(), spec_edit, size_edit);
      });
    }

    partition_count_chip_->setText(Q(std::to_string(current_rows_.size()) + " 项"));
    table_summary_->setText(
        Q("已加载 " + std::to_string(current_rows_.size()) + " 个分区，其中 " + std::to_string(enabled_count) + " 个已启用，" +
          std::to_string(bound_count) + " 个已绑定镜像文件。"));

    issue_box_->setProperty("clean", current_issues_.empty());
    issue_box_->setText(BuildIssueSummaryText(false));
    issue_box_->style()->unpolish(issue_box_);
    issue_box_->style()->polish(issue_box_);
  }

  void UpdatePreview() {
    if (command_preview_ == nullptr) {
      return;
    }
    const auto iter = previews_.find(active_mode_.toStdString());
    if (iter == previews_.end()) {
      command_preview_->setPlainText(Q("当前模式暂无预览"));
      return;
    }
    command_preview_->setPlainText(Q(iter->second));
  }

  void UpdateModeButtons() {
    for (const auto& [code, button] : mode_buttons_) {
      button->setProperty("active", code == active_mode_);
      button->style()->unpolish(button);
      button->style()->polish(button);
      button->update();
    }
  }

  void UpdateTopStateBadges(const QString& active_state) {
    for (const auto& [state, badge] : top_state_badges_) {
      badge->setProperty("active", state == active_state);
      badge->style()->unpolish(badge);
      badge->style()->polish(badge);
      badge->update();
    }
  }

  void UpdateSummaries() {
    if (summary_mode_value_ != nullptr) {
      QString mode_desc = Q("分区烧录");
      if (active_mode_ == Q("UF")) mode_desc = Q("整包烧录");
      if (active_mode_ == Q("DB")) mode_desc = Q("下载 Boot");
      if (active_mode_ == Q("UL")) mode_desc = Q("烧录 Loader");
      if (active_mode_ == Q("EF")) mode_desc = Q("擦除 Flash");
      summary_mode_value_->setText(active_mode_ + Q(" / ") + mode_desc);
    }
    if (summary_project_state_value_ != nullptr) {
      summary_project_state_value_->setText(context_.has_value() ? Q("工程已装载") : Q("等待读取工程"));
    }

    QString mode_desc = Q("分区烧录");
    if (active_mode_ == Q("UF")) mode_desc = Q("整包烧录");
    if (active_mode_ == Q("DB")) mode_desc = Q("下载 Boot");
    if (active_mode_ == Q("UL")) mode_desc = Q("烧录 Loader");
    if (active_mode_ == Q("EF")) mode_desc = Q("擦除 Flash");

    int enabled_count = 0;
    for (const auto& row : current_rows_) {
      if (row.enabled) {
        ++enabled_count;
      }
    }
    if (summary_partition_value_ != nullptr) {
      summary_partition_value_->setText(Q(std::to_string(current_rows_.size()) + " 项 / " + std::to_string(enabled_count) + " 启用"));
    }
    if (summary_issue_value_ != nullptr) {
      summary_issue_value_->setText(current_issues_.empty() ? Q("当前未发现阻塞性问题") : Q(current_issues_.front().message));
    }
    side_queue_value_->setText(Q(current_issues_.empty() ? "0" : std::to_string(static_cast<int>(current_issues_.size()))));
    side_partition_value_->setText(Q(std::to_string(static_cast<int>(current_rows_.size()))));
    side_issue_value_->setText(Q(current_issues_.empty() ? "正常" : "告警"));
  }
};

}  // namespace

int RunQtApp(int argc, char** argv, StudioBackend& backend, bool smoke_test, const std::string& capture_path, int capture_nav_page) {
  QApplication app(argc, argv);
  app.setFont(BuildUiFont());
  NativeMainWindow window(backend);
  if (capture_nav_page > 0) {
    window.ActivateNavPage(capture_nav_page);
  }
  window.show();
  if (smoke_test) {
    QTimer::singleShot(250, &app, &QApplication::quit);
  }
  if (!capture_path.empty()) {
    QTimer::singleShot(350, &app, [&window, capture_path, &app]() {
      window.grab().save(Q(capture_path));
      app.quit();
    });
  }
  return app.exec();
}

int GenerateHelpGuideAssets(int argc, char** argv, StudioBackend& backend) {
  QApplication app(argc, argv);
  app.setFont(BuildUiFont());
  NativeMainWindow window(backend);
  window.show();

  auto draw_callouts = [](QImage* image, const std::vector<GuideCallout>& callouts) {
    QPainter painter(image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen pen(QColor(122, 147, 61));
    pen.setWidth(6);
    pen.setStyle(Qt::DashLine);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    QFont number_font = painter.font();
    number_font.setBold(true);
    number_font.setPointSize(18);

    for (const auto& callout : callouts) {
      if (!callout.rect.isValid()) {
        continue;
      }
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(callout.rect, 18, 18);

      const QRect badge_rect(callout.badge_center.x() - 24, callout.badge_center.y() - 24, 48, 48);
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(111, 136, 49));
      painter.drawEllipse(badge_rect);

      painter.setFont(number_font);
      painter.setPen(QColor(247, 250, 240));
      painter.drawText(badge_rect, Qt::AlignCenter, callout.index);
      painter.setPen(pen);
    }
  };

  auto capture_page = [&](int nav_page,
                          const std::filesystem::path& output_path,
                          const std::function<std::vector<GuideCallout>()>& build_callouts) {
    window.ActivateNavPage(nav_page);
    app.processEvents();
    QThread::msleep(140);
    app.processEvents();
    QImage image = window.grab().toImage();
    draw_callouts(&image, build_callouts());
    image.save(Q(output_path.string()));
  };

  QTimer::singleShot(400, &app, [&]() {
    const std::filesystem::path asset_root = backend.paths().root / "rkstudio" / "assets";

    capture_page(0, asset_root / "guide-overview.png", [&window]() { return window.BuildOverviewGuideCallouts(); });

    capture_page(0, asset_root / "guide-burn.png", [&window]() { return window.BuildBurnGuideCallouts(); });

    capture_page(1, asset_root / "guide-pack.png", [&window]() { return window.BuildPackGuideCallouts(); });

    capture_page(2, asset_root / "guide-device.png", [&window]() { return window.BuildDeviceGuideCallouts(); });

    app.quit();
  });

  return app.exec();
}

}  // namespace rkstudio
