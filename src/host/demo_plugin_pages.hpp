#pragma once

#include "anomaly/sdk/services/ui.h"

#include <span>
#include <string_view>

namespace anomaly::ui_preview {

// 预览自带的插件页面。
//
// AnomalyUI 不含插件加载器，因此管理界面里那些“插件页面”由本仓库自己的页面提供；
// 这些页面只通过插件可见的 `anomaly.ui` C ABI 绘制，所以它们看到和用到的控件集
// 与真正的插件完全一致。仓库只带一个演示插件（控件画廊），本文件也只注册它。
struct DemoPluginPage {
    // 页面绑定的 manifest id。未在此登记的包仍会出现在插件列表里，
    // 只是没有对应的页面主体。
    std::string_view plugin_id;
    void (*draw)(const AnomalyUiServiceV1& ui);
};

[[nodiscard]] std::span<const DemoPluginPage> DemoPluginPages() noexcept;

// 由宿主的更新节拍调用；真实宿主会在这里调用插件的 on_update 回调。
void AdvanceDemoPlugins(double delta_seconds) noexcept;

}  // namespace anomaly::ui_preview
