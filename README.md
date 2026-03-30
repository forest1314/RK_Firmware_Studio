# RK Firmware Studio

Rockchip 固件工程工作台。当前默认产品链路已经统一到 `C++17 + Qt Widgets`。

## 当前状态

- 默认桌面入口：原生 C++ Qt 程序 `rkstudio_cpp`
- 回退入口：C++ 本地 HTTP 服务 + 浏览器壳
- 默认自检：C++ smoke test + Qt offscreen smoke test
- 仓库已经清理为 C++ 主线，不再保留旧版 Python 实现和 Python 测试

## 当前能力

- 烧录中心：`UF` `DB` `UL` `EF` `DI`
- 打包解包：生成 `update.img`、解包、合并、拆分
- 设备信息：状态检测、`ADB -> Loader`、只读查询、环境诊断
- 帮助说明：截图式引导、模式说明、本地文档路径
- `parameter.txt` 解析、分区表编辑、`rootfs` 等分区容量修改与写回

## 目录结构

- `cpp/`
  当前 C++ 源码与前端静态资源
- `rkstudio/assets/`
  图标、模式资源、帮助页截图素材
- `tools/pack/`
  `afptool`、`rkImageMaker` 和 `package-file` 模板
- `tools/upgrade/`
  `upgrade_tool` 与配套文档
- `workspace/`
  运行时自动生成的默认工程、解包目录、状态缓存；默认不纳入版本库

## 启动

```bash
cd ~/桌面/RK_Firmware_Studio
./run_rkstudio.sh
```

如果需要浏览器壳：

```bash
./run_rkstudio.sh --web
```

如果只跑原生自检：

```bash
./run_rkstudio.sh --self-test
```

## 自检与构建

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

## 这轮整理后的优化

- 默认入口和默认自检都收口到 C++，不再要求 Python 运行时
- 烧写任务执行期间停止后台设备轮询与诊断，避免 `upgrade_tool LD` 后台探测干扰前台烧写
- 日志窗口改为增量更新，减少高频进度输出时的整窗重绘
- `package-file` 模板列表改为缓存，减少工程刷新时的重复目录扫描

## 烧写速度结论

真正的烧写吞吐主要取决于：

- `upgrade_tool`
- USB 通道质量与模式
- 目标存储介质
- 镜像体积与分区布局

软件本身不能直接提高底层写盘带宽，但可以避免自身去抢设备、卡 UI 或引入额外探测开销。本轮已经把这类明确的软件侧损耗压掉。

## 说明

- 这套软件是 Rockchip 官方二进制工具的图形化调度层，不替代底层协议实现
- 没有真实设备连接时，工程解析、命令构造、界面和帮助页都可以验证；真实烧写仍需要本机 USB 环境
- 更细的分层说明见 [ARCHITECTURE.md](/home/lv/桌面/RK_Firmware_Studio/ARCHITECTURE.md)
