// Standalone preview for the AnomalyUI platform surface.
//
// This is the AnomalyUI counterpart of Anomaly's `anomaly-platform-preview`
// fixture: it opens the management shell in a real Win32 window with a D3D11
// swap chain, with no game process, no runtime session and no loader involved.
// Everything the shell can show comes from this repository -- the plugin list is
// read from staged manifests (data only), the plugin pages are the preview's own
// demo pages drawn through the plugin UI ABI, and the diagnostics providers below
// publish a synthetic snapshot so every page has content to lay out.

#include "anomaly/i18n.hpp"
#include "anomaly/platform_settings.hpp"
#include "anomaly/platform_ui_theme.hpp"
#include "anomaly/sdk/version.h"

#include "config.hpp"
#include "platform_host.hpp"
#include "plugin_manager.hpp"

#include <Windows.h>
#include <objbase.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

// `--palette=naiwa` selects the theme palette the shell resolves its artwork for.
[[nodiscard]] std::string PreviewPaletteArgument() {
    const std::wstring command_line = GetCommandLineW();
    constexpr std::wstring_view prefix = L"--palette=";
    const std::size_t start = command_line.find(prefix);
    if (start == std::wstring::npos) return "naiwa";
    const std::size_t value_start = start + prefix.size();
    const std::size_t value_end = command_line.find_first_of(L" \t\r\n", value_start);
    const std::wstring value = command_line.substr(
        value_start, value_end == std::wstring::npos ? std::wstring::npos : value_end - value_start);
    std::string result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character >= L'A' && character <= L'Z') {
            result.push_back(static_cast<char>(character - L'A' + L'a'));
        } else if (character >= L'a' && character <= L'z') {
            result.push_back(static_cast<char>(character));
        }
    }
    return result.empty() ? "naiwa" : result;
}

// A service graph describing the pieces this preview actually runs. It is the
// same table the shell renders for a live host; nothing here claims a game.
[[nodiscard]] anomaly::ServiceGraphSnapshot PreviewServiceGraph() {
    anomaly::ServiceGraphSnapshot graph;
    graph.built = true;
    graph.blocking_startup_complete = true;
    graph.async_startup_complete = true;
    graph.error = ERROR_SUCCESS;
    const auto service = [](std::string id, const std::uint32_t version,
                             const anomaly::ServiceAffinity affinity,
                             const anomaly::ServiceStartup startup) {
        anomaly::ServiceSnapshot snapshot;
        snapshot.id = std::move(id);
        snapshot.version = version;
        snapshot.affinity = affinity;
        snapshot.startup = startup;
        snapshot.state = anomaly::ServiceState::Ready;
        snapshot.startup_duration = std::chrono::microseconds(120);
        snapshot.start_thread_id = GetCurrentThreadId();
        return snapshot;
    };
    graph.services.push_back(service("anomaly.ui.host", 1,
        anomaly::ServiceAffinity::Render, anomaly::ServiceStartup::Blocking));
    graph.services.push_back(service("anomaly.ui.resources", 1,
        anomaly::ServiceAffinity::Render, anomaly::ServiceStartup::Lazy));
    graph.services.push_back(service("anomaly.plugin.catalog", 1,
        anomaly::ServiceAffinity::Worker, anomaly::ServiceStartup::Async));
    graph.services.push_back(service("anomaly.platform.settings", 1,
        anomaly::ServiceAffinity::Lifecycle, anomaly::ServiceStartup::Blocking));
    return graph;
}

// Sample repository rows. The shell's Available/Updates tabs lay out whatever a
// repository provider publishes, so the preview publishes a fixed sample list
// instead of contacting a network source.
[[nodiscard]] anomaly::RepositoryCoordinatorSnapshot PreviewRepository() {
    anomaly::RepositoryCoordinatorSnapshot snapshot;
    snapshot.state = anomaly::RepositoryCoordinatorState::Ready;
    snapshot.reason = "sample index published by the UI preview";
    snapshot.configured_sources = 1;
    snapshot.cached_sources = 1;
    const auto row = [](std::string internal_name, std::string name, std::string version,
                        std::string author, std::string punchline) {
        anomaly::RepositoryPluginView view;
        view.entry.internal_name = std::move(internal_name);
        view.entry.name = std::move(name);
        view.entry.version = std::move(version);
        view.entry.author = std::move(author);
        view.entry.punchline = std::move(punchline);
        view.entry.games = {"nte"};
        view.entry.api_major = 1;
        view.source_id = "sample index";
        view.compatible = true;
        return view;
    };
    snapshot.plugins.push_back(row("anomaly.example.hello-ui", "Hello UI", "1.1.0", "Anomaly",
        "Reference package for the plugin UI surface"));
    snapshot.plugins.push_back(row("anomaly.example.tick-counter", "Game Tick Counter", "1.0.2",
        "Anomaly", "Counts host update ticks"));
    snapshot.plugins.push_back(row("anomaly.example.reliable-config", "Reliable Config", "1.2.0",
        "Anomaly", "Configuration page built from the widget ABI"));
    return snapshot;
}

[[nodiscard]] anomaly::RepositoryOperationSubmission PreviewRepositoryOperation() {
    return {false, 0, "UI preview build: repository operations are not part of this extraction"};
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // The launcher and the injected host both own a single-threaded apartment on
    // the thread that drives the platform UI, and the shell file dialog only
    // builds its view there. Entering it first keeps this preview faithful to
    // the hosts it stands in for.
    const HRESULT com =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    static_cast<void>(com);
    const auto palette = ue5mem::ParsePlatformUiPalette(PreviewPaletteArgument());
    ue5mem::SetPlatformUiPalette(palette);
    const auto root = ExecutableDirectory() / L"Anomaly";
    auto config = ue5mem::AnalyzerConfig::Load(root / L"anomaly.ini");
    const auto locale = anomaly::ResolveUserLocale(config.platform_language);
    ue5mem::PlatformDiagnostics diagnostics;
    diagnostics.runtime_root = root;
    diagnostics.runtime_version = std::string(ANOMALY_SDK_VERSION_STRING) + " (ui preview)";
    diagnostics.translator = anomaly::LoadHostCatalog(
        locale.locale, root / L"locales" / L"host").translator;
    config.platform_enabled = true;
    config.platform_visible = true;
    config.platform_embedded = false;
    config.platform_attach_to_process_window = false;
    auto plugins = std::make_shared<ue5mem::PluginManager>(root, config.plugin_directory);
    plugins->SetTranslator(diagnostics.translator);
    plugins->LoadAll();
    std::filesystem::create_directories(root / L"config");
    auto settings = std::make_shared<anomaly::PlatformSettingsStore>(root);
    static_cast<void>(settings->Start());
    diagnostics.settings_snapshot = [settings] { return settings->Snapshot(); };
    diagnostics.settings_apply = [settings](const anomaly::PlatformSettingsApplyRequest& request) {
        return settings->Apply(request);
    };
    diagnostics.settings_record_route = [settings](const std::string_view route) {
        return settings->RecordLastRoute(route);
    };
    diagnostics.service_graph = [] { return PreviewServiceGraph(); };
    diagnostics.repository_snapshot = [] { return PreviewRepository(); };
    diagnostics.repository_refresh = [] { return PreviewRepositoryOperation(); };
    diagnostics.repository_install = [](std::string_view, std::string_view) {
        return PreviewRepositoryOperation();
    };
    diagnostics.repository_uninstall = [](std::string_view) {
        return PreviewRepositoryOperation();
    };
    diagnostics.performance_probe_enabled = [] { return false; };
    ue5mem::RunPlatform(root, config, {}, {}, {}, std::move(diagnostics), plugins);
    static_cast<void>(plugins->StopForRuntime());
    return 0;
}
