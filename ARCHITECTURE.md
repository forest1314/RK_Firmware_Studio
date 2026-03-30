# RK Firmware Studio Architecture

## 活跃技术栈

- `C++17`
  当前核心实现语言
- `Qt Widgets`
  当前默认桌面 UI
- `C++ local HTTP server`
  浏览器壳回退入口
- `upgrade_tool`
  Rockusb 通信、Loader 下载、烧写、擦除
- `afptool`
  `firmware.img` 打包与解包
- `rkImageMaker`
  `update.img` 封装、拆包、merge、unmerge

## 当前主链路

默认运行和默认自检都走 C++：

1. `run_rkstudio.sh`
2. `build/rkstudio_cpp`
3. `cpp/src/main.cpp`
4. `cpp/src/qt_window.cpp` 或 `cpp/src/ui_server.cpp`
5. `cpp/src/backend.cpp`
6. 外部 Rockchip 工具

仓库已经清理为 C++ 主线，旧版 Python 实现和 Python 测试不再保留。

## 分层结构

### 1. 桌面界面层

- 文件：`cpp/src/qt_window.cpp`
- 职责：
  - 构建顶部状态栏、导航、模式条、烧录工作区、右侧任务中心
  - 处理参数编辑、分区表编辑、帮助页截图导览
  - 启动外部任务并消费实时日志

### 2. 浏览器壳回退层

- 文件：`cpp/src/ui_server.cpp`
- 职责：
  - 启动本地 HTTP 服务
  - 提供概览、设备状态、诊断等接口
  - 承载 `cpp/ui/` 静态页面

### 3. 后端任务与模型层

- 文件：`cpp/src/backend.cpp`
- 职责：
  - 解析 `package-file`
  - 解析、刷新、校验、写回 `parameter.txt`
  - 生成 `UF / DB / UL / EF / DI` 命令任务
  - 生成打包、解包、合并、拆分任务
  - 探测 ADB / Rockusb 设备状态
  - 收集环境诊断

### 4. 命令规格层

- 文件：`cpp/src/specs.cpp`
- 职责：
  - 描述 `upgrade_tool` 命令规格
  - 统一参数拼装与校验

### 5. 路径与工作区层

- 文件：`cpp/src/paths.cpp`
- 职责：
  - 发现工具目录、工作区目录、默认工程目录
  - 初始化运行所需目录

## 关键运行流

### 烧录中心

1. 界面收集当前模式所需输入
2. 后端生成 `CommandTask`
3. Qt 侧用 `QProcess` 启动对应工具
4. 解析 stdout/stderr 中的阶段与百分比
5. 右侧任务中心展示总任务、当前阶段和日志

### 分区烧录 DI

1. 读取 `parameter.txt`
2. 生成分区行
3. 用户可单独勾选分区、改镜像路径、改容量或 `mtdparts`
4. 容量与 `partition_spec` 双向换算
5. 点“写回 parameter”后回写到 `parameter.txt`
6. 后端把已启用分区映射为 `upgrade_tool DI ...`

### 打包解包

1. `afptool -pack` 生成 `firmware.img`
2. `rkImageMaker` 封装 `update.img`
3. 解包时顺序相反

## 这轮整理后的性能点

### 1. 避免烧写时的后台探测干扰

此前界面定时刷新会周期性执行设备探测，而探测内部会调用 `upgrade_tool LD`。这在烧写期间可能和前台任务竞争同一设备通道。

当前策略：

- 任务运行中停止后台轮询
- 任务结束后再主动刷新设备状态与诊断

这类优化不能提升底层写盘带宽，但能减少软件自身对烧写流程的干扰。

### 2. 日志视图改为增量刷新

此前每追加一行日志都用 `setPlainText()` 重绘全部历史文本。

当前策略：

- 普通追加走增量插入
- 仅在日志裁剪或状态失配时全量重绘
- 进度类替换仍支持原位更新

这能明显降低高频进度输出时的 UI 重绘成本。

### 3. 模板列表缓存

`package_templates()` 现在缓存扫描结果，避免每次刷新工程都重复遍历模板目录。

## 烧写速度判断

底层烧写速度主要由以下因素决定：

- `upgrade_tool` 的实现
- 板端存储介质
- USB 链路质量
- 设备当前模式
- 镜像大小与分区布局

因此软件层能做的优化主要是：

- 不与烧写进程抢设备
- 不让 UI 因日志/重绘卡顿
- 不重复做重型诊断和目录扫描

本轮已覆盖这些可确定的优化点。
