# AnomalyUI Agent 协作约定

本仓库是 Anomaly 界面层的独立提取，遵循通用 `AGENTS.md` 规范。修改前先读 `README.md`
的「提取范围」与「未提取」两节，那里定义了本仓库的边界。

## 仓库定位

只做界面：窗口、主题、布局、控件、本地化、设置与插件 UI 宿主。以下内容不属于本仓库，
不要为了“补全功能”而重新引入：

- 进程注入、manual map、代理安装、进程附加、运行时解析
- 任何进程内存读写、hook、trampoline
- 插件二进制的加载/调用（`LoadLibrary` + 入口调用）
- 网络请求、仓库下载/安装、签名校验流程
- 游戏适配（UE5/NTE 符号、Profile、Adapters）与嵌入式 D3D12 presenter

需要新增交互时，先判断它是否只是界面状态；只有界面无法表达时，才在
`include/` 增加一个接口，并在 `src/host/` 或 `src/launcher/` 提供**空实现**，
在 README 的边界表中登记。

## 分层与边界

- `include/anomaly/sdk/`：纯 C ABI v1，定宽类型 + opaque handle + `struct_size` 校验；
  不得暴露 STL、异常、RTTI 或内部头文件。插件只会看到这一层。
- `include/anomaly/*.hpp`：UI 层接口（主题、布局、模型、设置、资源注册表、插件目录模型）。
- `src/ui/`：管理界面本体。`platform_host.cpp` 是界面组合根，`host_ui_service.cpp` 是
  `anomaly.ui` 服务表与插件窗口外框的实现。这两处代码与上游保持同构，改动要克制。
- `src/host/`：仅 UI 的插件宿主（`plugin_manager.hpp` 的实现）与预览演示页面。
- `src/launcher/preview_backend.cpp`：Launcher 的 UI 后端替身；签名必须与
  `include/anomaly/launcher/*.hpp`、`runtime_launch.hpp`、`runtime_recovery.hpp` 一致。
- `examples/widget_gallery/`：控件画廊。`gallery_page.hpp` 只依赖插件 ABI，被插件与预览
  共用；新增控件必须先加到 ABI（`include/anomaly/sdk/services/ui.h`），再加到这一页。

## 构建与验证

统一使用 `CMakePresets.json` 的 `windows-vs2022` / `windows-relwithdebinfo`。验证方式：

1. `cmake --build --preset windows-relwithdebinfo`（两个目标都要过）
2. 运行 `AnomalyUIPreview.exe`，用 `tools/capture-window.ps1` 截图，确认页面正常渲染
3. 修改控件或页面时，用 `tools/click.ps1` 点开对应页签再截图

中文源码一律保存为 **UTF-8 with BOM**（MSVC 在 936 代码页下会误解析无 BOM 的 UTF-8），
或给该 target 加 `/utf-8`。

## 提交

只暂存明确文件，不要 `git add -A`；不要生成回滚副本（`.bak`、带时间戳的副本）。
