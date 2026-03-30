#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace rkstudio {

constexpr int kSectorSize = 512;
constexpr const char* kDeviceStateDisconnected = "未连接";
constexpr const char* kDeviceStateMaskrom = "maskrom";
constexpr const char* kDeviceStateLoader = "loader";
constexpr const char* kDeviceStateAdb = "adb";

using StringMap = std::map<std::string, std::string>;

struct PackageEntry {
  std::string name;
  std::string relative_path;
  bool is_reserved = false;

  [[nodiscard]] bool source_required() const;
};

struct PackProfile {
  std::string name;
  std::string chip_code;
  std::filesystem::path script_path;
  std::string default_os_type = "ANDROIDOS";

  [[nodiscard]] std::string display_name() const;
};

struct CommandStep {
  std::string label;
  std::vector<std::string> argv;
  std::filesystem::path cwd;
};

struct CommandTask {
  std::string label;
  std::vector<CommandStep> steps;
  std::vector<std::filesystem::path> cleanup_paths;
};

struct ToolDescriptor {
  std::string key;
  std::string label;
  std::filesystem::path path;
  std::filesystem::path default_cwd;
};

struct ParameterField {
  std::string key;
  std::string value;
};

struct PartitionDefinition {
  std::string name;
  std::uint64_t offset_sectors = 0;
  std::optional<std::uint64_t> size_sectors;
  std::vector<std::string> flags;

  [[nodiscard]] bool grow() const;
  [[nodiscard]] std::string name_token() const;
  [[nodiscard]] std::string to_spec() const;
};

struct PartitionRow {
  bool enabled = false;
  std::string name;
  std::string file_path;
  std::uint64_t file_size = 0;
  std::string partition_spec;
  std::string package_entry_name;
};

struct PartitionValidationIssue {
  std::string partition_name;
  std::string message;
};

struct ParameterFileModel {
  std::filesystem::path path;
  std::vector<ParameterField> fields;
  std::string mtdparts_target;
  std::vector<PartitionDefinition> partitions;
};

struct ProjectContext {
  std::filesystem::path project_dir;
  std::filesystem::path package_file;
  std::filesystem::path parameter_file;
  std::filesystem::path loader_file;
  std::filesystem::path update_image;
  std::vector<PackageEntry> package_entries;
};

struct RockusbDevice {
  std::string mode;
  std::string location_id;
  std::string device_no;
  std::string raw_line;
};

struct AdbDevice {
  std::string serial;
  std::string state;
  std::string raw_line;
};

struct DeviceStateSnapshot {
  std::string primary_state = kDeviceStateDisconnected;
  std::vector<AdbDevice> adb_devices;
  std::vector<RockusbDevice> rockusb_devices;
  std::string detail_text;
  std::string last_error;
  std::chrono::system_clock::time_point updated_at = std::chrono::system_clock::now();
};

}  // namespace rkstudio
