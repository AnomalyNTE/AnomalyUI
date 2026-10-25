// Packaged example plugin: the widget gallery page.
//
// This is a real plugin binary built against the pure C ABI, and it draws with
// the same `gallery_page.hpp` the preview uses. Note that AnomalyUI never loads
// it: the repository ships the package so the management surface has a manifest
// to list, and the preview renders the page from its own copy of the header.

#include "anomaly/sdk/cpp.hpp"

#include "gallery_page.hpp"

namespace {

const AnomalyUiServiceV1* g_ui{};
anomaly::example::gallery::GalleryState g_state;

AnomalyStatusV1 ANOMALY_CALL Load(const AnomalyHostApiV1* host, void** context) {
    const auto ui = anomaly::sdk::Host(host).Query<AnomalyUiServiceV1>(
        ANOMALY_UI_SERVICE_V1_ID, ANOMALY_UI_SERVICE_V1_VERSION);
    if (!ui) return {ANOMALY_STATUS_V1_UNAVAILABLE, 0, {}};
    g_ui = ui.get();
    if (context != nullptr) *context = &g_state;
    return anomaly::sdk::Ok();
}

AnomalyStatusV1 ANOMALY_CALL Start(void*) { return anomaly::sdk::Ok(); }

AnomalyStatusV1 ANOMALY_CALL Stop(void*, std::uint32_t) { return anomaly::sdk::Ok(); }

void ANOMALY_CALL Unload(void*) {
    g_ui = nullptr;
    g_state = {};
}

void ANOMALY_CALL Update(void* context, double) {
    static_cast<void>(context);
}

void ANOMALY_CALL Draw(void* context, const AnomalyUiServiceV1* ui) {
    if (ui == nullptr) ui = g_ui;
    if (ui == nullptr) return;
    auto* state = context != nullptr
        ? static_cast<anomaly::example::gallery::GalleryState*>(context) : &g_state;
    anomaly::example::gallery::DrawGalleryPage(*ui, *state);
}

}  // namespace

ANOMALY_SDK_EXPORT AnomalyStatusV1 ANOMALY_CALL AnomalyPluginEntryV1(
    AnomalyPluginDescriptorV1* descriptor) {
    if (descriptor == nullptr || descriptor->struct_size < sizeof(*descriptor)) {
        return {ANOMALY_STATUS_V1_INVALID_ARGUMENT, 0, {}};
    }
    *descriptor = {
        sizeof(*descriptor), ANOMALY_PLUGIN_API_V1_MAJOR, ANOMALY_PLUGIN_API_V1_MINOR,
        anomaly::sdk::StringView("anomaly.example.widget-gallery"),
        anomaly::sdk::StringView("Widget Gallery"),
        anomaly::sdk::StringView("Anomaly"), anomaly::sdk::StringView("1.0.0"),
        Load, Start, Stop, Unload, Update, Draw};
    return anomaly::sdk::Ok();
}
