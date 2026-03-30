#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "rkstudio/backend.h"
#include "rkstudio/paths.h"
#include "rkstudio/qt_window.h"
#include "rkstudio/specs.h"
#include "rkstudio/ui_server.h"

namespace fs = std::filesystem;

namespace rkstudio {

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    throw BackendError(message);
  }
}

int RunSmokeTests(const StudioBackend& backend) {
  const fs::path temp_root = fs::temp_directory_path() / "rkstudio_cpp_smoke";
  fs::remove_all(temp_root);
  fs::create_directories(temp_root / "Image");

  {
    std::ofstream package_file(temp_root / "package-file");
    package_file << "package-file package-file\n";
    package_file << "bootloader Image/MiniLoaderAll.bin\n";
    package_file << "parameter Image/parameter.txt\n";
    package_file << "boot Image/boot.img\n";
  }
  {
    std::ofstream parameter(temp_root / "Image" / "parameter.txt");
    parameter << "CMDLINE: mtdparts=:0x00000002@0x00000020(boot),-@0x00000022(userdata:grow)\n";
  }
  {
    std::ofstream loader(temp_root / "Image" / "MiniLoaderAll.bin", std::ios::binary);
    loader << "loader";
  }
  {
    std::ofstream boot(temp_root / "Image" / "boot.img", std::ios::binary);
    boot << std::string(1024, 'x');
  }

  const auto entries = backend.parse_package_file(temp_root / "package-file");
  Expect(entries.size() == 4, "package-file 解析失败");

  const auto [model, rows] = backend.load_partition_rows(temp_root);
  (void)model;
  Expect(rows.size() == 2, "分区行解析失败");
  Expect(rows.front().name == "boot", "boot 分区缺失");

  const auto updated_rows = backend.refresh_partition_rows(rows, true);
  Expect(updated_rows.front().partition_spec == "0x00000002@0x00000020(boot)", "分区大小刷新异常");

  const fs::path saved_parameter = temp_root / "Image" / "parameter_saved.txt";
  backend.save_parameter_file(model, updated_rows, saved_parameter);
  const auto saved_model = backend.load_parameter_file(saved_parameter);
  Expect(saved_model.partitions.size() == 2, "parameter.txt 写回失败");
  Expect(saved_model.partitions.front().name == "boot", "写回后 boot 分区缺失");

  const auto di_task = backend.create_di_task_from_rows(temp_root / "Image" / "parameter.txt", updated_rows, "-s 5");
  Expect(!di_task.steps.empty(), "DI 任务构造失败");
  Expect(di_task.steps.front().argv.size() >= 5, "DI 参数不足");

  const auto rockusb = backend.parse_rockusb_devices("DevNo=1 Vid=0x2207,Pid=0x350b,LocationID=1401 Loader");
  Expect(rockusb.size() == 1 && rockusb.front().mode == kDeviceStateLoader, "Rockusb 解析失败");
  Expect(rockusb.front().location_id == "1401", "Rockusb LocationID 解析失败");

  const auto adb = backend.parse_adb_devices("List of devices attached\nSERIAL123\tdevice\nZXCVBN\toffline\n");
  Expect(adb.size() == 2 && adb.front().serial == "SERIAL123" && adb.back().state == "offline", "ADB 解析失败");

  const auto uf_args = BuildUpgradeArgs("UF", {{"firmware", "/tmp/update.img"}, {"noreset", "true"}});
  Expect(uf_args.size() == 3 && uf_args[0] == "UF" && uf_args[2] == "-noreset", "UF 命令规格构造失败");

  const auto ef_args = BuildUpgradeArgs("EF", {{"loader_or_firmware", "/tmp/MiniLoaderAll.bin"}});
  Expect(ef_args.size() == 2 && ef_args[0] == "EF", "EF 命令规格构造失败");

  const auto di_args =
      BuildUpgradeArgs("DI", {{"parameter", "/tmp/parameter.txt"}, {"items", "boot=/tmp/boot.img\nvendor=/tmp/vendor.img"}});
  Expect(di_args.size() == 7 && di_args[0] == "DI" && di_args[1] == "-p" && di_args[3] == "-b" && di_args[5] == "-vendor" &&
             di_args[6] == "/tmp/vendor.img",
         "DI 命令规格构造失败");

  {
    const fs::path relaxed_root = temp_root / "relaxed_parameter_only";
    fs::create_directories(relaxed_root / "Image");
    {
      std::ofstream parameter(relaxed_root / "Image" / "parameter.txt");
      parameter << "CMDLINE: mtdparts=:0x00000002@0x00000020(boot),-@0x00000022(rootfs:grow)\n";
    }
    {
      std::ofstream boot(relaxed_root / "Image" / "boot.img", std::ios::binary);
      boot << std::string(2048, 'b');
    }

    const auto [relaxed_model, relaxed_rows] =
        backend.load_partition_rows(relaxed_root, relaxed_root / "Image" / "parameter.txt");
    Expect(relaxed_model.partitions.size() == 2, "parameter-only 分区解析失败");
    Expect(relaxed_rows.size() == 2, "parameter-only 分区行数量异常");
    Expect(relaxed_rows.front().enabled, "parameter-only 场景下未匹配到 boot.img");
    Expect(relaxed_rows.front().file_size == 2048, "parameter-only 场景下 boot.img 大小异常");
  }

  fs::remove_all(temp_root);
  std::cout << "smoke test ok\n";
  return 0;
}

}  // namespace

}  // namespace rkstudio

int main(int argc, char** argv) {
  try {
    auto paths = rkstudio::DiscoverPaths();
    rkstudio::EnsureAppDirs(paths);
    rkstudio::StudioBackend backend(paths);

    if (argc > 1 && std::string(argv[1]) == "--smoke-test") {
      return rkstudio::RunSmokeTests(backend);
    }
    if (argc > 1 && std::string(argv[1]) == "--qt-smoke-test") {
      return rkstudio::RunQtApp(argc, argv, backend, true);
    }
    if (argc > 2 && std::string(argv[1]) == "--qt-capture") {
      return rkstudio::RunQtApp(argc, argv, backend, false, argv[2]);
    }
    if (argc > 3 && std::string(argv[1]) == "--qt-capture-nav") {
      return rkstudio::RunQtApp(argc, argv, backend, false, argv[3], std::atoi(argv[2]));
    }
    if (argc > 1 && std::string(argv[1]) == "--qt-generate-help-assets") {
      return rkstudio::GenerateHelpGuideAssets(argc, argv, backend);
    }
    if (argc > 1 && std::string(argv[1]) == "--overview") {
      const auto overview = backend.overview();
      for (const auto& [key, value] : overview) {
        std::cout << key << ": " << value << "\n";
      }
      return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--web") {
      int port = 8080;
      if (const char* env_port = std::getenv("RKSTUDIO_PORT"); env_port != nullptr && *env_port != '\0') {
        port = std::atoi(env_port);
      }
      if (argc > 3 && std::string(argv[2]) == "--port") {
        port = std::atoi(argv[3]);
      }
      return rkstudio::RunUiServer(backend, port);
    }

    return rkstudio::RunQtApp(argc, argv, backend, false);
  } catch (const std::exception& error) {
    std::cerr << "rkstudio_cpp error: " << error.what() << "\n";
    return 1;
  }
}
