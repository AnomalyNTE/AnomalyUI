#pragma once

#include "anomaly/sdk/anomaly_sdk.h"
#include "anomaly/ipc_registry.hpp"
#include "anomaly/pattern_service.hpp"
#include "anomaly/plugin_catalog.hpp"
#include "anomaly/plugin_manifest.hpp"
#include "anomaly/plugin_scope.hpp"
#include "anomaly/ui_resource_registry.hpp"
#include "anomaly/ui_resource_render_backend.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace anomaly {
class Translator;
}

// The plugin-facing view of the management surface.
//
// AnomalyUI ships the UI half of the Anomaly plugin system: package discovery,
// the manifest/enablement model, the resource registry and the `anomaly.ui` C
// ABI that plugin pages call. It deliberately contains no loader: nothing under
// this repository maps, calls or executes a plugin binary, and the pages the
// shell draws come from this repository's own demo pages. A host that owns a
// real loader supplies the same surface.
namespace ue5mem {

struct CallbackMetricsView {
    std::uint64_t calls{};
    std::uint64_t faults{};
    std::uint64_t slow_calls{};
    double p50_milliseconds{};
    double p95_milliseconds{};
    double p99_milliseconds{};
};

struct PluginServiceVersionView {
    std::string id;
    std::uint32_t version{};
};

struct PluginResourceCountsView {
    std::size_t ledger_resources{};
    std::size_t configs{};
    std::size_t self_tests{};
    std::size_t tasks{};
    std::size_t ipc_resources{};
    std::size_t commands{};
    std::size_t notifications{};
    std::size_t hooks{};
    std::size_t patches{};
    std::size_t windows{};
    std::size_t fonts{};
    std::size_t textures{};
    std::size_t hotkeys{};
    std::size_t nte_esc_menu_buttons{};
};

struct PluginScopedCallbackMetricsView {
    std::uint64_t calls{};
    std::uint64_t faults{};
    std::uint64_t slow_calls{};
};

struct PluginPlatformDiagnosticsView {
    bool capability_enforced{};
    std::vector<std::string> capabilities;
    std::vector<PluginServiceVersionView> services;
    PluginResourceCountsView resources;
    std::size_t queued_tasks{};
    PluginScopedCallbackMetricsView scoped_callbacks;
    std::vector<anomaly::IpcEndpointDiagnostics> ipc_endpoints;
    std::vector<std::string> deny_reasons;
};

struct PluginView {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::filesystem::path source;
    std::filesystem::path package_directory;
    bool visible{true};
    bool visibility_control{};
    bool enabled{true};
    std::uint64_t generation{};
    std::string state{"active"};
    std::string status_reason;
    CallbackMetricsView update_metrics;
    CallbackMetricsView draw_metrics;
    PluginPlatformDiagnosticsView platform_diagnostics;
};

struct PluginRuntimeDiagnosticsSnapshot {
    std::uint32_t schema_version{1};
    std::vector<PluginView> plugins;
};

struct PluginMaintenanceTiming {
    std::chrono::steady_clock::duration retry_waiting_for_services{};
    std::chrono::steady_clock::duration poll_for_changes{};
};

class PluginManager {
public:
    PluginManager(
        std::filesystem::path root,
        std::filesystem::path plugin_directory);
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Serializes a lifecycle mutation against draw/update, exactly as the
    // desktop shell expects from a host that owns plugin generations.
    void RunLifecycleOperation(std::mutex& execution_mutex, std::function<void()> operation);
    [[nodiscard]] bool PluginLoadStepInProgress() const noexcept;

    // Reads the staged package manifests. Manifests are parsed as data; the
    // entry binaries they name are never mapped.
    void LoadAll();
    void ReloadAll();
    bool Reload(std::string_view plugin_id);
    bool SetEnabled(std::string_view plugin_id, bool enabled);
    bool SetVisible(std::string_view plugin_id, bool visible);
    void UnloadAll();
    bool StopForRuntime(std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

    // Worker/lifecycle maintenance. The desktop shell calls this once per frame
    // while no load step is in flight.
    void Maintenance();
    [[nodiscard]] PluginMaintenanceTiming MaintenancePluginState();
    void PersistUiWindowState();
    void GameUpdate(double delta_seconds);
    // Draws every running plugin page through the `anomaly.ui` service table.
    void Draw(void* imgui_context);
    void SetImGuiContext(void* imgui_context) noexcept;
    void SetUiService(const AnomalyUiServiceV1* service);
    void SetNteEscMenuHostAction(std::function<void()> action) noexcept;
    void SetTranslator(std::shared_ptr<const anomaly::Translator> translator) noexcept;

    // UI resource pass-through. This build has no GPU resource backend, so a
    // texture or font request is accepted, reported as unavailable, and drawn
    // by nobody -- the desktop shell's standalone surface uploads its own theme
    // artwork and header logo instead.
    void SetUiResourceRenderBackend(
        std::shared_ptr<anomaly::UiResourceRenderBackend> backend) noexcept;
    void PrepareUiResources() noexcept;
    void PrepareUiTexture(
        const std::shared_ptr<anomaly::PluginScope>& scope,
        anomaly::UiResourceHandle handle) noexcept;
    [[nodiscard]] bool PushUiFont(
        const std::shared_ptr<anomaly::PluginScope>& scope,
        anomaly::UiResourceHandle handle) noexcept;
    [[nodiscard]] bool PopUiFont() noexcept;
    [[nodiscard]] bool DrawUiTexture(
        const std::shared_ptr<anomaly::PluginScope>& scope,
        anomaly::UiResourceHandle handle, float width, float height,
        std::uint32_t tint_rgba) noexcept;
    [[nodiscard]] bool DrawUiTextureEx(
        const std::shared_ptr<anomaly::PluginScope>& scope,
        anomaly::UiResourceHandle handle,
        const anomaly::UiTextureDrawRequest& request) noexcept;
    [[nodiscard]] bool QueueUiFontLoad(
        const std::shared_ptr<anomaly::PluginScope>& scope,
        anomaly::UiResourceHandle handle) noexcept;
    [[nodiscard]] bool QueueUiTextureLoad(
        const std::shared_ptr<anomaly::PluginScope>& scope,
        anomaly::UiResourceHandle handle) noexcept;

    [[nodiscard]] std::vector<PluginView> Plugins() const;
    [[nodiscard]] PluginRuntimeDiagnosticsSnapshot DiagnosticsSnapshot() const;
    [[nodiscard]] std::string DiagnosticsJson() const;
    [[nodiscard]] std::vector<std::string> Events() const;
    [[nodiscard]] const std::filesystem::path& Directory() const noexcept;
    [[nodiscard]] const std::filesystem::path& Root() const noexcept;
    [[nodiscard]] const anomaly::CoreMemoryServices& MemoryServices() const noexcept;
    [[nodiscard]] const AnomalyUiServiceV1* UiService() const noexcept;
    [[nodiscard]] anomaly::UiResourceRegistry& UiResources() noexcept;
    [[nodiscard]] const anomaly::UiResourceRegistry& UiResources() const noexcept;
    [[nodiscard]] std::filesystem::path PackageDirectory(std::string_view plugin_id) const;
    void Log(AnomalyCoreLogLevelV1 level, std::string message);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace ue5mem
