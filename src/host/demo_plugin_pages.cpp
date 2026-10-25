// 预览的插件页面。
//
// 这里没有任何“真正的插件”：本仓库只发布一个演示插件（examples/widget_gallery），
// 预览在管理界面里绘制的就是它那份页面。页面代码与打包插件共用同一个头文件，
// 因此这里能看到的控件，就是插件侧 ABI 提供的全部控件。

#include "demo_plugin_pages.hpp"

#include "../examples/widget_gallery/gallery_page.hpp"

#include <array>

namespace anomaly::ui_preview {
namespace {

anomaly::example::gallery::GalleryState g_gallery;

void DrawGallery(const AnomalyUiServiceV1& ui) {
    anomaly::example::gallery::DrawGalleryPage(ui, g_gallery);
}

constexpr std::array<DemoPluginPage, 1> kPages{{
    {"anomaly.example.widget-gallery", &DrawGallery},
}};

}  // namespace

std::span<const DemoPluginPage> DemoPluginPages() noexcept {
    return kPages;
}

void AdvanceDemoPlugins(double delta_seconds) noexcept {
    static_cast<void>(delta_seconds);
    // 控件画廊没有需要推进的状态：所有数值都由页面自己的控件维护，
    // 就像插件把状态放在 Update 回调里一样。
}

}  // namespace anomaly::ui_preview
