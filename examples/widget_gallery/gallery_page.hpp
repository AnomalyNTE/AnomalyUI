#pragma once

// 控件画廊：一页演示插件可见的 `AnomalyUiServiceV1` 服务表中的每一个控件。
//
// 这个头文件刻意不依赖宿主实现：打包的示例插件在 on_draw 回调里包含它，
// AnomalyUI 的预览也在不加载任何二进制的前提下包含它，因此预览里看到的页面
// 就是插件实际会执行的那份代码。

#include "anomaly/sdk/services/ui.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace anomaly::example::gallery {

// 窗口标志直接透传给宿主的窗口开启调用；取值来自宿主使用的 ImGui 版本。
inline constexpr std::uint32_t kWindowMenuBar = 1U << 0U;

struct GalleryState final {
    int enabled{1};
    int show_overlay{1};
    int developer_only{};
    int table_rows{3};
    float scale{1.0F};
    float weight{0.35F};
    float accent[4]{0.42F, 0.72F, 0.94F, 1.0F};
    std::uint32_t interval_milliseconds{250};
    double smoothing{0.35};
    std::array<char, 128> label{"控件画廊"};
    std::uint32_t frame_state{};
    int filter_hit{};
    std::uint32_t clicks{};
    std::uint32_t popup_clicks{};
    std::string status{"就绪"};
    bool popup_open{};
    float window_width{};
    float window_height{};
};

namespace detail {

[[nodiscard]] inline AnomalyStringViewV1 View(const std::string_view value) noexcept {
    return AnomalyStringViewV1{value.data(), value.size()};
}

// ABI 里每个入口都是可选的，因此这里统一做空指针判断：
// 宿主发布的是完整表时全部控件都会出现，缺少某一项时只是少一个控件。
class GalleryUi final {
public:
    explicit GalleryUi(const AnomalyUiServiceV1& ui) noexcept : ui_(ui) {}

    void SetNextWindowSize(const float width, const float height,
        const std::uint32_t condition) const noexcept {
        if (ui_.set_next_window_size != nullptr) {
            ui_.set_next_window_size(ui_.user, width, height, condition);
        }
    }
    [[nodiscard]] bool BeginWindow(const std::string_view title, int* open,
        const std::uint32_t flags) const noexcept {
        return ui_.begin_window != nullptr &&
            ui_.begin_window(ui_.user, View(title), open, flags) != 0;
    }
    void EndWindow() const noexcept {
        if (ui_.end_window != nullptr) ui_.end_window(ui_.user);
    }
    void Text(const std::string_view text) const noexcept {
        if (ui_.text != nullptr) ui_.text(ui_.user, View(text));
    }
    void TextWrapped(const std::string_view text) const noexcept {
        if (ui_.text_wrapped != nullptr) ui_.text_wrapped(ui_.user, View(text));
    }
    [[nodiscard]] bool Button(const std::string_view label, const float width = 0.0F,
        const float height = 0.0F) const noexcept {
        return ui_.button != nullptr && ui_.button(ui_.user, View(label), width, height) != 0;
    }
    [[nodiscard]] bool ButtonEnabled(const std::string_view label, const bool enabled,
        const float width = 0.0F, const float height = 0.0F) const noexcept {
        return ui_.button_enabled != nullptr &&
            ui_.button_enabled(ui_.user, View(label), width, height, enabled ? 1 : 0) != 0;
    }
    [[nodiscard]] bool Checkbox(const std::string_view label, int* value) const noexcept {
        return ui_.checkbox != nullptr && ui_.checkbox(ui_.user, View(label), value) != 0;
    }
    [[nodiscard]] bool Slider(const std::string_view label, float* value,
        const float minimum, const float maximum) const noexcept {
        return ui_.slider_float != nullptr &&
            ui_.slider_float(ui_.user, View(label), value, minimum, maximum) != 0;
    }
    [[nodiscard]] bool SliderEnabled(const std::string_view label, float* value,
        const float minimum, const float maximum, const bool enabled) const noexcept {
        return ui_.slider_float_enabled != nullptr &&
            ui_.slider_float_enabled(ui_.user, View(label), value, minimum, maximum,
                enabled ? 1 : 0) != 0;
    }
    [[nodiscard]] bool Color(const std::string_view label, float rgba[4]) const noexcept {
        return ui_.color_edit4 != nullptr &&
            ui_.color_edit4(ui_.user, View(label), rgba) != 0;
    }
    [[nodiscard]] bool InputUint32(const std::string_view label, std::uint32_t* value,
        const std::uint32_t step, const std::uint32_t step_fast) const noexcept {
        return ui_.input_uint32 != nullptr &&
            ui_.input_uint32(ui_.user, View(label), value, step, step_fast) != 0;
    }
    [[nodiscard]] bool InputDouble(const std::string_view label, double* value,
        const double step, const double step_fast) const noexcept {
        return ui_.input_double != nullptr &&
            ui_.input_double(ui_.user, View(label), value, step, step_fast) != 0;
    }
    [[nodiscard]] bool InputText(const std::string_view label, char* buffer,
        const std::size_t capacity, const std::uint32_t flags) const noexcept {
        return ui_.input_text != nullptr &&
            ui_.input_text(ui_.user, View(label), buffer, capacity, flags) != 0;
    }
    void Separator() const noexcept {
        if (ui_.separator != nullptr) ui_.separator(ui_.user);
    }
    void SameLine(const float offset = 0.0F, const float spacing = -1.0F) const noexcept {
        if (ui_.same_line != nullptr) ui_.same_line(ui_.user, offset, spacing);
    }
    void SetCursorPosX(const float local_x) const noexcept {
        if (ui_.set_cursor_pos_x != nullptr) ui_.set_cursor_pos_x(ui_.user, local_x);
    }
    [[nodiscard]] bool BeginChild(const std::string_view id, const float width,
        const float height, const std::uint32_t flags = 0) const noexcept {
        return ui_.begin_child != nullptr &&
            ui_.begin_child(ui_.user, View(id), width, height, flags) != 0;
    }
    [[nodiscard]] bool BeginChildAutoFitY(const std::string_view id, const float width,
        const float max_height) const noexcept {
        return ui_.begin_child_auto_fit_y != nullptr &&
            ui_.begin_child_auto_fit_y(ui_.user, View(id), width, max_height) != 0;
    }
    void EndChild() const noexcept {
        if (ui_.end_child != nullptr) ui_.end_child(ui_.user);
    }
    [[nodiscard]] bool BeginTable(const std::string_view id, const std::int32_t columns,
        const std::uint32_t flags, const float width, const float height) const noexcept {
        return ui_.begin_table != nullptr &&
            ui_.begin_table(ui_.user, View(id), columns, flags, width, height) != 0;
    }
    void TableNextRow() const noexcept {
        if (ui_.table_next_row != nullptr) ui_.table_next_row(ui_.user);
    }
    [[nodiscard]] bool TableNextColumn() const noexcept {
        return ui_.table_next_column != nullptr && ui_.table_next_column(ui_.user) != 0;
    }
    void EndTable() const noexcept {
        if (ui_.end_table != nullptr) ui_.end_table(ui_.user);
    }
    [[nodiscard]] bool BeginTabBar(const std::string_view id) const noexcept {
        return ui_.begin_tab_bar != nullptr &&
            ui_.begin_tab_bar(ui_.user, View(id), ANOMALY_UI_TAB_BAR_V1_NONE) != 0;
    }
    [[nodiscard]] bool BeginTabItem(const std::string_view label) const noexcept {
        return ui_.begin_tab_item != nullptr &&
            ui_.begin_tab_item(ui_.user, View(label), nullptr,
                ANOMALY_UI_TAB_ITEM_V1_NONE, 1) != 0;
    }
    void EndTabItem() const noexcept {
        if (ui_.end_tab_item != nullptr) ui_.end_tab_item(ui_.user);
    }
    void EndTabBar() const noexcept {
        if (ui_.end_tab_bar != nullptr) ui_.end_tab_bar(ui_.user);
    }
    [[nodiscard]] bool BeginMenu(const std::string_view label, const bool enabled) const noexcept {
        return ui_.begin_menu != nullptr &&
            ui_.begin_menu(ui_.user, View(label), enabled ? 1 : 0) != 0;
    }
    void EndMenu() const noexcept {
        if (ui_.end_menu != nullptr) ui_.end_menu(ui_.user);
    }
    void OpenPopup(const std::string_view id) const noexcept {
        if (ui_.open_popup != nullptr) ui_.open_popup(ui_.user, View(id));
    }
    [[nodiscard]] bool BeginPopupModal(const std::string_view id, int* open,
        const std::uint32_t flags) const noexcept {
        return ui_.begin_popup_modal != nullptr &&
            ui_.begin_popup_modal(ui_.user, View(id), open, flags) != 0;
    }
    void EndPopup() const noexcept {
        if (ui_.end_popup != nullptr) ui_.end_popup(ui_.user);
    }
    void CloseCurrentPopup() const noexcept {
        if (ui_.close_current_popup != nullptr) ui_.close_current_popup(ui_.user);
    }
    [[nodiscard]] int FilterMatch(const std::string_view filter,
        const std::string_view value) const noexcept {
        return ui_.filter_match != nullptr
            ? ui_.filter_match(ui_.user, View(filter), View(value)) : 0;
    }
    [[nodiscard]] std::uint32_t FrameState() const noexcept {
        return ui_.frame_state != nullptr ? ui_.frame_state(ui_.user) : 0U;
    }
    void SetNextWindowSizeConstraints(const float min_width, const float min_height,
        const float max_width, const float max_height) const noexcept {
        if (ui_.set_next_window_size_constraints != nullptr) {
            ui_.set_next_window_size_constraints(
                ui_.user, min_width, min_height, max_width, max_height);
        }
    }
    void GetWindowSize(float* width, float* height) const noexcept {
        if (ui_.get_window_size != nullptr) ui_.get_window_size(ui_.user, width, height);
    }
    [[nodiscard]] int DeveloperModeEnabled() const noexcept {
        return ui_.developer_mode_enabled != nullptr
            ? ui_.developer_mode_enabled(ui_.user) : 0;
    }
    [[nodiscard]] bool TextLink(const std::string_view label,
        const std::string_view url) const noexcept {
        return ui_.text_link != nullptr &&
            ui_.text_link(ui_.user, View(label), View(url)) != 0;
    }
    [[nodiscard]] int DrawEntityBbox(const AnomalyEspCameraV1& camera,
        const AnomalyEspEntityBoundsV1& bounds, const AnomalyEspBoxStyleV1& style) const noexcept {
        return ui_.draw_entity_bbox != nullptr
            ? ui_.draw_entity_bbox(ui_.user, &camera, &bounds, &style) : 0;
    }
    [[nodiscard]] int DrawEntityBox3d(const AnomalyEspCameraV1& camera,
        const AnomalyEspEntityBoundsV1& bounds, const AnomalyEspBoxStyleV1& style) const noexcept {
        return ui_.draw_entity_box3d != nullptr
            ? ui_.draw_entity_box3d(ui_.user, &camera, &bounds, &style) : 0;
    }
    [[nodiscard]] int DrawEntityLabel(const AnomalyEspCameraV1& camera,
        const AnomalyEspEntityBoundsV1& bounds, const std::string_view text,
        const std::uint32_t color_rgba) const noexcept {
        return ui_.draw_entity_label != nullptr
            ? ui_.draw_entity_label(ui_.user, &camera, &bounds, View(text), color_rgba) : 0;
    }

private:
    const AnomalyUiServiceV1& ui_;
};

// 演示相机：沿 +Y 看向原点处的包围盒，投影结果落在宿主视口中央，
// 这样三个 ESP 绘制入口都是可见的效果，而不是纸面 API。
[[nodiscard]] inline AnomalyEspCameraV1 DemoTargetCamera() noexcept {
    AnomalyEspCameraV1 camera{};
    camera.struct_size = sizeof(AnomalyEspCameraV1);
    camera.position[1] = -640.0;
    camera.rotation[1] = 90.0;
    camera.horizontal_fov_degrees = 60.0F;
    return camera;
}

[[nodiscard]] inline AnomalyEspEntityBoundsV1 DemoTargetBounds() noexcept {
    AnomalyEspEntityBoundsV1 bounds{};
    bounds.struct_size = sizeof(AnomalyEspEntityBoundsV1);
    bounds.extent[0] = 70.0;
    bounds.extent[1] = 95.0;
    bounds.extent[2] = 70.0;
    return bounds;
}

[[nodiscard]] inline AnomalyEspBoxStyleV1 DemoBoxStyle(const std::uint32_t color) noexcept {
    AnomalyEspBoxStyleV1 style{};
    style.struct_size = sizeof(AnomalyEspBoxStyleV1);
    style.flags = ANOMALY_ESP_BOX_V1_OUTLINE;
    style.color_rgba = color;
    style.outline_color_rgba = ANOMALY_RGBA_V1(12, 14, 18, 200);
    style.thickness = 1.6F;
    style.outline_thickness = 1.0F;
    return style;
}

}  // namespace detail

inline void DrawGalleryPage(const AnomalyUiServiceV1& ui, GalleryState& state) {
    detail::GalleryUi page(ui);
    page.SetNextWindowSizeConstraints(380.0F, 260.0F, 1200.0F, 900.0F);
    if (!page.BeginWindow("控件画廊###anomaly.example.widget-gallery", nullptr,
            kWindowMenuBar)) {
        page.EndWindow();
        return;
    }
    page.GetWindowSize(&state.window_width, &state.window_height);
    state.frame_state = page.FrameState();

    if (page.BeginMenu("文件", true)) {
        page.Text("菜单项来自 begin_menu / end_menu。");
        if (page.Button("重置本页", 140.0F, 0.0F)) {
            state = GalleryState{};
        }
        page.EndMenu();
    }
    if (page.BeginMenu("弹窗", true)) {
        if (page.Button("打开模态框", 150.0F, 0.0F)) page.OpenPopup("gallery-popup");
        if (page.Button("打开带开关的模态框", 180.0F, 0.0F)) {
            state.popup_open = true;
            page.OpenPopup("gallery-modal");
        }
        page.EndMenu();
    }
    if (page.BeginMenu("禁用菜单", false)) {
        page.Text("不可达");
        page.EndMenu();
    }

    page.TextWrapped(
        "本页把 anomaly.ui 服务表里的全部控件都摆了出来。页面用插件侧的 ABI 绘制："
        "打包的示例插件和 UI 预览运行的是同一份代码。");
    page.Separator();

    if (page.BeginTabBar("gallery-tabs")) {
        if (page.BeginTabItem("控件")) {
            state.filter_hit = page.FilterMatch("gallery", "widget gallery");
            page.Text(state.filter_hit ? "filter_match：命中" : "filter_match：未命中");
            page.Separator();
            static_cast<void>(page.Checkbox("启用", &state.enabled));
            static_cast<void>(page.Checkbox("仅开发者可见", &state.developer_only));
            static_cast<void>(page.Slider("缩放", &state.scale, 0.25F, 2.0F));
            static_cast<void>(
                page.SliderEnabled("权重", &state.weight, 0.0F, 1.0F, state.enabled != 0));
            static_cast<void>(page.Color("主题色", state.accent));
            static_cast<void>(
                page.InputUint32("间隔（毫秒）", &state.interval_milliseconds, 10U, 100U));
            static_cast<void>(page.InputDouble("平滑", &state.smoothing, 0.01, 0.1));
            static_cast<void>(page.InputText("标签", state.label.data(), state.label.size(),
                ANOMALY_UI_TEXT_INPUT_V1_NONE));
            page.Separator();
            if (page.Button("按钮", 110.0F, 0.0F)) {
                ++state.clicks;
                state.status = "按下了「按钮」";
            }
            page.SameLine();
            if (page.Button("同行按钮", 120.0F, 0.0F)) {
                state.status = "按下了「同行按钮」";
            }
            page.SameLine();
            if (page.ButtonEnabled("禁用态按钮", state.enabled != 0, 130.0F, 0.0F)) {
                state.status = "按下了「禁用态按钮」";
            }
            page.SetCursorPosX(24.0F);
            page.Text("set_cursor_pos_x 把这行右移了 24 像素。");
            page.Separator();
            page.Text("点击次数：" + std::to_string(state.clicks));
            page.Text("状态：" + state.status);
            page.EndTabItem();
        }

        if (page.BeginTabItem("布局")) {
            page.Text("窗口尺寸：" + std::to_string(static_cast<int>(state.window_width)) +
                " x " + std::to_string(static_cast<int>(state.window_height)));
            page.Text("set_next_window_size_constraints 与 get_window_size 都已生效。");
            page.Separator();
            if (page.BeginChild("gallery-child", 0.0F, 92.0F, 0)) {
                page.Text("begin_child / end_child 滚动区域");
                for (int row = 0; row != 12; ++row) {
                    page.Text("滚动行 " + std::to_string(row));
                }
                page.EndChild();
            }
            if (page.BeginChildAutoFitY("gallery-fit-child", 0.0F, 120.0F)) {
                page.Text("begin_child_auto_fit_y 会把这块区域撑到内容高度。");
                page.Text("第二行");
                page.EndChild();
            }
            page.Separator();
            if (page.BeginTable("gallery-table", 3, ANOMALY_UI_TABLE_V1_SIZING_FIXED_FIT,
                    0.0F, 0.0F)) {
                page.TableNextRow();
                static_cast<void>(page.TableNextColumn());
                page.Text("控件");
                static_cast<void>(page.TableNextColumn());
                page.Text("值");
                static_cast<void>(page.TableNextColumn());
                page.Text("状态");
                for (int row = 0; row != state.table_rows; ++row) {
                    page.TableNextRow();
                    static_cast<void>(page.TableNextColumn());
                    page.Text("行 " + std::to_string(row));
                    static_cast<void>(page.TableNextColumn());
                    page.Text("值 " + std::to_string(row));
                    static_cast<void>(page.TableNextColumn());
                    page.Text("正常");
                }
                page.EndTable();
            }
            page.EndTabItem();
        }

        if (page.BeginTabItem("叠加层")) {
            page.TextWrapped(
                "三个 ESP 入口用下面这台演示相机把结果画到宿主的前景绘制列表上："
                "包围矩形、投影后的 3D 盒子，以及一行文字标签。");
            page.Separator();
            const AnomalyEspCameraV1 camera = detail::DemoTargetCamera();
            const AnomalyEspEntityBoundsV1 bounds = detail::DemoTargetBounds();
            if (state.show_overlay != 0) {
                const int rect = page.DrawEntityBbox(camera, bounds,
                    detail::DemoBoxStyle(ANOMALY_RGBA_V1(90, 220, 140, 235)));
                const int box = page.DrawEntityBox3d(camera, bounds,
                    detail::DemoBoxStyle(ANOMALY_RGBA_V1(110, 190, 255, 235)));
                const int label = page.DrawEntityLabel(camera, bounds, "演示目标",
                    ANOMALY_RGBA_V1(255, 230, 150, 255));
                page.Text("draw_entity_bbox：" + std::to_string(rect));
                page.Text("draw_entity_box3d：" + std::to_string(box));
                page.Text("draw_entity_label：" + std::to_string(label));
            } else {
                page.Text("叠加层绘制已关闭");
            }
            static_cast<void>(page.Checkbox("显示叠加层", &state.show_overlay));
            page.EndTabItem();
        }

        if (page.BeginTabItem("交互")) {
            page.Text(state.frame_state != 0U
                ? "frame_state：当前有交互"
                : "frame_state：当前没有交互");
            page.Text("frame_state 位掩码：" + std::to_string(state.frame_state));
            page.Text(page.DeveloperModeEnabled() != 0
                ? "developer_mode_enabled：1"
                : "developer_mode_enabled：0");
            page.Separator();
            static_cast<void>(
                page.TextLink("text_link（点击后由宿主打开链接）", "https://example.com/anomaly"));
            if (page.Button("从这里打开弹窗", 200.0F, 0.0F)) {
                page.OpenPopup("gallery-popup");
            }
            page.EndTabItem();
        }
        page.EndTabBar();
    }

    if (page.BeginPopupModal("gallery-popup", nullptr, 0)) {
        page.Text("begin_popup_modal / end_popup / close_current_popup");
        page.Separator();
        if (page.Button("关闭", 100.0F, 0.0F)) {
            ++state.popup_clicks;
            page.CloseCurrentPopup();
        }
        page.EndPopup();
    }
    int modal_open = state.popup_open ? 1 : 0;
    if (page.BeginPopupModal("gallery-modal", &modal_open, 0)) {
        page.Text("一个带显式开关的模态框。");
        page.Separator();
        if (page.Button("确定", 100.0F, 0.0F)) {
            modal_open = 0;
            state.popup_open = false;
            page.CloseCurrentPopup();
        }
        page.EndPopup();
    }
    state.popup_open = modal_open != 0;

    page.EndWindow();
}

}  // namespace anomaly::example::gallery
