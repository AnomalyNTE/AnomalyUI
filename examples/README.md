# AnomalyUI SDK 示例

本目录只带一个演示插件：`widget_gallery`（控件画廊）。它演示插件可见的
`anomaly.ui` 服务表中的全部控件，页面代码在 `gallery_page.hpp`，插件入口在
`plugin.cpp`。

构建由根 `CMakeLists.txt` 统一完成（`anomaly_add_plugin`），产物落在
`.build/<preset>/packages/WidgetGallery/`，并自动同步到预览的
`bin/<Config>/Anomaly/plugins/WidgetGallery/`。

注意：AnomalyUI 不含插件加载器。预览只读取该目录下的 `manifest.json` 生成插件列表，
不会加载 `plugin.dll`；预览里画出的页面来自同一份 `gallery_page.hpp`。
