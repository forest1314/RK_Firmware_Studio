# RK Firmware Studio 使用指南

## 1. 这版软件现在是什么

这是一个面向 Rockchip 固件工程的桌面工具，当前默认实现已经切到 `C++17 + Qt Widgets`。界面按量产/调试流程整理成 4 个顶部导航页：

- 烧录中心
- 打包解包
- 设备信息
- 帮助说明

软件默认进入“烧录中心”，不再走旧版 Python Qt 主界面。

顶部设备状态是全局常驻的，不会因为切页丢失。当前状态分为：

- 未连接
- maskrom
- loader
- adb

---

## 2. 这版软件的技术栈和工作原理

## 2.1 技术栈

- 语言：C++17
- GUI：Qt Widgets
- 回退入口：C++ 本地 HTTP 服务 + 浏览器壳
- 命令执行：本地子进程调用 `upgrade_tool`、`afptool`、`rkImageMaker`
- 后端模型：`parameter.txt`、`package-file`、分区表、设备状态统一由 C++ 后端解析
- 日志系统：右侧任务中心实时显示任务输出和阶段百分比

## 2.2 软件不是自己烧录，它本质上做了什么

这个软件本质上是一个“图形化控制台”：

- 读取你的固件工程目录
- 解析 `package-file`
- 解析 `parameter.txt`
- 根据界面上的输入生成命令
- 调用 Rockchip 官方工具执行
- 把标准输出、错误输出和任务状态显示到界面

所以真正下发到板子的不是 GUI 本身，而是这些底层命令：

- `upgrade_tool`
- `afptool`
- `rkImageMaker`

## 2.3 五个核心烧录动作的原理

### `UF`

用途：

- 整包烧录 `update.img`

原理：

- 调用 `upgrade_tool UF update.img`
- 工具会把整包按内部清单解开并写入设备

适合：

- 固件已经打成标准 `update.img`
- 想走最省事的量产流程

### `DB`

用途：

- 在 Maskrom 下把 Loader 临时下载到 RAM

原理：

- 调用 `upgrade_tool DB MiniLoaderAll.bin`
- 只建立临时通信，不直接把整套系统写进去

适合：

- 设备处于 `maskrom`
- 先把板子拉到可通信状态

### `UL`

用途：

- 烧录 Loader/IDBlock

原理：

- 调用 `upgrade_tool UL MiniLoaderAll.bin [存储类型]`
- 把启动链路写到目标存储

适合：

- 启动链异常
- 设备需要先恢复 Loader

### `EF`

用途：

- 擦除设备存储

原理：

- 调用 `upgrade_tool EF <loader 或固件>`
- 由官方工具执行擦除

适合：

- 清空存储
- 重刷前做全擦

注意：

- 这是破坏性操作

### `DI`

用途：

- 按分区逐个烧录镜像

原理：

- 软件从分区表格中读取“已勾选分区 + 镜像路径”
- 把它们转换成 `upgrade_tool DI ...` 命令
- 例如 `boot` 会转成 `-b boot.img`
- `rootfs` 这种非内建短参数分区会转成 `-rootfs rootfs.img`

适合：

- 不想整包烧录
- 只改某几个分区
- 调试、救砖、局部升级

---

## 3. 当前已经验证过什么

## 3.1 已验证通过

以下内容已经在当前项目中验证通过：

- 新界面结构可以启动
- 默认页面是“烧录中心”
- `UF / DB / UL / EF / DI` 命令能从当前工程正确生成
- `parameter.txt` 和 `package-file` 能正确解析
- 分区表格能正确导入你提供的工程
- 选择镜像后会刷新镜像大小，不会再自动偷偷改小分区参数
- 如需按镜像大小重算分区，必须手动点击“按镜像大小重算”
- 单元测试已通过

## 3.2 用来验证的实际工程

本轮用的是你的工程：

```text
/home/lv/baidunetDownload/Output/Android
```

工程里已检测到：

- `package-file`
- `parameter.txt`
- `MiniLoaderAll.bin`
- `update.img`
- `uboot.img`
- `misc.img`
- `boot.img`
- `recovery.img`
- `rootfs.img`
- `oem.img`
- `userdata.img`

## 3.3 当前没有在这个会话里直接完成的部分

当前 Codex 运行环境没有挂载真实的 USB 设备节点，虽然能看到 `lsusb`，但没有 `/dev/bus/usb`，所以这里不能直接替你执行真板烧录或读取。

这就是为什么在这里执行 `upgrade_tool LD` 时会直接报：

```text
failed to call context_enter!
```

这说明：

- 软件的工程解析和命令生成已经验证
- 真板读写必须在你自己的桌面环境里运行 GUI 或本机终端执行

---

## 4. 先看懂界面

## 4.1 顶部全局区域

这里所有页面共用。

### 设备状态

显示当前检测到的主状态：

- `未连接`：没检测到 Rockusb，也没检测到 ADB
- `maskrom`：检测到 Rockusb Maskrom
- `loader`：检测到 Rockusb Loader
- `adb`：检测到 ADB，且优先级高于 Rockusb

### 状态详情

会显示更细的信息，例如：

- ADB 序列号
- Rockusb 的 LocationID
- 检测失败时的错误文字

### 工程摘要

会显示当前使用的：

- 工程目录
- `parameter.txt`
- Loader 文件
- `update.img`

### 立即刷新状态

用途：

- 立即重新跑一次设备检测

### 打开日志目录

用途：

- 打开任务日志所在目录

## 4.2 全局上下文栏

这里相当于“本次任务的基础参数”。

### 工程目录

填整个固件工程目录，例如：

```text
/home/lv/baidunetDownload/Output/Android
```

### parameter.txt

填参数文件路径，例如：

```text
/home/lv/baidunetDownload/Output/Android/parameter.txt
```

### Loader 文件

填 Loader 路径，例如：

```text
/home/lv/baidunetDownload/Output/Android/MiniLoaderAll.bin
```

### update.img

填整包固件路径，例如：

```text
/home/lv/baidunetDownload/Output/Android/update.img
```

### 目标设备参数

这里是给 `upgrade_tool` 加公共前置参数的。

最常见的用途是多设备场景：

```text
-s 1401
```

不确定就留空。

---

## 5. 烧录中心怎么用

这是默认页，也是最常用的页。

## 5.1 分区镜像列表五列分别是什么意思

### 启用

- 勾选后，`DI` 时该分区会参与烧录

### 名称

- 分区名
- 来自 `parameter.txt`

### 文件路径

- 对应镜像文件路径
- 可以手工改
- 也可以点“浏览”

### 包大小

- 显示镜像的真实文件大小
- 同时显示换算后的 sector 数

### mtdparts 参数

- 这是当前分区在 `parameter.txt` 里的分区描述
- 例如：

```text
0x00020000@0x00008000(boot)
```

## 5.2 表格上方按钮怎么用

### 从工程导入

用途：

- 从工程目录自动读取 `package-file`
- 自动找 `parameter.txt`
- 自动找 Loader
- 自动生成分区表格

第一次打开软件，最先点这个。

### 重新加载 parameter

用途：

- 丢弃当前表格修改
- 从磁盘重新读取 `parameter.txt`

### 按镜像大小重算

用途：

- 把当前已选镜像的实际大小换算成分区 sector 数
- 按顺序重算后续分区偏移

什么时候用：

- 你明确知道要根据镜像大小改分区
- 你准备重新生成一份新的 `parameter.txt`

注意：

- 这个动作会改表格里的分区参数
- 改完后要自己确认布局是否合理
- 不会自动保存到磁盘，必须再点“保存 parameter”

### 保存 parameter

用途：

- 把当前表格里的分区参数写回 `parameter.txt`

注意：

- 保存的是表格当前值
- 不会自动帮你判断这个布局是不是你真正想要的最终布局

## 5.3 五个小按钮分别怎么用

### `UF`

需要什么：

- 上下文栏里的 `update.img`

什么时候用：

- 已经有完整 `update.img`
- 想直接整包烧录

### `DB`

需要什么：

- 上下文栏里的 Loader 文件

什么时候用：

- 板子处于 `maskrom`
- 先建立临时下载通道

### `UL`

需要什么：

- 上下文栏里的 Loader 文件
- 如有需要，在“UL 目标存储”里选择存储类型

什么时候用：

- 需要重写 Loader/IDBlock

### `EF`

需要什么：

- 上下文栏里的 Loader 文件

什么时候用：

- 需要擦除设备存储

注意：

- 这是破坏性操作

### `DI`

需要什么：

- 已导入工程
- 已加载 `parameter.txt`
- 分区表中至少勾选一个分区
- 被勾选分区必须有有效镜像文件

软件会额外检查：

- 镜像文件是否存在
- 镜像大小是否超过分区容量

如果失败：

- 不会弹窗
- 会把错误直接写到右侧日志
- 不会执行危险命令

## 5.4 最推荐的两条烧录路线

### 路线 A：最省心

适合：

- 你已经有 `update.img`

步骤：

1. 从工程导入
2. 确认 `update.img` 已自动带出
3. 确认设备状态不是“未连接”
4. 点击 `UF`

### 路线 B：救砖/分区烧录

适合：

- 板子在 `maskrom`
- 只想烧某几个分区
- 不想整包刷

步骤：

1. 从工程导入
2. 在设备状态看到 `maskrom`
3. 先点 `DB`
4. 如需写启动链，再点 `UL`
5. 在表格里勾选要烧录的分区
6. 点 `DI`

---

## 6. 打包解包怎么用

## 6.1 生成 update.img

这一块用于把工程目录重新打成标准固件包。

### 芯片配置

优先选与你芯片一致的配置。

你这套工程推荐：

- 芯片配置：`rk3588 (RK3588)`
- Chip Code：`RK3588`
- OS Type：`ANDROIDOS`

### 检查工程

用途：

- 检查 `package-file` 里的条目是否都有对应文件

### 生成 update.img

用途：

- 调用 `afptool -pack`
- 再调用 `rkImageMaker`
- 生成新的 `update.img`

你这套工程如果要生成，建议确认磁盘空间足够。

## 6.2 解包 / 合并 / 拆分

### 解包 update.img

用途：

- 把 `update.img` 解开成工程目录

### 合并

用途：

- 把多个固件合成一个输出文件

### 拆分

用途：

- 把多存储固件拆分回多个文件

---

## 7. 设备信息怎么用

这个页面只做读取和维护，不直接做整包烧录。

## 7.1 常用读取按钮

### 列出设备

用途：

- 看 `upgrade_tool` 当前是否识别到了 Rockusb 设备

### 读取分区表

用途：

- 读取设备当前分区信息

### 读取 Flash 信息

用途：

- 查看存储信息

### 读取芯片信息

用途：

- 查看芯片信息

### 读取 SN / 读取 Flash ID / 读取能力 / 读取 CPU ID / 读取安全模式

用途：

- 读取对应信息

## 7.2 切换与维护按钮

### 切到 Maskrom

用途：

- 向设备下发重启到 Maskrom 的命令

### 切换 USB3

用途：

- 切换 USB 传输模式

### 切换存储 SSD

用途：

- 切换目标存储

## 7.3 环境诊断

用途：

- 检查本机工具链是否齐全
- 检查 `lsusb` 是否看到了 Rockchip 设备
- 检查 `adb devices` 是否有输出
- 检查 `upgrade_tool LD` 的返回码和原始报错

什么时候用：

- “列出设备”失败
- 日志里出现 `failed to call context_enter!`
- 你怀疑是 USB 权限、环境或工具链问题

它不会写板，只做本机读诊断。

---

## 8. 帮助说明怎么用

当前版本已经把旧的“高级工具”页面移出主流程，第四个导航页改成“帮助说明”。

这页主要做三件事：

- 用截图框选方式解释顶部状态栏、模式条、分区表和任务中心
- 说明 `UF / DB / UL / EF / DI` 的适用场景
- 展示本地文档路径，便于继续查看 `GUIDE_ZH.md` 和官方 PDF

如果你要修改 `rootfs` 大小或写回 `parameter.txt`，优先看帮助页里“烧录中心怎么用”那一段，里面已经明确说明：

- 在分区镜像表里找到 `rootfs`
- 修改“包大小”或 `mtdparts` 参数
- 确认容量没有越界
- 点“写回 parameter”把结果落到 `parameter.txt`

---

## 9. 你这套 `Output/Android` 工程应该怎么填

如果你就用当前这套工程，最常用的填写方式如下。

### 工程目录

```text
/home/lv/baidunetDownload/Output/Android
```

### parameter.txt

```text
/home/lv/baidunetDownload/Output/Android/parameter.txt
```

### Loader 文件

```text
/home/lv/baidunetDownload/Output/Android/MiniLoaderAll.bin
```

### update.img

```text
/home/lv/baidunetDownload/Output/Android/update.img
```

### 推荐的分区勾选

如果你做全分区 `DI`，通常勾选：

- `uboot`
- `misc`
- `boot`
- `recovery`
- `rootfs`
- `oem`
- `userdata`

`backup` 没有镜像时不要勾。

---

## 10. 常见问题

## 10.1 为什么“列出设备”能失败，但我明明插着板子

常见原因：

- 板子没进 Rockusb，而只是普通 USB
- 板子只进了 ADB，没进 Loader/Maskrom
- USB 线只有供电没有数据
- USB 权限不对
- 系统能看到设备，但 `upgrade_tool` 没法进入上下文

先做这几步：

1. 看顶部状态是不是 `maskrom` 或 `loader`
2. 在终端执行 `lsusb | grep 2207`
3. 检查是否有 Rockchip 设备
4. 如果 `upgrade_tool LD` 报 `failed to call context_enter!`，优先排查 USB 权限或系统设备节点

这时建议直接去“设备信息”页点击“运行环境诊断”，先看：

- `/dev/bus/usb` 是否存在
- `lsusb` 是否能看到 `2207:` 设备
- `upgrade_tool LD` 的原始返回码和输出

## 10.2 为什么我选了镜像，分区参数没有自动变

这是当前版本故意这样设计的。

原因：

- 自动按镜像大小缩分区太危险
- 容易把原来留好的冗余空间缩掉

如果你确实要按镜像大小重算：

1. 先导入工程
2. 选好镜像
3. 点击“按镜像大小重算”
4. 确认表格结果
5. 再点“保存 parameter”

## 10.3 为什么 `DI` 点了以后不执行，只在日志里报错

这是当前版本的保护逻辑。

会阻止 `DI` 的情况包括：

- 勾选了分区但没填镜像
- 镜像文件不存在
- 镜像大小超过分区容量
- `parameter.txt` 格式非法

先看右侧日志，按日志修正后再执行。

## 10.4 `maskrom`、`loader`、`adb` 到底怎么理解

- `maskrom`：最低级救援状态，常见于空片、启动损坏、强制进 Maskrom
- `loader`：已经有 Rockusb Loader 通道
- `adb`：系统已经起起来，能走 ADB

对于官方 `upgrade_tool`：

- 真正烧录通常还是依赖 Rockusb 通道
- 所以仅有 `adb` 不代表 `upgrade_tool` 命令一定能执行

---

## 11. 当前版本的设计原则

- 默认进入烧录中心
- 设备状态常驻，不受页面切换影响
- `parameter.txt` 是分区表唯一真实来源
- `DI` 一律从分区表格取值
- 普通错误只写日志，不弹窗打断
- 危险的自动修改行为尽量改成显式按钮

---

## 12. 如果你现在就要开始用

最简单的顺序就是：

1. 启动软件
2. 默认停在“烧录中心”
3. 在上方把工程目录填成 `/home/lv/baidunetDownload/Output/Android`
4. 点击“从工程导入”
5. 看顶部设备状态
6. 如果你已经有 `update.img`，优先试 `UF`
7. 如果设备在 `maskrom`，先试 `DB`
8. 需要分区刷写时，勾选目标分区后再执行 `DI`

如果执行失败，不要猜，先看右侧日志。
