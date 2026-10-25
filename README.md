# AnomalyUI

Anomaly 的**界面层**独立提取仓库：Launcher 窗口、平台（覆盖层）管理界面、插件 UI 宿主，
以及它们之间交换的纯 C ABI。

本仓库只包含 UI。它不注入进程、不读写其它进程内存、不挂钩游戏、不安装代理层、不联网、
也不加载/调用任何插件二进制。所有“后端”都退化为可编译的空实现或界面自带的演示数据，
打开即可预览。

```
AnomalyUI/
├─ apps/
│  ├─ ui_preview/           平台管理界面的独立预览（Win32 + D3D11 + ImGui）
│  └─ launcher/             AnomalyLauncher 窗口（与原仓库同一份 UI 源码）
├─ examples/
│  └─ widget_gallery/       唯一演示插件：控件画廊（中文，覆盖 anomaly.ui 全部控件）
├─ include/
│  ├─ anomaly/sdk/          插件 ABI v1（纯 C，UI 服务表/窗口/字体/纹理/输入）
│  ├─ anomaly/*.hpp         UI 层接口：主题、布局、模型、设置、资源注册表、插件目录模型
│  ├─ platform_host.hpp     平台界面的宿主入口
│  └─ plugin_manager.hpp    插件 UI 宿主（本仓库的实现不含加载器）
├─ src/
│  ├─ ui/                   管理界面本体（platform_host、主题、布局、模型、设置、资源）
│  ├─ host/                 仅 UI 的插件宿主 + 演示页面 + “无内存提供者”实现
│  ├─ plugin/               插件清单/目录/依赖/启停的数据层（无加载）
│  ├─ launcher/             Launcher 的 UI 后端替身（配置持久化 + 其余操作不可用）
│  ├─ platform/windows/     配置文件读取
│  ├─ i18n/ services/       本地化与原子文件写入
│  └─ repository/           仅仓库地址校验（界面输入校验，不联网）
├─ assets/ config/ locales/ 主题素材、字体、语言目录
└─ tools/                   预览窗口截图/点击脚本（仅本地验收用）
```

## 构建

要求 Windows 10/11 x64、Visual Studio 2022、CMake 3.22+。依赖（ImGui 1.91.9b、
nlohmann/json 3.11.3、json-schema-validator 2.3.0）由 CMake 自动拉取。

```powershell
cmake --preset windows-vs2022
cmake --build --preset windows-relwithdebinfo
```

离线构建时可指向本地依赖副本：

```powershell
cmake --preset windows-vs2022 `
  -DFETCHCONTENT_SOURCE_DIR_IMGUI=<imgui> `
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=<json> `
  -DFETCHCONTENT_SOURCE_DIR_JSON_SCHEMA_VALIDATOR=<validator>
```

产物在 `.build/windows-vs2022/bin/<Config>/`：

| 文件 | 说明 |
| --- | --- |
| `AnomalyUIPreview.exe` | 平台管理界面预览（独立窗口 + D3D11 交换链） |
| `AnomalyLauncher.exe` | Launcher 窗口 |
| `Anomaly/` | 两个程序共用的数据根：`anomaly.ini`、`locales/`、`assets/`、`config/themes/`、`plugins/WidgetGallery/` |

## 运行

```powershell
# 平台管理界面预览（--palette= 选择主题调色板，默认 naiwa）
.build\windows-vs2022\bin\RelWithDebInfo\AnomalyUIPreview.exe
.build\windows-vs2022\bin\RelWithDebInfo\AnomalyUIPreview.exe --palette=naiwa

# Launcher 窗口
.build\windows-vs2022\bin\RelWithDebInfo\AnomalyLauncher.exe
```

预览窗口里可以完整走通管理界面：插件列表（安装/可用/更新/第三方）、插件详情、诊断
（概览/插件性能/日志/开发者）、设置（外观/主题/输入/语言）、主题切换、窗口缩放与折叠。
关闭后窗口位置等状态由 ImGui/设置存储在 `Anomaly/` 下。

## 控件画廊（唯一演示插件）

`examples/widget_gallery` 是仓库里唯一的演示插件，manifest 名称即「控件画廊」。它演示
`AnomalyUiServiceV1` 服务表中的**全部**入口：

- 文本：`text`、`text_wrapped`、`separator`
- 基础控件：`button`、`button_enabled`、`checkbox`、`slider_float`、`slider_float_enabled`、
  `color_edit4`、`input_uint32`、`input_double`、`input_text`（含密码/数字标志）
- 布局：`set_next_window_size(_constraints)`、`get_window_size`、`same_line`、
  `set_cursor_pos_x`、`begin_child`/`end_child`、`begin_child_auto_fit_y`、
  `begin_table`/`table_next_row`/`table_next_column`/`end_table`
- 容器：`begin_tab_bar`/`begin_tab_item`/`end_tab_item`/`end_tab_bar`、
  `begin_menu`/`end_menu`
- 弹窗：`open_popup`、`begin_popup_modal`、`end_popup`、`close_current_popup`
- 状态与交互：`frame_state`、`filter_match`、`developer_mode_enabled`
- 屏幕叠加：`draw_entity_bbox`、`draw_entity_box3d`、`draw_entity_label`
  （用演示相机投影，结果画到宿主前景绘制列表，能在「叠加层」页看到）
- 链接：`text_link`

页面代码在 `examples/widget_gallery/gallery_page.hpp`，只依赖插件 ABI。打包插件
（`plugin.cpp`）在 `on_draw` 里画它，预览则由 `src/host/demo_plugin_pages.cpp` 画同一份
头文件——所以预览里看到的页面就是插件会执行的代码。**预览不会加载 `plugin.dll`**，
它只读该包的 `manifest.json` 来生成插件列表。

## 提取范围

保留（原样提取，未改行为）：

- `src/ui/`：`platform_host.cpp`（管理界面本体）、`host_ui_service.cpp`（`anomaly.ui` 服务表
  实现与插件窗口外框）、`platform_ui_theme/layout/model`、`platform_settings`、
  `ui_resource_registry/decoder`
- `apps/launcher/main.cpp`：Launcher 窗口、页面、主题、语言切换、状态卡片
- `include/anomaly/sdk/`：插件 ABI（UI/窗口/字体/纹理/输入/本地化等）
- `src/plugin/` 的**数据层**：manifest 解析与 schema 校验、semver、兼容性、包布局、目录扫描、
  依赖解析、启停状态
- `assets/`、`config/themes/`、`locales/host/`、`schemas/plugin-manifest.schema.json`

未提取（属运行时/游戏侧，本仓库用界面替身或空实现）：

| 原模块 | 这里 |
| --- | --- |
| 插件加载器（`plugin_manager.cpp` 的映射/回调代际/热重载/IPC/作用域服务） | `src/host/plugin_ui_host.cpp`：只读清单建列表、页面绘制、启停与可见性状态 |
| `scoped_platform_services`（插件的内存/钩子/IPC/存储服务） | 不提供；插件页面只使用 `anomaly.ui` 表 |
| 进程内存与 hook（`memory.cpp`、`hook_manager`、`minhook`） | `src/host/no_memory_service.cpp`：全部返回不可用 |
| 游戏适配（`ue5_nte_adapter`、符号/Profile） | `include/anomaly/ui_tick_source.hpp`：仅保留“更新节拍”这一条接口，预览不提供来源 |
| Launcher 的注入与安装（manual map、代理安装、进程附加、运行时解析、恢复状态） | `src/launcher/preview_backend.cpp`：同签名空实现 + 本地面板配置持久化 |
| 仓库网络（下载/安装/索引/签名） | 不提取；预览发布固定示例行，安装类操作返回“不可用” |
| 嵌入式 D3D12 presenter（游戏内呈现） | 不提取（属渲染宿主，不是界面内容） |

因此打开 Launcher 时「安装状态」会显示不可用并说明这是纯 UI 构建；预览的
「开发者」页内存操作会提示提供者不可用。这些是刻意的边界，不是缺陷。

## 验收

本仓库已在 `windows-vs2022` + `RelWithDebInfo` 下编译通过，并用脚本截图确认：

- `AnomalyUIPreview.exe`：管理界面四个页面、主题素材、插件列表与详情、控件画廊四个页签
  （控件/布局/叠加层/交互）均正常渲染，ESP 叠加层投影可见
- `AnomalyLauncher.exe`：窗口与其中文界面正常打开

截图脚本：`tools/capture-window.ps1`（窗口截图）、`tools/click.ps1`（点击驱动页面切换）。

## 许可

界面源码沿用原仓库的许可（见 `LICENSE` / `NOTICE`）。第三方组件许可见
`third_party/licenses/`。
