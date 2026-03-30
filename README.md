# RK Firmware Studio

Rockchip 固件工程桌面工作台，主线实现已经统一为 `C++17 + Qt Widgets`。这个项目的目标不是重写 Rockchip 底层协议，而是在 Linux 桌面上提供一套更稳定、可视化、可编辑、适合量产与调试并存的固件工程操作界面。

## 项目定位

RK Firmware Studio 面向以下使用场景：

- 整包烧录 `update.img`
- 分区级烧录与局部调试
- Loader / Maskrom 救砖流程
- `parameter.txt` 解析、分区尺寸调整、回写
- `update.img` 打包、解包、拆分、合并
- 设备连接状态查看、模式切换、任务日志观察

它本质上是 Rockchip 官方工具链的图形化调度层。真正执行烧录、打包、解包的是这些外部工具：

- `upgrade_tool`
- `afptool`
- `rkImageMaker`

## 当前实现状态

- 默认桌面入口：原生 C++ Qt 程序 `rkstudio_cpp`
- 回退入口：C++ 本地 HTTP 服务 + 浏览器壳
- 默认自检：C++ smoke test + Qt offscreen smoke test
- 仓库已经清理为 C++ 主线，不再保留旧版 Python 实现和 Python 测试

## 主要能力

- 烧录中心：`UF` `DB` `UL` `EF` `DI`
- 打包解包：生成 `update.img`、解包、合并、拆分
- 设备信息：状态检测、`ADB -> Loader`、只读查询、环境诊断
- 帮助说明：截图式引导、模式说明、本地文档路径
- 工程解析：`package-file`、`parameter.txt`、镜像映射、分区表
- 分区编辑：容量修改、镜像大小重算、`rootfs` 等关键分区写回
- 日志与进度：任务阶段百分比、增量日志输出、错误提示

## 仓库内容与 GitHub 约定

这个仓库只保存“程序本身”和“运行它所需的通用工具”，不保存任何用户自己的固件成品或解包结果。

会保留在仓库里的内容：

- C++ 源码与前端静态资源
- 图标、帮助页素材、文档
- 官方打包/升级工具及模板
- 启动脚本、桌面入口、构建文件

不会提交到 GitHub 的内容：

- 任意 `update.img`
- 任意分区镜像，例如 `boot.img`、`rootfs.img`、`userdata.img`
- `MiniLoaderAll.bin` 这类板级固件文件
- 解包目录、临时工程目录、缓存目录
- 从外部固件包导入出来的 `parameter.txt`
- 本地压缩包、量产包、中间产物

仓库已经通过 `.gitignore` 明确屏蔽这些运行时/固件产物，默认不把解包内容和镜像包纳入版本控制。

## 目录结构

- `cpp/`
  当前 C++ 源码与前端静态资源。
- `cpp/include/rkstudio/`
  后端接口、数据结构、路径管理、UI 桥接声明。
- `cpp/src/`
  Qt 主窗口、任务后端、工程解析、命令调度实现。
- `cpp/ui/`
  浏览器壳回退模式使用的静态页面资源。
- `rkstudio/assets/`
  图标、状态资源、帮助页截图素材。
- `tools/pack/`
  `afptool`、`rkImageMaker`、`package-file` 模板和打包辅助脚本。
- `tools/upgrade/`
  `upgrade_tool` 与配套文档。
- `workspace/`
  运行时默认工程目录、解包目录、缓存目录，属于本地产物，默认不纳入版本库。

## 依赖环境

当前主线依赖 Qt 5 开发环境。Ubuntu 22.04 上至少需要这些包：

```bash
sudo apt install -y \
  build-essential cmake pkg-config \
  qtbase5-dev qt5-qmake qtbase5-dev-tools \
  libqt5svg5-dev qtdeclarative5-dev
```

如果只是运行已经构建好的程序，至少仍需要 Qt 运行库和图形桌面环境。

## 启动方式

默认启动原生 Qt 程序：

```bash
cd ~/桌面/RK_Firmware_Studio
./run_rkstudio.sh
```

如果需要浏览器壳回退入口：

```bash
./run_rkstudio.sh --web
```

如果只跑原生自检：

```bash
./run_rkstudio.sh --self-test
```

## 手动构建与自检

手动构建：

```bash
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

原生自检：

```bash
./build/rkstudio_cpp --smoke-test
env QT_QPA_PLATFORM=offscreen ./build/rkstudio_cpp --qt-smoke-test
```

## 典型工作流

### 1. 导入固件工程

准备一个 Rockchip 固件工程目录，通常至少包含：

- `package-file`
- `parameter.txt`
- `MiniLoaderAll.bin`
- 若干分区镜像

软件读取这些文件后，会自动建立分区镜像表与命令预览。

### 2. 检查设备模式

软件会持续检测并显示设备当前状态：

- 未连接
- `maskrom`
- `loader`
- `adb`

不同操作要求不同模式。例如：

- `DB` 通常用于 `maskrom`
- `UF` / `DI` / `UL` 更依赖 `loader`
- `ADB -> Loader` 用于从系统态切到可烧录态

### 3. 选择烧录动作

- `UF`：整包烧录 `update.img`
- `DB`：在 `maskrom` 下临时下载 Loader
- `UL`：写入 Loader / 启动链
- `EF`：擦除存储
- `DI`：按分区逐个烧录镜像

### 4. 观察日志和阶段百分比

任务执行期间，右侧日志窗口显示阶段输出；进度条按阶段推进，不再依赖假进度。

### 5. 打包或解包

打包页可用于：

- 根据当前工程重新生成 `update.img`
- 把整包拆开做检查
- 合并/拆分用于调试或重组固件内容

## 最近一轮整理后的优化

- 默认入口和默认自检全部收口到 C++，不再要求 Python 运行时
- 烧写期间停止后台设备轮询，避免后台探测干扰前台任务
- 日志窗口改为增量更新，减少高频进度输出造成的整窗重绘
- `package-file` 模板列表做缓存，降低工程刷新时的重复扫描
- 仓库已清理为源码主线，不再夹带历史运行目录与旧语言实现

## 烧写速度说明

真正决定烧写吞吐的主要因素不是 GUI，而是：

- `upgrade_tool` 本身的实现
- USB 通道质量
- 当前设备模式
- 目标存储介质速度
- 镜像大小与分区布局

软件层能做的优化主要是：

- 减少后台探测与前台任务冲突
- 减少界面重绘和日志刷屏
- 避免重复扫描工程目录
- 让命令生成与参数校验更确定

所以这个项目能优化的是“流程效率、稳定性、可视化反馈”，不是凭空提升底层写盘带宽。

## 风险与边界

- `EF`、`UL`、`DI` 都可能改写设备关键区域，属于高风险操作
- 没有真实设备连接时，可以验证工程解析、UI、命令构造，但不能替代真机烧录测试
- 这套软件依赖本机 USB 环境和 Rockchip 官方工具，不负责实现底层协议

## 文档

- 中文使用说明：[GUIDE_ZH.md](/home/lv/桌面/RK_Firmware_Studio/GUIDE_ZH.md)
- 分层与架构说明：[ARCHITECTURE.md](/home/lv/桌面/RK_Firmware_Studio/ARCHITECTURE.md)
