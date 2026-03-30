#include "rkstudio/ui_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace rkstudio {

namespace {

std::string JsonEscape(const std::string& text) {
  std::ostringstream stream;
  for (unsigned char ch : text) {
    switch (ch) {
      case '\\':
        stream << "\\\\";
        break;
      case '"':
        stream << "\\\"";
        break;
      case '\n':
        stream << "\\n";
        break;
      case '\r':
        stream << "\\r";
        break;
      case '\t':
        stream << "\\t";
        break;
      default:
        if (ch < 0x20) {
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch) << std::dec;
        } else {
          stream << ch;
        }
        break;
    }
  }
  return stream.str();
}

std::string JsonString(const std::string& text) {
  return "\"" + JsonEscape(text) + "\"";
}

std::string ReadTextFile(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw BackendError("无法读取资源: " + path.string());
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::string UrlDecode(const std::string& input) {
  std::ostringstream decoded;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const std::string hex = input.substr(i + 1, 2);
      char* end = nullptr;
      const long value = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        decoded << static_cast<char>(value);
        i += 2;
        continue;
      }
    }
    if (input[i] == '+') {
      decoded << ' ';
    } else {
      decoded << input[i];
    }
  }
  return decoded.str();
}

std::pair<std::string, std::map<std::string, std::string>> ParsePathAndQuery(const std::string& target) {
  const std::size_t question = target.find('?');
  const std::string path = question == std::string::npos ? target : target.substr(0, question);
  std::map<std::string, std::string> query;
  if (question == std::string::npos) {
    return {path, query};
  }
  const std::string query_text = target.substr(question + 1);
  std::size_t cursor = 0;
  while (cursor <= query_text.size()) {
    const std::size_t next = query_text.find('&', cursor);
    const std::string piece = query_text.substr(cursor, next == std::string::npos ? std::string::npos : next - cursor);
    if (!piece.empty()) {
      const std::size_t eq = piece.find('=');
      const std::string key = UrlDecode(piece.substr(0, eq));
      const std::string value = eq == std::string::npos ? "" : UrlDecode(piece.substr(eq + 1));
      query[key] = value;
    }
    if (next == std::string::npos) {
      break;
    }
    cursor = next + 1;
  }
  return {path, query};
}

std::string DetectContentType(const fs::path& path) {
  const std::string ext = path.extension().string();
  if (ext == ".html") {
    return "text/html; charset=utf-8";
  }
  if (ext == ".css") {
    return "text/css; charset=utf-8";
  }
  if (ext == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (ext == ".svg") {
    return "image/svg+xml";
  }
  if (ext == ".json") {
    return "application/json; charset=utf-8";
  }
  return "text/plain; charset=utf-8";
}

std::string JoinJsonArray(const std::vector<std::string>& items) {
  std::ostringstream stream;
  stream << "[";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    stream << items[i];
  }
  stream << "]";
  return stream.str();
}

std::string BuildOverviewJson(const StudioBackend& backend) {
  const auto overview = backend.overview();
  std::vector<std::string> entries;
  for (const auto& [key, value] : overview) {
    entries.push_back(JsonString(key) + ":" + JsonString(value));
  }

  std::vector<std::string> profile_items;
  for (const auto& profile : backend.pack_profiles()) {
    profile_items.push_back(
        "{"
        "\"name\":" + JsonString(profile.name) + "," +
        "\"chipCode\":" + JsonString(profile.chip_code) + "," +
        "\"osType\":" + JsonString(profile.default_os_type) + "," +
        "\"scriptPath\":" + JsonString(profile.script_path.string()) +
        "}");
  }

  std::vector<std::string> template_items;
  for (const auto& item : backend.package_templates()) {
    template_items.push_back(JsonString(item.string()));
  }

  std::ostringstream stream;
  stream << "{";
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) {
      stream << ",";
    }
    stream << entries[i];
  }
  if (!entries.empty()) {
    stream << ",";
  }
  stream << "\"profiles\":" << JoinJsonArray(profile_items) << ",";
  stream << "\"templates\":" << JoinJsonArray(template_items);
  stream << "}";
  return stream.str();
}

std::string BuildDeviceStateJson(const StudioBackend& backend) {
  const auto snapshot = backend.detect_device_state();
  std::vector<std::string> adb_devices;
  for (const auto& device : snapshot.adb_devices) {
    adb_devices.push_back(
        "{"
        "\"serial\":" + JsonString(device.serial) + "," +
        "\"state\":" + JsonString(device.state) + "," +
        "\"raw\":" + JsonString(device.raw_line) +
        "}");
  }
  std::vector<std::string> rockusb_devices;
  for (const auto& device : snapshot.rockusb_devices) {
    rockusb_devices.push_back(
        "{"
        "\"mode\":" + JsonString(device.mode) + "," +
        "\"locationId\":" + JsonString(device.location_id) + "," +
        "\"deviceNo\":" + JsonString(device.device_no) + "," +
        "\"raw\":" + JsonString(device.raw_line) +
        "}");
  }

  const auto epoch = std::chrono::system_clock::to_time_t(snapshot.updated_at);
  std::ostringstream ts;
  ts << std::put_time(std::localtime(&epoch), "%F %T");

  return "{"
         "\"primaryState\":" + JsonString(snapshot.primary_state) + "," +
         "\"detailText\":" + JsonString(snapshot.detail_text) + "," +
         "\"lastError\":" + JsonString(snapshot.last_error) + "," +
         "\"updatedAt\":" + JsonString(ts.str()) + "," +
         "\"adbDevices\":" + JoinJsonArray(adb_devices) + "," +
         "\"rockusbDevices\":" + JoinJsonArray(rockusb_devices) +
         "}";
}

std::string BuildErrorJson(const std::string& message) {
  return "{"
         "\"ok\":false,"
         "\"error\":" + JsonString(message) +
         "}";
}

std::string BuildFlashCenterJson(const StudioBackend& backend, const std::string& project_query) {
  const fs::path project = project_query.empty() ? backend.paths().default_project : fs::path(project_query);
  ProjectContext context;
  ParameterFileModel model;
  std::vector<PartitionRow> rows;
  std::vector<PartitionValidationIssue> issues;

  try {
    context = backend.resolve_project_context(project);
    std::tie(model, rows) = backend.load_partition_rows(context.project_dir);
    issues = backend.validate_partition_rows(rows);
  } catch (const std::exception&) {
    context.project_dir = project;
    context.package_file = project / "package-file";
    context.parameter_file = project / "Image" / "parameter.txt";
    context.loader_file = project / "Image" / "MiniLoaderAll.bin";
    context.update_image = project / "update.img";
    issues.push_back(
        {"", "当前工程还没有准备完整。请先应用 package-file 模板，或在上方工程目录里切换到一个已有工程。"});
  }

  std::vector<std::string> row_items;
  for (const auto& row : rows) {
    row_items.push_back(
        "{"
        "\"enabled\":" + std::string(row.enabled ? "true" : "false") + "," +
        "\"name\":" + JsonString(row.name) + "," +
        "\"filePath\":" + JsonString(row.file_path) + "," +
        "\"fileSize\":" + std::to_string(row.file_size) + "," +
        "\"fileSizeText\":" + JsonString(FormatFileSize(row.file_size)) + "," +
        "\"partitionSpec\":" + JsonString(row.partition_spec) + "," +
        "\"packageEntryName\":" + JsonString(row.package_entry_name) +
        "}");
  }

  std::vector<std::string> issue_items;
  for (const auto& issue : issues) {
    issue_items.push_back(
        "{"
        "\"partitionName\":" + JsonString(issue.partition_name) + "," +
        "\"message\":" + JsonString(issue.message) +
        "}");
  }

  std::map<std::string, std::string> previews = {
      {"UF", "请先准备有效的 update.img 工程。"},
      {"DB", "请先准备 Loader 文件。"},
      {"UL", "请先准备 Loader 文件。"},
      {"EF", "请先准备 Loader 文件。"},
      {"DI", "请先准备 parameter.txt 和分区镜像。"},
  };
  if (!rows.empty()) {
    try {
      previews["UF"] = backend.preview_task(
          backend.create_upgrade_task("UF", {{"firmware", context.update_image.string()}, {"noreset", "false"}}, ""));
    } catch (const std::exception& error) {
      previews["UF"] = error.what();
    }
    try {
      previews["DB"] = backend.preview_task(backend.create_upgrade_task("DB", {{"loader", context.loader_file.string()}}, ""));
    } catch (const std::exception& error) {
      previews["DB"] = error.what();
    }
    try {
      previews["UL"] = backend.preview_task(
          backend.create_upgrade_task("UL", {{"loader", context.loader_file.string()}, {"storage", ""}, {"noreset", "false"}}, ""));
    } catch (const std::exception& error) {
      previews["UL"] = error.what();
    }
    try {
      previews["EF"] = backend.preview_task(
          backend.create_upgrade_task("EF", {{"loader_or_firmware", context.loader_file.string()}}, ""));
    } catch (const std::exception& error) {
      previews["EF"] = error.what();
    }
    try {
      previews["DI"] = backend.preview_task(backend.create_di_task_from_rows(context.parameter_file, rows, ""));
    } catch (const std::exception& error) {
      previews["DI"] = error.what();
    }
  }

  std::vector<std::string> preview_items;
  for (const auto& [key, value] : previews) {
    preview_items.push_back(JsonString(key) + ":" + JsonString(value));
  }

  std::ostringstream parameter_fields;
  parameter_fields << "[";
  for (std::size_t i = 0; i < model.fields.size(); ++i) {
    if (i > 0) {
      parameter_fields << ",";
    }
    parameter_fields << "{"
                     << "\"key\":" << JsonString(model.fields[i].key) << ","
                     << "\"value\":" << JsonString(model.fields[i].value)
                     << "}";
  }
  parameter_fields << "]";

  std::ostringstream preview_json;
  preview_json << "{";
  for (std::size_t i = 0; i < preview_items.size(); ++i) {
    if (i > 0) {
      preview_json << ",";
    }
    preview_json << preview_items[i];
  }
  preview_json << "}";

  return "{"
         "\"ok\":true,"
         "\"projectDir\":" + JsonString(context.project_dir.string()) + "," +
         "\"packageFile\":" + JsonString(context.package_file.string()) + "," +
         "\"parameterFile\":" + JsonString(context.parameter_file.string()) + "," +
         "\"loaderFile\":" + JsonString(context.loader_file.string()) + "," +
         "\"updateImage\":" + JsonString(context.update_image.string()) + "," +
         "\"mtdpartsTarget\":" + JsonString(model.mtdparts_target) + "," +
         "\"parameterFields\":" + parameter_fields.str() + "," +
         "\"rows\":" + JoinJsonArray(row_items) + "," +
         "\"issues\":" + JoinJsonArray(issue_items) + "," +
         "\"previews\":" + preview_json.str() +
         "}";
}

std::string BuildDiagnosticsJson(const StudioBackend& backend) {
  return "{"
         "\"text\":" + JsonString(backend.collect_environment_diagnostics()) +
         "}";
}

void SendResponse(int client_fd, const std::string& status, const std::string& content_type, const std::string& body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << "\r\n";
  response << "Content-Type: " << content_type << "\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "Cache-Control: no-store\r\n";
  response << "Connection: close\r\n\r\n";
  response << body;
  const std::string text = response.str();
  send(client_fd, text.data(), text.size(), 0);
}

void HandleClient(int client_fd, const StudioBackend& backend) {
  std::array<char, 8192> buffer{};
  const ssize_t count = recv(client_fd, buffer.data(), buffer.size() - 1, 0);
  if (count <= 0) {
    close(client_fd);
    return;
  }
  const std::string request(buffer.data(), static_cast<std::size_t>(count));
  std::istringstream request_stream(request);
  std::string method;
  std::string target;
  std::string version;
  request_stream >> method >> target >> version;

  if (method != "GET") {
    SendResponse(client_fd, "405 Method Not Allowed", "text/plain; charset=utf-8", "Only GET is supported");
    close(client_fd);
    return;
  }

  const auto [path, query] = ParsePathAndQuery(target);

  try {
    if (path == "/" || path == "/index.html") {
      const fs::path file = fs::path(RKSTUDIO_SOURCE_ROOT) / "cpp" / "ui" / "index.html";
      SendResponse(client_fd, "200 OK", DetectContentType(file), ReadTextFile(file));
      close(client_fd);
      return;
    }
    if (path == "/styles.css" || path == "/app.js") {
      const fs::path file = fs::path(RKSTUDIO_SOURCE_ROOT) / "cpp" / "ui" / path.substr(1);
      SendResponse(client_fd, "200 OK", DetectContentType(file), ReadTextFile(file));
      close(client_fd);
      return;
    }
    if (path.rfind("/assets/", 0) == 0) {
      const fs::path file = fs::path(RKSTUDIO_SOURCE_ROOT) / "rkstudio" / path.substr(1);
      SendResponse(client_fd, "200 OK", DetectContentType(file), ReadTextFile(file));
      close(client_fd);
      return;
    }
    if (path == "/api/overview") {
      SendResponse(client_fd, "200 OK", "application/json; charset=utf-8", BuildOverviewJson(backend));
      close(client_fd);
      return;
    }
    if (path == "/api/device-state") {
      SendResponse(client_fd, "200 OK", "application/json; charset=utf-8", BuildDeviceStateJson(backend));
      close(client_fd);
      return;
    }
    if (path == "/api/diagnostics") {
      SendResponse(client_fd, "200 OK", "application/json; charset=utf-8", BuildDiagnosticsJson(backend));
      close(client_fd);
      return;
    }
    if (path == "/api/flash-center") {
      try {
        const std::string project = query.count("project") > 0 ? query.at("project") : "";
        SendResponse(client_fd, "200 OK", "application/json; charset=utf-8", BuildFlashCenterJson(backend, project));
      } catch (const std::exception& error) {
        SendResponse(client_fd, "200 OK", "application/json; charset=utf-8", BuildErrorJson(error.what()));
      }
      close(client_fd);
      return;
    }

    SendResponse(client_fd, "404 Not Found", "text/plain; charset=utf-8", "Not Found");
  } catch (const std::exception& error) {
    SendResponse(client_fd, "500 Internal Server Error", "text/plain; charset=utf-8", error.what());
  }
  close(client_fd);
}

}  // namespace

int RunUiServer(const StudioBackend& backend, int port) {
  const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    throw BackendError("无法创建本地服务 socket");
  }

  int reuse = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(static_cast<std::uint16_t>(port));

  if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(server_fd);
    throw BackendError("端口绑定失败: " + std::to_string(port));
  }
  if (listen(server_fd, 16) != 0) {
    close(server_fd);
    throw BackendError("本地服务 listen 失败");
  }

  std::cout << "RK Firmware Studio C++ UI\n";
  std::cout << "Open http://127.0.0.1:" << port << "\n";
  std::cout << "Press Ctrl+C to stop.\n";

  while (true) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
      continue;
    }
    std::thread([client_fd, &backend]() { HandleClient(client_fd, backend); }).detach();
  }

  close(server_fd);
  return 0;
}

}  // namespace rkstudio
