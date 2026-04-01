#include "rkstudio/qt_style.h"

#include <QFontDatabase>
#include <QStringList>

namespace rkstudio {

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

}  // namespace rkstudio
