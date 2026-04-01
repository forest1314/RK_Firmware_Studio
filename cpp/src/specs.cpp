#include "rkstudio/specs.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

#include "rkstudio/utils.h"

namespace rkstudio {

namespace {

std::string RequireText(const StringMap& values, const std::string& key, const std::string& label) {
  const auto iter = values.find(key);
  const std::string value = iter == values.end() ? "" : Trim(iter->second);
  if (value.empty()) {
    throw BackendError(label + " 不能为空");
  }
  return value;
}

std::string Text(const StringMap& values, const std::string& key) {
  const auto iter = values.find(key);
  return iter == values.end() ? "" : Trim(iter->second);
}

bool Flag(const StringMap& values, const std::string& key) {
  const auto iter = values.find(key);
  if (iter == values.end()) {
    return false;
  }
  const std::string lowered = ToLower(Trim(iter->second));
  return lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on";
}

std::string PartitionFlag(const std::string& name) {
  static const std::unordered_map<std::string, std::string> kMapping = {
      {"parameter", "-p"},
      {"boot", "-b"},
      {"kernel", "-k"},
      {"system", "-s"},
      {"recovery", "-r"},
      {"misc", "-m"},
      {"uboot", "-u"},
      {"trust", "-t"},
      {"resource", "-re"},
  };
  const std::string trimmed = Trim(name);
  const std::string lowered = ToLower(trimmed);
  const auto iter = kMapping.find(lowered);
  if (iter != kMapping.end()) {
    return iter->second;
  }
  return "-" + trimmed;
}

std::vector<std::string> BuildSimple(const std::string& code) {
  return {code};
}

std::vector<std::string> BuildOptionalSingle(const std::string& code, const StringMap& values, const std::string& key) {
  const std::string value = Text(values, key);
  if (value.empty()) {
    return {code};
  }
  return {code, value};
}

std::vector<std::string> BuildUf(const StringMap& values) {
  std::vector<std::string> args = {"UF", RequireText(values, "firmware", "固件文件")};
  if (Flag(values, "noreset")) {
    args.push_back("-noreset");
  }
  return args;
}

std::vector<std::string> BuildUl(const StringMap& values) {
  std::vector<std::string> args = {"UL", RequireText(values, "loader", "Loader 文件")};
  if (Flag(values, "noreset")) {
    args.push_back("-noreset");
  }
  const std::string storage = Text(values, "storage");
  if (!storage.empty()) {
    args.push_back(storage);
  }
  return args;
}

std::vector<std::string> BuildDb(const StringMap& values) {
  return {"DB", RequireText(values, "loader", "Loader 文件")};
}

std::vector<std::string> BuildDi(const StringMap& values) {
  std::vector<std::string> args = {"DI"};
  const std::string parameter = Text(values, "parameter");
  if (!parameter.empty()) {
    args.push_back("-p");
    args.push_back(parameter);
  }

  bool valid_item = false;
  std::string mappings = Text(values, "items");
  std::size_t cursor = 0;
  while (cursor <= mappings.size()) {
    const std::size_t next = mappings.find('\n', cursor);
    const std::string raw_line = mappings.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor);
    const std::string line = Trim(raw_line);
    if (!line.empty() && line[0] != '#') {
      const std::size_t eq = line.find('=');
      if (eq == std::string::npos) {
        throw BackendError("分区镜像列表格式应为 分区名=镜像路径");
      }
      const std::string name = Trim(line.substr(0, eq));
      const std::string image = Trim(line.substr(eq + 1));
      if (name.empty() || image.empty()) {
        throw BackendError("分区镜像列表中的分区名和镜像路径都不能为空");
      }
      args.push_back(PartitionFlag(name));
      args.push_back(image);
      valid_item = true;
    }
    if (next == std::string::npos) {
      break;
    }
    cursor = next + 1;
  }

  const auto extra_args = SplitShellArgs(Text(values, "extra_args"));
  if (!extra_args.empty()) {
    args.insert(args.end(), extra_args.begin(), extra_args.end());
    valid_item = true;
  }

  if (args.size() == 1 || !valid_item) {
    throw BackendError("至少需要提供一个待烧录分区或附加 DI 参数");
  }
  return args;
}

std::vector<std::string> BuildEf(const StringMap& values) {
  std::string input_file = Text(values, "loader_or_firmware");
  if (input_file.empty()) {
    input_file = Text(values, "loader");
  }
  if (input_file.empty()) {
    input_file = Text(values, "firmware");
  }
  if (input_file.empty()) {
    throw BackendError("EF 需要一个有效的 Loader 或固件文件");
  }
  return {"EF", input_file};
}

std::vector<std::string> BuildSn(const StringMap& values) {
  return {"SN", RequireText(values, "serial_number", "序列号")};
}

std::vector<std::string> BuildRcl(const StringMap& values) {
  return {"RCL", RequireText(values, "output_file", "输出文件")};
}

std::vector<std::string> BuildGpt(const StringMap& values) {
  return {
      "GPT",
      RequireText(values, "input_parameter", "输入 parameter"),
      RequireText(values, "output_gpt", "输出 GPT 文件"),
  };
}

std::vector<std::string> BuildSfi(const StringMap& values) {
  std::vector<std::string> args = {"SFI", RequireText(values, "firmware", "固件文件")};
  const std::string entry_no = Text(values, "entry_no");
  if (!entry_no.empty()) {
    args.push_back(entry_no);
  }
  return args;
}

std::vector<std::string> BuildExf(const StringMap& values) {
  return {
      "EXF",
      RequireText(values, "input_file", "输入固件或 Loader"),
      RequireText(values, "output_dir", "输出目录"),
  };
}

std::vector<std::string> BuildCpu(const StringMap& values) {
  std::vector<std::string> args = {"CPU"};
  const std::string mode = Text(values, "mode");
  if (!mode.empty() && mode != "默认") {
    args.push_back(mode);
  }
  return args;
}

std::vector<std::string> BuildRl(const StringMap& values) {
  std::vector<std::string> args = {
      "RL",
      RequireText(values, "begin_sec", "起始扇区"),
      RequireText(values, "sector_len", "扇区长度"),
  };
  const std::string output_file = Text(values, "output_file");
  if (!output_file.empty()) {
    args.push_back(output_file);
  }
  return args;
}

std::vector<std::string> BuildWl(const StringMap& values) {
  std::vector<std::string> args = {"WL", RequireText(values, "begin_sec", "起始扇区")};
  const std::string size_sec = Text(values, "size_sec");
  if (!size_sec.empty()) {
    args.push_back(size_sec);
  }
  args.push_back(RequireText(values, "input_file", "输入文件"));
  return args;
}

std::vector<std::string> BuildEl(const StringMap& values) {
  return {
      "EL",
      RequireText(values, "begin_sec", "起始扇区"),
      RequireText(values, "erase_count", "擦除扇区数"),
  };
}

std::vector<std::string> BuildEb(const StringMap& values) {
  std::vector<std::string> args = {
      "EB",
      RequireText(values, "cs", "Chip Select"),
      RequireText(values, "begin_block", "起始块"),
      RequireText(values, "block_len", "块长度"),
  };
  if (Flag(values, "force")) {
    args.push_back("--Force");
  }
  return args;
}

std::vector<std::string> BuildRun(const StringMap& values) {
  return {
      "RUN",
      RequireText(values, "uboot_addr", "u-boot 地址"),
      RequireText(values, "trust_addr", "trust 地址"),
      RequireText(values, "boot_addr", "boot 地址"),
      RequireText(values, "uboot_file", "u-boot 文件"),
      RequireText(values, "trust_file", "trust 文件"),
      RequireText(values, "boot_file", "boot 文件"),
  };
}

}  // namespace

std::vector<CommandSpec> GetUpgradeCommandSpecs() {
  return {
      {"H", "帮助", "基础", "显示 upgrade_tool 的命令帮助。"},
      {"V", "版本", "基础", "显示 upgrade_tool 版本。"},
      {"LG", "日志开关", "基础", "执行工具日志控制命令。"},
      {"CD", "选择设备", "设备", "在多设备场景下交互式选择当前设备。"},
      {"LD", "列出设备", "设备", "列出当前连接的 Rockusb 设备。"},
      {"SD", "切换设备", "设备", "切换当前操作设备。"},
      {"UF", "整包烧录", "升级", "烧录 update.img 整包固件。"},
      {"UL", "烧录 Loader", "升级", "烧录 Loader/IDBlock，可选指定目标存储。"},
      {"DI", "烧录分区镜像", "升级", "按分区写入镜像。"},
      {"DB", "下载 Boot", "升级", "在 Maskrom 下临时下载 Loader。"},
      {"EF", "擦除 Flash", "升级", "擦除设备存储。"},
      {"PL", "读取分区表", "信息", "读取设备分区信息。"},
      {"SN", "写序列号", "设备", "向设备写入序列号。"},
      {"RSN", "读序列号", "信息", "读取设备序列号。"},
      {"RCL", "读取通信日志", "信息", "把通信日志保存到文件。"},
      {"GPT", "生成 GPT", "封包辅助", "根据 parameter 文件生成 GPT 文件。"},
      {"SSD", "切换存储", "设备", "切换当前操作的目标存储。"},
      {"SU3", "切换 USB3", "设备", "切换设备到 USB3 传输模式。"},
      {"SFI", "查看固件信息", "信息", "显示 update.img 或 loader 条目信息。"},
      {"EXF", "提取固件/Loader", "信息", "提取 update.img 或 loader 内容到目录。"},
      {"TD", "测试设备", "专家", "执行设备状态测试。"},
      {"RD", "复位设备", "设备", "复位设备。"},
      {"RP", "复位 Pipe", "专家", "复位指定 Pipe。"},
      {"RCB", "读能力", "信息", "读取设备能力信息。"},
      {"RID", "读 Flash ID", "信息", "读取 Flash ID。"},
      {"RFI", "读 Flash 信息", "信息", "读取存储信息。"},
      {"RCI", "读芯片信息", "信息", "读取芯片信息。"},
      {"CPU", "读 CPU ID", "信息", "读取 CPU ID。"},
      {"RSM", "读安全模式", "信息", "读取安全模式。"},
      {"RL", "按地址读 LBA", "专家", "按 LBA 地址读取设备数据。"},
      {"WL", "按地址写 LBA", "专家", "按 LBA 地址直接写入文件。"},
      {"EL", "按地址擦除 LBA", "专家", "按扇区范围擦除。"},
      {"EB", "按块擦除", "专家", "按块擦除。"},
      {"RUN", "直接运行系统", "专家", "直接加载系统组件并运行。"},
  };
}

CommandSpec FindUpgradeCommandSpec(const std::string& code) {
  for (const auto& spec : GetUpgradeCommandSpecs()) {
    if (spec.code == code) {
      return spec;
    }
  }
  throw BackendError("未知命令: " + code);
}

std::vector<std::string> BuildUpgradeArgs(const std::string& code, const StringMap& values) {
  if (code == "H" || code == "V" || code == "LG" || code == "CD" || code == "LD" || code == "SD" || code == "PL" ||
      code == "RSN" || code == "SU3" || code == "TD" || code == "RCB" || code == "RID" || code == "RFI" ||
      code == "RCI" || code == "RSM") {
    return BuildSimple(code);
  }
  if (code == "UF") {
    return BuildUf(values);
  }
  if (code == "UL") {
    return BuildUl(values);
  }
  if (code == "DI") {
    return BuildDi(values);
  }
  if (code == "DB") {
    return BuildDb(values);
  }
  if (code == "EF") {
    return BuildEf(values);
  }
  if (code == "SN") {
    return BuildSn(values);
  }
  if (code == "RCL") {
    return BuildRcl(values);
  }
  if (code == "GPT") {
    return BuildGpt(values);
  }
  if (code == "SSD") {
    return BuildOptionalSingle("SSD", values, "storage_no");
  }
  if (code == "SFI") {
    return BuildSfi(values);
  }
  if (code == "EXF") {
    return BuildExf(values);
  }
  if (code == "RD") {
    return BuildOptionalSingle("RD", values, "subcode");
  }
  if (code == "RP") {
    return BuildOptionalSingle("RP", values, "pipe");
  }
  if (code == "CPU") {
    return BuildCpu(values);
  }
  if (code == "RL") {
    return BuildRl(values);
  }
  if (code == "WL") {
    return BuildWl(values);
  }
  if (code == "EL") {
    return BuildEl(values);
  }
  if (code == "EB") {
    return BuildEb(values);
  }
  if (code == "RUN") {
    return BuildRun(values);
  }
  throw BackendError("未知命令: " + code);
}

std::vector<std::string> SplitShellArgs(const std::string& text) {
  std::vector<std::string> args;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  bool escaping = false;

  for (char ch : text) {
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (!in_single && !in_double && std::isspace(static_cast<unsigned char>(ch))) {
      if (!current.empty()) {
        args.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escaping || in_single || in_double) {
    throw BackendError("命令参数格式不完整");
  }
  if (!current.empty()) {
    args.push_back(current);
  }
  return args;
}

}  // namespace rkstudio
