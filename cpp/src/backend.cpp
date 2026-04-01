#include "rkstudio/backend.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace rkstudio {

namespace {

const std::regex kPackageFileRegex("\\s+");
const std::regex kMtdpartsRegex(R"(mtdparts=([^:]*):(.+)$)", std::regex::icase);
const std::regex kLocationIdRegex(R"(locationid\s*[:=]\s*([0-9a-zA-Zx_-]+))", std::regex::icase);
const std::regex kDeviceNoRegex(R"(devno\s*[:=]\s*([0-9a-zA-Zx_-]+))", std::regex::icase);

std::vector<std::string> ReadLines(const fs::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw BackendError("无法读取文件: " + path.string());
  }
  std::vector<std::string> lines;
  for (std::string line; std::getline(stream, line);) {
    lines.push_back(line);
  }
  return lines;
}

std::string ReadText(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw BackendError("无法读取文件: " + path.string());
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::uint64_t SectorCountForFile(const fs::path& path) {
  if (!fs::is_regular_file(path)) {
    return 0;
  }
  const std::uint64_t size = fs::file_size(path);
  if (size == 0) {
    return 0;
  }
  return (size + kSectorSize - 1) / kSectorSize;
}

std::string FormatSectorHex(std::uint64_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << value;
  return stream.str();
}

std::pair<std::string, std::vector<std::string>> SplitNameToken(const std::string& token) {
  std::vector<std::string> parts;
  std::size_t cursor = 0;
  while (cursor <= token.size()) {
    const std::size_t next = token.find(':', cursor);
    const std::string part = Trim(token.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor));
    if (!part.empty()) {
      parts.push_back(part);
    }
    if (next == std::string::npos) {
      break;
    }
    cursor = next + 1;
  }
  if (parts.empty()) {
    throw BackendError("分区名不能为空");
  }
  return {parts.front(), std::vector<std::string>(parts.begin() + 1, parts.end())};
}

std::string JoinQuoted(const std::vector<std::string>& argv) {
  std::ostringstream stream;
  bool first = true;
  for (const auto& arg : argv) {
    if (!first) {
      stream << ' ';
    }
    first = false;
    if (arg.find_first_of(" \t\n\"'\\") == std::string::npos) {
      stream << arg;
      continue;
    }
    stream << '\'';
    for (char ch : arg) {
      if (ch == '\'') {
        stream << "'\\''";
      } else {
        stream << ch;
      }
    }
    stream << '\'';
  }
  return stream.str();
}

std::pair<int, std::string> RunCommand(
    const std::vector<std::string>& argv,
    const fs::path& cwd,
    int timeout_seconds,
    bool check_nonzero) {
  if (argv.empty()) {
    throw BackendError("空命令无法执行");
  }

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    throw BackendError("无法创建输出管道");
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    throw BackendError("无法创建子进程");
  }

  if (pid == 0) {
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    chdir(cwd.c_str());

    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const auto& item : argv) {
      args.push_back(const_cast<char*>(item.c_str()));
    }
    args.push_back(nullptr);
    execvp(args[0], args.data());
    _exit(127);
  }

  close(pipefd[1]);
  std::string output;
  std::array<char, 4096> buffer{};
  bool timed_out = false;
  int status = 0;

  while (true) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(pipefd[0], &readfds);

    struct timeval tv {
    };
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;

    const int ready = select(pipefd[0] + 1, &readfds, nullptr, nullptr, &tv);
    if (ready > 0 && FD_ISSET(pipefd[0], &readfds)) {
      const ssize_t count = read(pipefd[0], buffer.data(), buffer.size());
      if (count > 0) {
        output.append(buffer.data(), static_cast<std::size_t>(count));
      } else {
        break;
      }
    } else if (ready == 0) {
      timed_out = true;
      kill(pid, SIGKILL);
      break;
    } else if (ready < 0 && errno != EINTR) {
      break;
    }

    const pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
      while (true) {
        const ssize_t count = read(pipefd[0], buffer.data(), buffer.size());
        if (count > 0) {
          output.append(buffer.data(), static_cast<std::size_t>(count));
        } else {
          break;
        }
      }
      break;
    }
  }

  close(pipefd[0]);
  if (timed_out) {
    waitpid(pid, &status, 0);
    throw BackendError("命令超时: " + JoinQuoted(argv));
  }

  if (!WIFEXITED(status)) {
    if (check_nonzero) {
      throw BackendError(output.empty() ? "命令执行失败" : Trim(output));
    }
    return {128, Trim(output)};
  }

  const int rc = WEXITSTATUS(status);
  const std::string trimmed = Trim(output);
  if (check_nonzero && rc != 0) {
    throw BackendError(trimmed.empty() ? ("命令返回非零退出码 " + std::to_string(rc)) : trimmed);
  }
  return {rc, trimmed};
}

}  // namespace

bool PackageEntry::source_required() const {
  if (name == "package-file") {
    return false;
  }
  return !is_reserved;
}

std::string PackProfile::display_name() const {
  return name + " (" + chip_code + ")";
}

bool PartitionDefinition::grow() const {
  return !size_sectors.has_value();
}

std::string PartitionDefinition::name_token() const {
  if (flags.empty()) {
    return name;
  }
  std::ostringstream stream;
  stream << name;
  for (const auto& flag : flags) {
    stream << ':' << flag;
  }
  return stream.str();
}

std::string PartitionDefinition::to_spec() const {
  return (grow() ? "-" : FormatSectorHex(*size_sectors)) + "@" + FormatSectorHex(offset_sectors) + "(" + name_token() + ")";
}

StudioBackend::StudioBackend(AppPaths paths) : paths_(std::move(paths)), pack_profiles_(load_pack_profiles()) {}

const AppPaths& StudioBackend::paths() const {
  return paths_;
}

const std::vector<PackProfile>& StudioBackend::pack_profiles() const {
  return pack_profiles_;
}

std::vector<fs::path> StudioBackend::package_templates() const {
  if (package_templates_cache_.has_value()) {
    return *package_templates_cache_;
  }
  std::vector<fs::path> templates;
  for (const auto& entry : fs::directory_iterator(paths_.pack_tools)) {
    if (entry.is_regular_file()) {
      const std::string name = entry.path().filename().string();
      if (name.find("package-file") != std::string::npos && name.rfind(".sh") == std::string::npos) {
        templates.push_back(entry.path());
      }
    }
  }
  std::sort(templates.begin(), templates.end());
  package_templates_cache_ = templates;
  return *package_templates_cache_;
}

std::vector<ToolDescriptor> StudioBackend::tool_descriptors() const {
  return {
      {"upgrade_tool", "upgrade_tool", paths_.upgrade_tools / "upgrade_tool", paths_.upgrade_tools},
      {"afptool", "afptool", paths_.pack_tools / "afptool", paths_.pack_tools},
      {"rkImageMaker", "rkImageMaker", paths_.pack_tools / "rkImageMaker", paths_.pack_tools},
  };
}

ToolDescriptor StudioBackend::get_tool(const std::string& key) const {
  for (const auto& tool : tool_descriptors()) {
    if (tool.key == key) {
      return tool;
    }
  }
  throw BackendError("未知工具: " + key);
}

fs::path StudioBackend::create_project(const fs::path& project_dir) const {
  const fs::path project = ResolvePath(project_dir);
  fs::create_directories(project / "Image");
  fs::create_directories(project / ".rkstudio");
  return project;
}

ProjectContext StudioBackend::resolve_project_context(const fs::path& project_dir) const {
  const fs::path project = create_project(project_dir);
  const fs::path package_file = project / "package-file";
  if (!fs::is_regular_file(package_file)) {
    throw BackendError("未找到 package-file: " + package_file.string());
  }

  const auto entries = project_entries(project);
  std::map<std::string, fs::path> entry_map;
  for (const auto& entry : entries) {
    entry_map[entry.name] = project / entry.relative_path;
  }

  fs::path parameter_file;
  if (entry_map.count("parameter") > 0) {
    parameter_file = entry_map["parameter"];
  } else {
    for (const auto& candidate : {project / "parameter.txt", project / "parameter", project / "Image" / "parameter.txt"}) {
      if (fs::is_regular_file(candidate)) {
        parameter_file = candidate;
        break;
      }
    }
  }
  if (parameter_file.empty() || !fs::is_regular_file(parameter_file)) {
    throw BackendError("工程中缺少 parameter.txt");
  }

  fs::path loader_file;
  if (entry_map.count("bootloader") > 0) {
    loader_file = entry_map["bootloader"];
  } else {
    for (const auto& candidate : {project / "MiniLoaderAll.bin", project / "Image" / "MiniLoaderAll.bin"}) {
      if (fs::is_regular_file(candidate)) {
        loader_file = candidate;
        break;
      }
    }
  }
  if (loader_file.empty() || !fs::is_regular_file(loader_file)) {
    throw BackendError("工程中缺少 Loader 文件");
  }

  return ProjectContext{
      project,
      package_file,
      parameter_file,
      loader_file,
      project / "update.img",
      entries,
  };
}

fs::path StudioBackend::apply_template(const fs::path& template_path, const fs::path& project_dir) const {
  const fs::path source = ResolvePath(template_path);
  if (!fs::is_regular_file(source)) {
    throw BackendError("模板文件不存在");
  }
  const fs::path project = create_project(project_dir);
  const fs::path destination = project / "package-file";
  fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
  return destination;
}

std::vector<PackageEntry> StudioBackend::parse_package_file(const fs::path& package_file) const {
  const fs::path path = ResolvePath(package_file);
  if (!fs::is_regular_file(path)) {
    throw BackendError("未找到 package-file: " + path.string());
  }
  std::vector<PackageEntry> entries;
  for (const auto& raw_line : ReadLines(path)) {
    const std::string line = Trim(raw_line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::sregex_token_iterator iter(line.begin(), line.end(), kPackageFileRegex, -1);
    std::vector<std::string> parts(iter, {});
    if (parts.size() < 2) {
      continue;
    }
    const std::string name = Trim(parts[0]);
    const std::string rel = Trim(line.substr(line.find(parts[1])));
    if (name.empty() || rel.empty()) {
      continue;
    }
    entries.push_back(PackageEntry{name, rel, ToUpper(rel) == "RESERVED" || ToUpper(rel) == "SELF"});
  }
  return entries;
}

std::vector<PackageEntry> StudioBackend::project_entries(const fs::path& project_dir) const {
  return parse_package_file(ResolvePath(project_dir) / "package-file");
}

std::vector<fs::path> StudioBackend::copy_project_sources(
    const fs::path& project_dir,
    const std::map<std::string, std::string>& sources_by_relpath) const {
  const fs::path project = create_project(project_dir);
  std::vector<fs::path> copied;
  for (const auto& [rel_path, source_text] : sources_by_relpath) {
    if (Trim(source_text).empty()) {
      continue;
    }
    const fs::path source = ResolvePath(source_text);
    if (!fs::is_regular_file(source)) {
      throw BackendError("源文件不存在: " + source.string());
    }
    const fs::path destination = project / rel_path;
    fs::create_directories(destination.parent_path());
    if (fs::exists(destination) && fs::equivalent(source, destination)) {
      copied.push_back(destination);
      continue;
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
    copied.push_back(destination);
  }
  return copied;
}

std::pair<std::vector<fs::path>, std::vector<fs::path>> StudioBackend::inspect_project(const fs::path& project_dir) const {
  const fs::path project = create_project(project_dir);
  std::vector<fs::path> missing;
  std::vector<fs::path> existing;
  for (const auto& entry : project_entries(project)) {
    if (!entry.source_required()) {
      continue;
    }
    const fs::path target = project / entry.relative_path;
    if (fs::exists(target)) {
      existing.push_back(target);
    } else {
      missing.push_back(target);
    }
  }
  return {missing, existing};
}

ParameterFileModel StudioBackend::load_parameter_file(const fs::path& parameter_file) const {
  const fs::path path = ResolvePath(parameter_file);
  if (!fs::is_regular_file(path)) {
    throw BackendError("未找到 parameter.txt: " + path.string());
  }

  std::vector<ParameterField> fields;
  std::string cmdline;
  for (const auto& raw_line : ReadLines(path)) {
    const std::string line = Trim(raw_line);
    const std::size_t colon = raw_line.find(':');
    if (line.empty() || line[0] == '#' || colon == std::string::npos) {
      continue;
    }
    ParameterField field{Trim(raw_line.substr(0, colon)), Trim(raw_line.substr(colon + 1))};
    fields.push_back(field);
    if (ToUpper(field.key) == "CMDLINE") {
      cmdline = field.value;
    }
  }

  if (cmdline.empty()) {
    throw BackendError("parameter.txt 中缺少 CMDLINE");
  }

  std::smatch match;
  if (!std::regex_search(cmdline, match, kMtdpartsRegex) || match.size() < 3) {
    throw BackendError("无法从 parameter.txt 解析 mtdparts");
  }
  const std::string target = match[1].str();
  const std::string parts_text = Trim(match[2].str());

  std::vector<PartitionDefinition> partitions;
  std::size_t cursor = 0;
  while (cursor <= parts_text.size()) {
    const std::size_t next = parts_text.find(',', cursor);
    const std::string segment = Trim(parts_text.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor));
    if (!segment.empty()) {
      partitions.push_back(parse_partition_definition(segment));
    }
    if (next == std::string::npos) {
      break;
    }
    cursor = next + 1;
  }

  return ParameterFileModel{path, fields, target, partitions};
}

PartitionDefinition StudioBackend::parse_partition_definition(const std::string& segment) const {
  static const std::regex plain_regex(R"(^([^@]+)@([^(]+)\(([^)]+)\)$)");
  std::smatch match;
  const std::string text = Trim(segment);
  if (!std::regex_match(text, match, plain_regex) || match.size() < 4) {
    throw BackendError("无法解析分区参数: " + segment);
  }

  const std::string size_token = Trim(match[1].str());
  const std::string offset_token = Trim(match[2].str());
  const std::string name_token = Trim(match[3].str());
  const auto [name, flags] = SplitNameToken(name_token);

  std::uint64_t offset = 0;
  try {
    offset = std::stoull(offset_token, nullptr, 0);
  } catch (const std::exception&) {
    throw BackendError("分区偏移非法: " + segment);
  }

  std::optional<std::uint64_t> size;
  if (size_token != "-") {
    try {
      size = std::stoull(size_token, nullptr, 0);
    } catch (const std::exception&) {
      throw BackendError("分区大小非法: " + segment);
    }
  }

  return PartitionDefinition{name, offset, size, flags};
}

std::pair<ParameterFileModel, std::vector<PartitionRow>> StudioBackend::load_partition_rows(
    const fs::path& project_dir,
    const fs::path& parameter_file) const {
  fs::path project;
  fs::path resolved_parameter = parameter_file;
  std::vector<PackageEntry> package_entries;

  if (parameter_file.empty()) {
    const auto context = resolve_project_context(project_dir);
    project = context.project_dir;
    resolved_parameter = context.parameter_file;
    package_entries = context.package_entries;
  } else {
    resolved_parameter = ResolvePath(parameter_file);
    if (!project_dir.empty()) {
      project = ResolvePath(project_dir);
    } else {
      project = resolved_parameter.parent_path();
      if (project.filename() == "Image" && project.has_parent_path()) {
        project = project.parent_path();
      }
    }

    if (!project.empty()) {
      const fs::path package_file = project / "package-file";
      if (fs::is_regular_file(package_file)) {
        try {
          package_entries = parse_package_file(package_file);
        } catch (const std::exception&) {
          package_entries.clear();
        }
      }
    }
  }

  const auto model = load_parameter_file(resolved_parameter);
  std::map<std::string, fs::path> package_map;
  for (const auto& entry : package_entries) {
    package_map[entry.name] = project / entry.relative_path;
  }

  std::vector<fs::path> search_roots;
  auto add_search_root = [&](const fs::path& root) {
    if (root.empty()) {
      return;
    }
    if (std::find(search_roots.begin(), search_roots.end(), root) == search_roots.end()) {
      search_roots.push_back(root);
    }
  };

  add_search_root(project);
  if (!project.empty()) {
    add_search_root(project / "Image");
  }
  const fs::path parameter_dir = model.path.parent_path();
  add_search_root(parameter_dir);
  if (!parameter_dir.empty() && parameter_dir.filename() == "Image" && parameter_dir.has_parent_path()) {
    add_search_root(parameter_dir.parent_path());
  }

  std::vector<PartitionRow> rows;
  for (const auto& part : model.partitions) {
    std::string file_path;
    std::string entry_name;
    if (package_map.count(part.name) > 0) {
      entry_name = part.name;
      const fs::path candidate = package_map[part.name];
      if (fs::is_regular_file(candidate)) {
        file_path = candidate.string();
      }
    }
    if (file_path.empty()) {
      for (const auto& root : search_roots) {
        const fs::path candidate = root / (part.name + ".img");
        if (fs::is_regular_file(candidate)) {
          file_path = candidate.string();
          break;
        }
      }
    }
    const std::uint64_t file_size = (!file_path.empty() && fs::is_regular_file(file_path)) ? fs::file_size(file_path) : 0;
    rows.push_back(PartitionRow{!file_path.empty(), part.name, file_path, file_size, part.to_spec(), entry_name});
  }
  return {model, rows};
}

std::vector<PartitionRow> StudioBackend::refresh_partition_rows(
    const std::vector<PartitionRow>& rows,
    bool apply_file_sizes_to_specs) const {
  std::vector<PartitionDefinition> definitions;
  definitions.reserve(rows.size());
  for (const auto& row : rows) {
    definitions.push_back(parse_partition_definition(row.partition_spec));
  }

  std::uint64_t current_offset = definitions.empty() ? 0 : definitions.front().offset_sectors;
  std::vector<PartitionRow> updated;

  for (std::size_t index = 0; index < rows.size(); ++index) {
    const auto& row = rows[index];
    const auto& definition = definitions[index];
    std::uint64_t file_size = 0;
    const std::string file_path = Trim(row.file_path);
    if (!file_path.empty()) {
      const fs::path candidate = ExpandUser(file_path);
      if (fs::is_regular_file(candidate)) {
        file_size = fs::file_size(candidate);
      }
    }

    std::optional<std::uint64_t> size_sectors = definition.size_sectors;
    if (apply_file_sizes_to_specs && definition.size_sectors.has_value() && file_size > 0) {
      size_sectors = std::max<std::uint64_t>(1, SectorCountForFile(ExpandUser(file_path)));
    }

    const PartitionDefinition current_definition{definition.name, current_offset, size_sectors, definition.flags};
    updated.push_back(
        PartitionRow{row.enabled, current_definition.name, file_path, file_size, current_definition.to_spec(), row.package_entry_name});
    if (current_definition.size_sectors.has_value()) {
      current_offset += *current_definition.size_sectors;
    }
  }
  return updated;
}

std::string StudioBackend::build_parameter_text(const ParameterFileModel& model, const std::vector<PartitionRow>& rows) const {
  std::vector<std::string> partition_specs;
  partition_specs.reserve(rows.size());
  for (const auto& row : rows) {
    partition_specs.push_back(parse_partition_definition(row.partition_spec).to_spec());
  }
  const std::string cmdline_value = "mtdparts=" + model.mtdparts_target + ":" + [&partition_specs]() {
    std::ostringstream stream;
    for (std::size_t i = 0; i < partition_specs.size(); ++i) {
      if (i > 0) {
        stream << ',';
      }
      stream << partition_specs[i];
    }
    return stream.str();
  }();

  std::ostringstream rendered;
  bool cmdline_written = false;
  for (const auto& field : model.fields) {
    if (ToUpper(field.key) == "CMDLINE") {
      rendered << field.key << ": " << cmdline_value << "\n";
      cmdline_written = true;
    } else {
      rendered << field.key << ": " << field.value << "\n";
    }
  }
  if (!cmdline_written) {
    rendered << "CMDLINE: " << cmdline_value << "\n";
  }
  return rendered.str();
}

fs::path StudioBackend::save_parameter_file(
    const ParameterFileModel& model,
    const std::vector<PartitionRow>& rows,
    const fs::path& output_path) const {
  const fs::path target = output_path.empty() ? model.path : ResolvePath(output_path);
  std::ofstream stream(target);
  if (!stream) {
    throw BackendError("无法写入 parameter.txt: " + target.string());
  }
  stream << build_parameter_text(model, rows);
  return target;
}

std::vector<PartitionValidationIssue> StudioBackend::validate_partition_rows(const std::vector<PartitionRow>& rows) const {
  std::vector<PartitionValidationIssue> issues;
  for (const auto& row : rows) {
    if (!row.enabled) {
      continue;
    }
    const std::string file_path = Trim(row.file_path);
    if (file_path.empty()) {
      issues.push_back({row.name, "分区 " + row.name + " 已勾选，但还没有选择镜像文件。"});
      continue;
    }

    const fs::path candidate = ExpandUser(file_path);
    if (!fs::is_regular_file(candidate)) {
      issues.push_back({row.name, "分区 " + row.name + " 镜像不存在: " + candidate.string()});
      continue;
    }

    const auto definition = parse_partition_definition(row.partition_spec);
    if (definition.grow()) {
      continue;
    }

    const std::uint64_t file_size = row.file_size > 0 ? row.file_size : fs::file_size(candidate);
    const std::uint64_t capacity = *definition.size_sectors * kSectorSize;
    if (file_size > capacity) {
      issues.push_back({
          row.name,
          "分区 " + row.name + " 镜像大小 " + FormatFileSize(file_size) + " 超过分区容量 " + FormatFileSize(capacity) +
              "，请调整 mtdparts 参数。",
      });
    }
  }
  return issues;
}

CommandTask StudioBackend::create_di_task_from_rows(
    const fs::path& parameter_file,
    const std::vector<PartitionRow>& rows,
    const std::string& global_args) const {
  std::ostringstream lines;
  bool has_lines = false;
  for (const auto& row : rows) {
    if (row.enabled && !Trim(row.file_path).empty()) {
      if (has_lines) {
        lines << '\n';
      }
      lines << row.name << "=" << Trim(row.file_path);
      has_lines = true;
    }
  }
  if (!has_lines) {
    throw BackendError("至少勾选一个分区并指定镜像文件");
  }
  StringMap values = {
      {"parameter", ResolvePath(parameter_file).string()},
      {"items", lines.str()},
      {"extra_args", ""},
  };
  return create_upgrade_task("DI", values, global_args);
}

CommandTask StudioBackend::create_pack_task(
    const fs::path& project_dir,
    const std::string& chip_code,
    const fs::path& output_file,
    const std::string& os_type,
    const std::string& storage) const {
  const fs::path project = create_project(project_dir);
  const auto entries = project_entries(project);
  if (entries.empty()) {
    throw BackendError("package-file 为空");
  }

  const auto [missing, existing] = inspect_project(project);
  (void)existing;
  if (!missing.empty()) {
    std::ostringstream sample;
    const std::size_t count = std::min<std::size_t>(8, missing.size());
    for (std::size_t index = 0; index < count; ++index) {
      if (index > 0) {
        sample << "\n";
      }
      sample << missing[index].string();
    }
    throw BackendError("工程素材不完整，缺少文件:\n" + sample.str());
  }

  std::string chip = Trim(chip_code);
  while (!chip.empty() && chip.front() == '-') {
    chip.erase(chip.begin());
  }
  if (chip.empty()) {
    throw BackendError("芯片代码不能为空");
  }

  const fs::path output_path = ResolvePath(output_file);
  fs::create_directories(output_path.parent_path());
  const fs::path stage_dir = project / ".rkstudio";
  fs::create_directories(stage_dir);
  const fs::path firmware_img = stage_dir / "firmware.img";

  const fs::path bootloader = project / find_entry(entries, "bootloader");
  if (!fs::is_regular_file(bootloader)) {
    throw BackendError("未找到 bootloader 文件: " + bootloader.string());
  }

  return CommandTask{
      "生成 update.img",
      {
          {"打包 firmware.img", {(paths_.pack_tools / "afptool").string(), "-pack", project.string(), firmware_img.string()}, paths_.pack_tools},
          {"封装 update.img", build_rk_image_maker_pack_args(chip, bootloader, firmware_img, output_path, os_type, storage), paths_.pack_tools},
      },
      {firmware_img},
  };
}

CommandTask StudioBackend::create_unpack_task(const fs::path& update_img, const fs::path& output_dir) const {
  const fs::path update_path = ResolvePath(update_img);
  if (!fs::is_regular_file(update_path)) {
    throw BackendError("待解包的 update.img 不存在");
  }
  const fs::path output = ResolvePath(output_dir);
  fs::create_directories(output);
  const fs::path staging = output / ".rkstudio_unpack";
  fs::create_directories(staging);
  const fs::path firmware_img = staging / "firmware.img";

  return CommandTask{
      "解包 update.img",
      {
          {"拆出 firmware.img", {(paths_.pack_tools / "rkImageMaker").string(), "-unpack", update_path.string(), staging.string()}, paths_.pack_tools},
          {"解包 firmware.img", {(paths_.pack_tools / "afptool").string(), "-unpack", firmware_img.string(), output.string()}, paths_.pack_tools},
      },
      {firmware_img, staging / "boot.bin"},
  };
}

CommandTask StudioBackend::create_merge_task(
    const fs::path& output_file,
    const std::vector<fs::path>& input_files) const {
  std::vector<fs::path> inputs;
  for (const auto& path : input_files) {
    if (!Trim(path.string()).empty()) {
      inputs.push_back(ResolvePath(path));
    }
  }
  if (inputs.size() < 2) {
    throw BackendError("至少需要两个输入固件用于 merge");
  }
  for (const auto& path : inputs) {
    if (!fs::is_regular_file(path)) {
      throw BackendError("未找到输入固件: " + path.string());
    }
  }
  const fs::path output = ResolvePath(output_file);
  fs::create_directories(output.parent_path());
  std::vector<std::string> argv = {(paths_.pack_tools / "rkImageMaker").string(), "-merge", output.string()};
  for (const auto& input : inputs) {
    argv.push_back(input.string());
  }
  return CommandTask{"合并固件", {{"合并多存储固件", argv, paths_.pack_tools}}, {}};
}

CommandTask StudioBackend::create_unmerge_task(const fs::path& input_file, const fs::path& output_dir) const {
  const fs::path input_path = ResolvePath(input_file);
  if (!fs::is_regular_file(input_path)) {
    throw BackendError("待拆分固件不存在");
  }
  const fs::path output = ResolvePath(output_dir);
  fs::create_directories(output);
  return CommandTask{
      "拆分固件",
      {{"拆分多存储固件", {(paths_.pack_tools / "rkImageMaker").string(), "-unmerge", input_path.string(), output.string()}, paths_.pack_tools}},
      {},
  };
}

std::pair<CommandSpec, std::vector<std::string>> StudioBackend::build_upgrade_command(
    const std::string& command_code,
    const StringMap& values,
    const std::string& global_args) const {
  const CommandSpec spec = FindUpgradeCommandSpec(command_code);
  const auto spec_args = BuildUpgradeArgs(command_code, values);
  auto prefix = SplitShellArgs(Trim(global_args));
  prefix.insert(prefix.end(), spec_args.begin(), spec_args.end());
  return {spec, prefix};
}

CommandTask StudioBackend::create_upgrade_task(
    const std::string& command_code,
    const StringMap& values,
    const std::string& global_args) const {
  const auto [spec, args] = build_upgrade_command(command_code, values, global_args);
  std::vector<std::string> argv = {(paths_.upgrade_tools / "upgrade_tool").string()};
  argv.insert(argv.end(), args.begin(), args.end());
  return CommandTask{"upgrade_tool " + spec.code, {{spec.code + " " + spec.label, argv, paths_.upgrade_tools}}, {}};
}

CommandTask StudioBackend::create_raw_tool_task(
    const std::string& tool_key,
    const std::string& args_text,
    const fs::path& workdir) const {
  const auto tool = get_tool(tool_key);
  auto args = SplitShellArgs(args_text);
  const fs::path cwd = workdir.empty() ? tool.default_cwd : ResolvePath(workdir);
  std::vector<std::string> argv = {tool.path.string()};
  argv.insert(argv.end(), args.begin(), args.end());
  return CommandTask{tool.label + " 原生命令", {{tool.label + " 原生命令", argv, cwd}}, {}};
}

DeviceStateSnapshot StudioBackend::detect_device_state() const {
  std::vector<std::string> errors;
  std::vector<AdbDevice> adb_devices;
  std::vector<RockusbDevice> rockusb_devices;

  if (const char* path_env = std::getenv("PATH"); path_env != nullptr) {
    (void)path_env;
  }

  const auto adb_probe = run_probe({"sh", "-lc", "command -v adb"}, paths_.root, 3);
  if (adb_probe.first == 0 && !adb_probe.second.empty()) {
    try {
      adb_devices = parse_adb_devices(run_capture({Trim(adb_probe.second), "devices"}, paths_.root, 3));
    } catch (const BackendError& error) {
      errors.push_back(error.what());
    }
  }

  try {
    rockusb_devices = parse_rockusb_devices(run_capture({(paths_.upgrade_tools / "upgrade_tool").string(), "LD"}, paths_.upgrade_tools, 3));
  } catch (const BackendError& error) {
    errors.push_back(error.what());
  }

  std::string primary = kDeviceStateDisconnected;
  if (!adb_devices.empty()) {
    primary = kDeviceStateAdb;
  } else {
    for (const auto& device : rockusb_devices) {
      if (device.mode == kDeviceStateLoader) {
        primary = kDeviceStateLoader;
        break;
      }
      if (device.mode == kDeviceStateMaskrom) {
        primary = kDeviceStateMaskrom;
      }
    }
  }

  std::vector<std::string> details;
  if (!adb_devices.empty()) {
    std::ostringstream stream;
    stream << "ADB: ";
    for (std::size_t i = 0; i < adb_devices.size(); ++i) {
      if (i > 0) {
        stream << ", ";
      }
      stream << adb_devices[i].serial << "(" << adb_devices[i].state << ")";
    }
    details.push_back(stream.str());
  }
  if (!rockusb_devices.empty()) {
    std::ostringstream stream;
    stream << "Rockusb: ";
    for (std::size_t i = 0; i < rockusb_devices.size(); ++i) {
      if (i > 0) {
        stream << ", ";
      }
      stream << rockusb_devices[i].mode;
      if (!rockusb_devices[i].location_id.empty()) {
        stream << "[" << rockusb_devices[i].location_id << "]";
      }
    }
    details.push_back(stream.str());
  }
  if (details.empty()) {
    details.push_back("未检测到设备");
  }
  if (!errors.empty()) {
    std::ostringstream stream;
    stream << "检测异常: ";
    for (std::size_t i = 0; i < errors.size(); ++i) {
      if (i > 0) {
        stream << " | ";
      }
      stream << errors[i];
    }
    details.push_back(stream.str());
  }

  std::ostringstream last_error;
  for (std::size_t i = 0; i < errors.size(); ++i) {
    if (i > 0) {
      last_error << " | ";
    }
    last_error << errors[i];
  }

  std::ostringstream detail_text;
  for (std::size_t i = 0; i < details.size(); ++i) {
    if (i > 0) {
      detail_text << " | ";
    }
    detail_text << details[i];
  }

  return DeviceStateSnapshot{primary, adb_devices, rockusb_devices, detail_text.str(), last_error.str(), std::chrono::system_clock::now()};
}

std::vector<AdbDevice> StudioBackend::parse_adb_devices(const std::string& output) const {
  std::vector<AdbDevice> devices;
  std::istringstream stream(output);
  for (std::string raw_line; std::getline(stream, raw_line);) {
    const std::string line = Trim(raw_line);
    if (line.empty() || line.rfind("List of devices", 0) == 0 || line.rfind("*", 0) == 0) {
      continue;
    }
    std::istringstream line_stream(line);
    std::string serial;
    std::string state;
    line_stream >> serial >> state;
    if (!serial.empty() && !state.empty()) {
      devices.push_back({serial, state, raw_line});
    }
  }
  return devices;
}

std::vector<RockusbDevice> StudioBackend::parse_rockusb_devices(const std::string& output) const {
  std::vector<RockusbDevice> devices;
  std::istringstream stream(output);
  for (std::string raw_line; std::getline(stream, raw_line);) {
    const std::string line = Trim(raw_line);
    const std::string lowered = ToLower(line);
    if (line.empty()) {
      continue;
    }
    std::string mode;
    if (lowered.find("maskrom") != std::string::npos) {
      mode = kDeviceStateMaskrom;
    } else if (lowered.find("loader") != std::string::npos) {
      mode = kDeviceStateLoader;
    } else {
      continue;
    }

    std::string location_id;
    std::smatch location_match;
    if (std::regex_search(line, location_match, kLocationIdRegex) && location_match.size() >= 2) {
      location_id = location_match[1].str();
    }

    std::string device_no;
    std::smatch device_match;
    if (std::regex_search(line, device_match, kDeviceNoRegex) && device_match.size() >= 2) {
      device_no = device_match[1].str();
    }

    devices.push_back({mode, location_id, device_no, raw_line});
  }
  return devices;
}

std::string StudioBackend::preview_task(const CommandTask& task) const {
  std::ostringstream stream;
  for (std::size_t i = 0; i < task.steps.size(); ++i) {
    if (i > 0) {
      stream << "\n";
    }
    stream << "[" << task.steps[i].label << "] (cwd=" << task.steps[i].cwd.string() << ")\n";
    stream << JoinQuoted(task.steps[i].argv);
  }
  return stream.str();
}

std::optional<PackProfile> StudioBackend::match_profile_for_template(const std::string& template_name) const {
  const std::size_t pos = template_name.find("-package-file");
  const std::string base = pos == std::string::npos ? template_name : template_name.substr(0, pos);
  for (const auto& profile : pack_profiles_) {
    if (profile.name == base) {
      return profile;
    }
  }
  return std::nullopt;
}

StringMap StudioBackend::overview() const {
  return {
      {"pack_profiles", std::to_string(pack_profiles_.size())},
      {"package_templates", std::to_string(package_templates().size())},
      {"pack_tools", paths_.pack_tools.string()},
      {"upgrade_tools", paths_.upgrade_tools.string()},
      {"default_project", paths_.default_project.string()},
      {"logs", paths_.logs.string()},
  };
}

std::string StudioBackend::collect_environment_diagnostics() const {
  std::ostringstream lines;
  const fs::path upgrade_tool = paths_.upgrade_tools / "upgrade_tool";
  lines << "upgrade_tool: " << upgrade_tool.string() << " [" << (fs::is_regular_file(upgrade_tool) ? "OK" : "缺失") << "]\n";
  lines << "afptool: " << (paths_.pack_tools / "afptool").string() << " ["
        << (fs::is_regular_file(paths_.pack_tools / "afptool") ? "OK" : "缺失") << "]\n";
  lines << "rkImageMaker: " << (paths_.pack_tools / "rkImageMaker").string() << " ["
        << (fs::is_regular_file(paths_.pack_tools / "rkImageMaker") ? "OK" : "缺失") << "]\n";
  lines << "/dev/bus/usb: " << (fs::exists("/dev/bus/usb") ? "存在" : "不存在") << "\n";

  const auto adb_which = run_probe({"sh", "-lc", "command -v adb"}, paths_.root, 3);
  lines << "adb: " << (adb_which.first == 0 ? adb_which.second : "未找到") << "\n";
  const auto lsusb_which = run_probe({"sh", "-lc", "command -v lsusb"}, paths_.root, 3);
  lines << "lsusb: " << (lsusb_which.first == 0 ? lsusb_which.second : "未找到") << "\n";

  if (lsusb_which.first == 0 && !lsusb_which.second.empty()) {
    const auto [rc, output] = run_probe({Trim(lsusb_which.second)}, paths_.root, 3);
    lines << "lsusb 返回码: " << rc << "\n";
    std::istringstream stream(output);
    std::vector<std::string> rockchip_lines;
    for (std::string line; std::getline(stream, line);) {
      if (ToLower(line).find("2207:") != std::string::npos) {
        rockchip_lines.push_back(line);
      }
    }
    if (!rockchip_lines.empty()) {
      lines << "lsusb 检测到的 Rockchip 设备:\n";
      for (const auto& line : rockchip_lines) {
        lines << "  " << line << "\n";
      }
    } else {
      lines << "lsusb 未发现 VID 2207 的 Rockchip 设备。\n";
    }
  }

  if (adb_which.first == 0 && !adb_which.second.empty()) {
    const auto [rc, output] = run_probe({Trim(adb_which.second), "devices"}, paths_.root, 3);
    lines << "adb devices 返回码: " << rc << "\n" << (output.empty() ? "(无输出)" : output) << "\n";
  }

  if (fs::is_regular_file(upgrade_tool)) {
    const auto [rc, output] = run_probe({upgrade_tool.string(), "LD"}, paths_.upgrade_tools, 3);
    lines << "upgrade_tool LD 返回码: " << rc << "\n" << (output.empty() ? "(无输出)" : output) << "\n";
  }

  return lines.str();
}

std::string StudioBackend::reboot_adb_device_to_loader() const {
  const auto adb_probe = run_probe({"sh", "-lc", "command -v adb"}, paths_.root, 3);
  if (adb_probe.first != 0 || adb_probe.second.empty()) {
    throw BackendError("未找到 adb，请先安装 adb 并确保它在 PATH 中。");
  }

  const std::string adb_bin = Trim(adb_probe.second);
  const auto devices = parse_adb_devices(run_capture({adb_bin, "devices"}, paths_.root, 3));
  if (devices.empty()) {
    throw BackendError("当前没有检测到 ADB 设备，无法切换到 loader。");
  }

  const auto ready = std::find_if(devices.begin(), devices.end(), [](const AdbDevice& device) {
    return ToLower(device.state) == "device";
  });
  if (ready == devices.end()) {
    std::ostringstream states;
    for (std::size_t i = 0; i < devices.size(); ++i) {
      if (i > 0) {
        states << ", ";
      }
      states << devices[i].serial << "(" << devices[i].state << ")";
    }
    throw BackendError("检测到 ADB 设备，但没有处于可重启状态的 device: " + states.str());
  }

  const auto [rc, output] = run_probe({adb_bin, "-s", ready->serial, "reboot", "bootloader"}, paths_.root, 8);
  if (rc != 0) {
    if (!output.empty()) {
      throw BackendError(Trim(output));
    }
    throw BackendError("adb reboot bootloader 执行失败，返回码: " + std::to_string(rc));
  }
  return ready->serial;
}

std::string StudioBackend::run_capture(const std::vector<std::string>& argv, const fs::path& cwd, int timeout_seconds) const {
  return RunCommand(argv, cwd, timeout_seconds, true).second;
}

std::pair<int, std::string> StudioBackend::run_probe(
    const std::vector<std::string>& argv,
    const fs::path& cwd,
    int timeout_seconds) const {
  try {
    return RunCommand(argv, cwd, timeout_seconds, false);
  } catch (const BackendError& error) {
    return {127, error.what()};
  }
}

std::vector<std::string> StudioBackend::build_rk_image_maker_pack_args(
    const std::string& chip_code,
    const fs::path& bootloader,
    const fs::path& firmware_img,
    const fs::path& output_path,
    const std::string& os_type,
    const std::string& storage) const {
  std::vector<std::string> args = {
      (paths_.pack_tools / "rkImageMaker").string(),
      "-" + chip_code,
      bootloader.string(),
      firmware_img.string(),
      output_path.string(),
      "-os_type:" + ToUpper(os_type),
  };
  if (!Trim(storage).empty()) {
    args.push_back("-storage:" + ToUpper(storage));
  }
  return args;
}

std::string StudioBackend::find_entry(const std::vector<PackageEntry>& entries, const std::string& name) const {
  for (const auto& entry : entries) {
    if (entry.name == name) {
      return entry.relative_path;
    }
  }
  throw BackendError("package-file 中缺少 " + name + " 条目");
}

std::vector<PackProfile> StudioBackend::load_pack_profiles() const {
  std::vector<PackProfile> profiles;
  const std::regex pattern(R"(rkImageMaker\s+-([A-Za-z0-9]+)\s+Image/MiniLoaderAll\.bin.*?-os_type:([A-Za-z0-9]+))", std::regex::icase);
  for (const auto& entry : fs::directory_iterator(paths_.pack_tools)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (name.size() < 12 || name.rfind("-mkupdate.sh") == std::string::npos) {
      continue;
    }
    const std::string text = ReadText(entry.path());
    std::smatch match;
    if (!std::regex_search(text, match, pattern) || match.size() < 3) {
      continue;
    }
    profiles.push_back(
        PackProfile{name.substr(0, name.find("-mkupdate.sh")), match[1].str(), entry.path(), ToUpper(match[2].str())});
  }
  std::sort(profiles.begin(), profiles.end(), [](const PackProfile& left, const PackProfile& right) {
    return left.name < right.name;
  });
  return profiles;
}

}  // namespace rkstudio
