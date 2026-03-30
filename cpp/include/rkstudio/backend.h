#pragma once

#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "rkstudio/paths.h"
#include "rkstudio/specs.h"
#include "rkstudio/types.h"

namespace rkstudio {

class BackendError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class StudioBackend {
 public:
  explicit StudioBackend(AppPaths paths);

  [[nodiscard]] const AppPaths& paths() const;
  [[nodiscard]] const std::vector<PackProfile>& pack_profiles() const;

  [[nodiscard]] std::vector<std::filesystem::path> package_templates() const;
  [[nodiscard]] std::vector<ToolDescriptor> tool_descriptors() const;
  [[nodiscard]] ToolDescriptor get_tool(const std::string& key) const;

  std::filesystem::path create_project(const std::filesystem::path& project_dir) const;
  ProjectContext resolve_project_context(const std::filesystem::path& project_dir) const;
  std::filesystem::path apply_template(const std::filesystem::path& template_path, const std::filesystem::path& project_dir) const;

  std::vector<PackageEntry> parse_package_file(const std::filesystem::path& package_file) const;
  std::vector<PackageEntry> project_entries(const std::filesystem::path& project_dir) const;
  std::vector<std::filesystem::path> copy_project_sources(
      const std::filesystem::path& project_dir,
      const std::map<std::string, std::string>& sources_by_relpath) const;
  std::pair<std::vector<std::filesystem::path>, std::vector<std::filesystem::path>> inspect_project(
      const std::filesystem::path& project_dir) const;

  ParameterFileModel load_parameter_file(const std::filesystem::path& parameter_file) const;
  PartitionDefinition parse_partition_definition(const std::string& segment) const;
  std::pair<ParameterFileModel, std::vector<PartitionRow>> load_partition_rows(
      const std::filesystem::path& project_dir,
      const std::filesystem::path& parameter_file = {}) const;
  std::vector<PartitionRow> refresh_partition_rows(
      const std::vector<PartitionRow>& rows,
      bool apply_file_sizes_to_specs) const;
  std::string build_parameter_text(const ParameterFileModel& model, const std::vector<PartitionRow>& rows) const;
  std::filesystem::path save_parameter_file(
      const ParameterFileModel& model,
      const std::vector<PartitionRow>& rows,
      const std::filesystem::path& output_path = {}) const;
  std::vector<PartitionValidationIssue> validate_partition_rows(const std::vector<PartitionRow>& rows) const;

  CommandTask create_di_task_from_rows(
      const std::filesystem::path& parameter_file,
      const std::vector<PartitionRow>& rows,
      const std::string& global_args = "") const;
  CommandTask create_pack_task(
      const std::filesystem::path& project_dir,
      const std::string& chip_code,
      const std::filesystem::path& output_file,
      const std::string& os_type = "ANDROIDOS",
      const std::string& storage = "") const;
  CommandTask create_unpack_task(const std::filesystem::path& update_img, const std::filesystem::path& output_dir) const;
  CommandTask create_merge_task(
      const std::filesystem::path& output_file,
      const std::vector<std::filesystem::path>& input_files) const;
  CommandTask create_unmerge_task(const std::filesystem::path& input_file, const std::filesystem::path& output_dir) const;
  std::pair<CommandSpec, std::vector<std::string>> build_upgrade_command(
      const std::string& command_code,
      const StringMap& values,
      const std::string& global_args = "") const;
  CommandTask create_upgrade_task(
      const std::string& command_code,
      const StringMap& values,
      const std::string& global_args = "") const;
  CommandTask create_raw_tool_task(
      const std::string& tool_key,
      const std::string& args_text,
      const std::filesystem::path& workdir = {}) const;

  DeviceStateSnapshot detect_device_state() const;
  std::vector<AdbDevice> parse_adb_devices(const std::string& output) const;
  std::vector<RockusbDevice> parse_rockusb_devices(const std::string& output) const;
  std::string preview_task(const CommandTask& task) const;
  std::optional<PackProfile> match_profile_for_template(const std::string& template_name) const;
  StringMap overview() const;
  std::string collect_environment_diagnostics() const;
  std::string reboot_adb_device_to_loader() const;

 private:
 AppPaths paths_;
  std::vector<PackProfile> pack_profiles_;
  mutable std::optional<std::vector<std::filesystem::path>> package_templates_cache_;

  std::string run_capture(
      const std::vector<std::string>& argv,
      const std::filesystem::path& cwd,
      int timeout_seconds) const;
  std::pair<int, std::string> run_probe(
      const std::vector<std::string>& argv,
      const std::filesystem::path& cwd,
      int timeout_seconds) const;
  std::vector<std::string> build_rk_image_maker_pack_args(
      const std::string& chip_code,
      const std::filesystem::path& bootloader,
      const std::filesystem::path& firmware_img,
      const std::filesystem::path& output_path,
      const std::string& os_type,
      const std::string& storage) const;
  std::string find_entry(const std::vector<PackageEntry>& entries, const std::string& name) const;
  std::vector<PackProfile> load_pack_profiles() const;
};

std::string FormatFileSize(std::uint64_t size);

}  // namespace rkstudio
