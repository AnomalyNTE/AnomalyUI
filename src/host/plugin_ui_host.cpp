// The UI half of the Anomaly plugin host.
//
// Anomaly's runtime owns a full plugin manager: it maps entry binaries, publishes
// scoped platform services, drains callback generations and isolates faults.
// None of that belongs to the user interface, so the extraction keeps only what
// the management surface talks to: the package catalog (read as data), the UI
// resource registry, the `anomaly.ui` service table and the page draw path. No
// binary is ever mapped or called from here.

#include "plugin_manager.hpp"

#include "demo_plugin_pages.hpp"

#include <anomaly/sdk/services/ui.h>

#include <algorithm>
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <utility>

namespace ue5mem {
namespace {

[[nodiscard]] bool HasDemoPage(const std::string_view plugin_id) noexcept {
    const auto pages = anomaly::ui_preview::DemoPluginPages();
    return std::any_of(pages.begin(), pages.end(),
        [plugin_id](const anomaly::ui_preview::DemoPluginPage& page) {
            return page.plugin_id == plugin_id;
        });
}

[[nodiscard]] std::string JoinIssues(const anomaly::PluginCatalogEntry& entry) {
    std::string reason;
    for (const auto& issue : entry.issues) {
        if (!reason.empty()) reason += "; ";
        reason += issue.code;
        if (!issue.message.empty()) {
            reason += ": ";
            reason += issue.message;
        }
    }
    return reason;
}

[[nodiscard]] std::string CatalogStateName(const anomaly::PluginCatalogStatus status) {
    switch (status) {
    case anomaly::PluginCatalogStatus::Valid:
        return "active";
    case anomaly::PluginCatalogStatus::InvalidManifest:
        return "invalid-manifest";
    case anomaly::PluginCatalogStatus::InvalidPackage:
        return "invalid-package";
    case anomaly::PluginCatalogStatus::DuplicateId:
        return "duplicate-id";
    case anomaly::PluginCatalogStatus::Incompatible:
        return "incompatible";
    }
    return "unknown";
}

}  // namespace

struct PluginManager::State final {
    std::filesystem::path root;
    std::filesystem::path plugin_directory;
    std::shared_ptr<anomaly::UiResourceRegistry> ui_resources =
        std::make_shared<anomaly::UiResourceRegistry>();
    // Empty by construction: the preview publishes no memory provider, so the
    // developer page reports the service as unavailable and refuses to act.
    anomaly::CoreMemoryServices memory_services;
    std::shared_ptr<const anomaly::Translator> translator;
    std::shared_ptr<anomaly::UiResourceRenderBackend> render_backend;

    mutable std::mutex mutex;
    std::vector<PluginView> plugins;
    std::vector<std::string> events;
    std::vector<bool> enabled;
    // 页面首次出现（或重新加载后）时需要置前一次。
    std::unordered_set<std::string> focus_pending;

    const AnomalyUiServiceV1* ui_service{};
    std::function<void()> nte_esc_menu_action;
    void* imgui_context{};
    std::atomic_bool load_step_in_progress{};
};

PluginManager::PluginManager(
    std::filesystem::path root, std::filesystem::path plugin_directory)
    : state_(std::make_unique<State>()) {
    state_->root = std::move(root);
    // The configured plugin directory is relative to the data root unless it is
    // absolute, which is what the runtime host does too.
    state_->plugin_directory = plugin_directory.is_absolute()
        ? std::move(plugin_directory)
        : state_->root / plugin_directory;
}

PluginManager::~PluginManager() = default;

void PluginManager::RunLifecycleOperation(
    std::mutex& execution_mutex, std::function<void()> operation) {
    // The preview has no asynchronous lifecycle domain, so a mutation runs
    // inline while the caller's gate is held.
    std::scoped_lock lock(execution_mutex);
    if (operation) operation();
}

bool PluginManager::PluginLoadStepInProgress() const noexcept {
    return state_->load_step_in_progress.load(std::memory_order_relaxed);
}

void PluginManager::LoadAll() {
    state_->load_step_in_progress.store(true, std::memory_order_relaxed);
    std::vector<PluginView> views;
    std::vector<bool> enabled;
    std::vector<std::string> events;
    try {
        const auto catalog = anomaly::DiscoverPluginCatalog(state_->plugin_directory);
        for (const auto& entry : catalog.Entries()) {
            PluginView view;
            view.package_directory = entry.package_root;
            view.source = entry.manifest_file;
            view.generation = 1;
            view.state = CatalogStateName(entry.status);
            if (entry.manifest) {
                const auto& manifest = *entry.manifest;
                view.id = manifest.id;
                view.name = manifest.name.empty() ? manifest.id : manifest.name;
                view.author = manifest.author;
                view.version = manifest.version.ToString();
                for (const auto& service : manifest.services) {
                    view.platform_diagnostics.services.push_back({service.id, service.minimum_version});
                }
                view.platform_diagnostics.capabilities = manifest.capabilities;
                view.platform_diagnostics.resources.windows = HasDemoPage(manifest.id) ? 1 : 0;
            } else {
                view.id = entry.package_root.filename().string();
                view.name = view.id;
            }
            if (!entry.LoadCandidate()) {
                view.status_reason = JoinIssues(entry);
                if (view.status_reason.empty()) view.status_reason = "package rejected by the catalog";
                view.enabled = false;
            }
            views.push_back(std::move(view));
            enabled.push_back(views.back().enabled);
        }
        events.push_back(
            "Read " + std::to_string(views.size()) + " package manifest(s) from " +
            state_->plugin_directory.string() +
            " (UI preview: manifests are data only, no entry binary is loaded)");
    } catch (...) {
        events.push_back("Package catalog could not be read");
    }

    std::scoped_lock lock(state_->mutex);
    state_->plugins = std::move(views);
    state_->enabled.clear();
    state_->enabled.reserve(state_->plugins.size());
    for (std::size_t index = 0; index != state_->plugins.size(); ++index) {
        state_->enabled.push_back(state_->plugins[index].enabled);
    }
    state_->events.insert(state_->events.end(), events.begin(), events.end());
    for (const auto& plugin : state_->plugins) {
        if (plugin.enabled && plugin.state == "active") state_->focus_pending.insert(plugin.id);
    }
    state_->load_step_in_progress.store(false, std::memory_order_relaxed);
}

void PluginManager::ReloadAll() {
    {
        std::scoped_lock lock(state_->mutex);
        state_->events.push_back("Catalog re-read requested");
    }
    LoadAll();
}

bool PluginManager::Reload(std::string_view plugin_id) {
    std::scoped_lock lock(state_->mutex);
    const auto found = std::find_if(state_->plugins.begin(), state_->plugins.end(),
        [plugin_id](const PluginView& plugin) { return plugin.id == plugin_id; });
    if (found == state_->plugins.end()) return false;
    ++found->generation;
    state_->focus_pending.insert(found->id);
    state_->events.push_back("Re-read " + std::string(plugin_id) + " manifest");
    return true;
}

bool PluginManager::SetEnabled(std::string_view plugin_id, bool enabled) {
    std::scoped_lock lock(state_->mutex);
    for (std::size_t index = 0; index != state_->plugins.size(); ++index) {
        if (state_->plugins[index].id != plugin_id) continue;
        if (state_->plugins[index].state != "active") return false;
        state_->plugins[index].enabled = enabled;
        if (index < state_->enabled.size()) state_->enabled[index] = enabled;
        state_->events.push_back(
            std::string(enabled ? "Enabled " : "Disabled ") + std::string(plugin_id));
        return true;
    }
    return false;
}

bool PluginManager::SetVisible(std::string_view plugin_id, bool visible) {
    std::scoped_lock lock(state_->mutex);
    for (auto& plugin : state_->plugins) {
        if (plugin.id != plugin_id) continue;
        plugin.visible = visible;
        return true;
    }
    return false;
}

void PluginManager::UnloadAll() {
    std::scoped_lock lock(state_->mutex);
    state_->plugins.clear();
    state_->enabled.clear();
    state_->events.push_back("Catalog cleared");
}

bool PluginManager::StopForRuntime(std::chrono::milliseconds) {
    UnloadAll();
    return true;
}

void PluginManager::Maintenance() {
    // Nothing to poll: manifests are read on demand and no worker owns state.
}

PluginMaintenanceTiming PluginManager::MaintenancePluginState() {
    return {};
}

void PluginManager::PersistUiWindowState() {
    // The preview keeps managed-window geometry in the live registry only. The
    // runtime host is the one that writes `ui-window-state.json`.
}

void PluginManager::GameUpdate(double delta_seconds) {
    anomaly::ui_preview::AdvanceDemoPlugins(delta_seconds);
}

void PluginManager::Draw(void*) {
    // The host table is process-lifetime and ignores `user`; it is published by
    // the shell only while a frame owns the ImGui context.
    const AnomalyUiServiceV1* const ui = state_->ui_service;
    if (ui == nullptr || ui->begin_window == nullptr || ui->end_window == nullptr) return;
    std::vector<PluginView> plugins;
    {
        std::scoped_lock lock(state_->mutex);
        plugins = state_->plugins;
    }
    const auto pages = anomaly::ui_preview::DemoPluginPages();
    for (const auto& plugin : plugins) {
        if (!plugin.enabled || plugin.state != "active") continue;
        const auto page = std::find_if(pages.begin(), pages.end(),
            [&plugin](const anomaly::ui_preview::DemoPluginPage& candidate) {
                return candidate.plugin_id == plugin.id;
            });
        if (page == pages.end() || page->draw == nullptr) continue;
        // A page opens as a floating window of its own. It is raised once when it
        // first appears (and again after a reload), so the management shell stays
        // usable afterwards instead of losing the front position every frame.
        if (state_->focus_pending.erase(plugin.id) != 0U) {
            ImGui::SetNextWindowFocus();
        }
        page->draw(*ui);
    }
}

void PluginManager::SetImGuiContext(void* imgui_context) noexcept {
    state_->imgui_context = imgui_context;
}

void PluginManager::SetUiService(const AnomalyUiServiceV1* service) {
    std::scoped_lock lock(state_->mutex);
    state_->ui_service = service;
}

void PluginManager::SetNteEscMenuHostAction(std::function<void()> action) noexcept {
    state_->nte_esc_menu_action = std::move(action);
}

void PluginManager::SetTranslator(std::shared_ptr<const anomaly::Translator> translator) noexcept {
    state_->translator = std::move(translator);
}

void PluginManager::SetUiResourceRenderBackend(
    std::shared_ptr<anomaly::UiResourceRenderBackend> backend) noexcept {
    state_->render_backend = std::move(backend);
}

void PluginManager::PrepareUiResources() noexcept {}

void PluginManager::PrepareUiTexture(
    const std::shared_ptr<anomaly::PluginScope>&, anomaly::UiResourceHandle) noexcept {}

bool PluginManager::PushUiFont(
    const std::shared_ptr<anomaly::PluginScope>&, anomaly::UiResourceHandle) noexcept {
    return false;
}

bool PluginManager::PopUiFont() noexcept {
    return false;
}

bool PluginManager::DrawUiTexture(
    const std::shared_ptr<anomaly::PluginScope>&, anomaly::UiResourceHandle,
    float, float, std::uint32_t) noexcept {
    return false;
}

bool PluginManager::DrawUiTextureEx(
    const std::shared_ptr<anomaly::PluginScope>&, anomaly::UiResourceHandle,
    const anomaly::UiTextureDrawRequest&) noexcept {
    return false;
}

bool PluginManager::QueueUiFontLoad(
    const std::shared_ptr<anomaly::PluginScope>& scope,
    anomaly::UiResourceHandle handle) noexcept {
    return state_->ui_resources != nullptr &&
        state_->ui_resources->MarkFontFailed(scope, handle);
}

bool PluginManager::QueueUiTextureLoad(
    const std::shared_ptr<anomaly::PluginScope>& scope,
    anomaly::UiResourceHandle handle) noexcept {
    // No backend exists to decode and upload, so the request is settled as
    // failed immediately rather than left queued forever.
    return state_->ui_resources != nullptr &&
        state_->ui_resources->MarkTextureFailed(scope, handle);
}

std::vector<PluginView> PluginManager::Plugins() const {
    std::scoped_lock lock(state_->mutex);
    return state_->plugins;
}

PluginRuntimeDiagnosticsSnapshot PluginManager::DiagnosticsSnapshot() const {
    PluginRuntimeDiagnosticsSnapshot snapshot;
    snapshot.plugins = Plugins();
    return snapshot;
}

std::string PluginManager::DiagnosticsJson() const {
    const auto plugins = Plugins();
    std::string document = "{\"schemaVersion\":1,\"plugins\":[";
    for (std::size_t index = 0; index != plugins.size(); ++index) {
        const auto& plugin = plugins[index];
        if (index != 0) document += ",";
        document += "{\"id\":\"" + plugin.id + "\",\"state\":\"" + plugin.state +
            "\",\"enabled\":" + (plugin.enabled ? "true" : "false") + "}";
    }
    document += "]}";
    return document;
}

std::vector<std::string> PluginManager::Events() const {
    std::scoped_lock lock(state_->mutex);
    return state_->events;
}

const std::filesystem::path& PluginManager::Directory() const noexcept {
    return state_->plugin_directory;
}

const std::filesystem::path& PluginManager::Root() const noexcept {
    return state_->root;
}

const anomaly::CoreMemoryServices& PluginManager::MemoryServices() const noexcept {
    return state_->memory_services;
}

const AnomalyUiServiceV1* PluginManager::UiService() const noexcept {
    return state_->ui_service;
}

anomaly::UiResourceRegistry& PluginManager::UiResources() noexcept {
    return *state_->ui_resources;
}

const anomaly::UiResourceRegistry& PluginManager::UiResources() const noexcept {
    return *state_->ui_resources;
}

std::filesystem::path PluginManager::PackageDirectory(std::string_view plugin_id) const {
    std::scoped_lock lock(state_->mutex);
    for (const auto& plugin : state_->plugins) {
        if (plugin.id == plugin_id) return plugin.package_directory;
    }
    return {};
}

void PluginManager::Log(AnomalyCoreLogLevelV1, std::string message) {
    std::scoped_lock lock(state_->mutex);
    state_->events.push_back(std::move(message));
}

}  // namespace ue5mem
