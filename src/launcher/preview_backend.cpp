// The launcher's UI with nothing behind it.
//
// AnomalyUI ships the launcher *window*: layout, theme, locale switching, client
// selection, status cards and diagnostics. What it deliberately does not ship is
// the launcher's job -- discovering an installed client, installing the proxy
// layer next to the game, attaching to a running process and mapping the runtime
// into it. Each entry point below is the same signature the real launcher
// backend exposes, and each one either reports the state as unavailable or keeps
// the launcher's own UI settings in a JSON file beside the launcher.
//
// This is the only file in the repository whose behaviour differs from the
// Anomaly runtime build; see README.md ("What was left behind").

#include "anomaly/launcher/configuration.hpp"
#include "anomaly/launcher/manual_map.hpp"
#include "anomaly/launcher/proxy_installation.hpp"
#include "anomaly/runtime_launch.hpp"
#include "anomaly/runtime_recovery.hpp"

#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace anomaly::launcher {
namespace {

constexpr std::string_view kPreviewNotice =
    "This build contains the launcher UI only: it does not install, attach to or "
    "start the game.";

[[nodiscard]] std::string Utf8(const std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

[[nodiscard]] std::wstring Wide(const std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

[[nodiscard]] bool NameEquals(const std::filesystem::path& path, const std::wstring_view expected) {
    std::wstring name = path.filename().wstring();
    std::wstring target(expected);
    const auto lower = [](std::wstring& value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](const wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    };
    lower(name);
    lower(target);
    return name == target;
}

[[nodiscard]] std::wstring_view LauncherExecutableName(const NteClient client) noexcept {
    return client == NteClient::Global ? L"NTEGlobalLauncher.exe" : L"NTELauncher.exe";
}

void WritePath(nlohmann::json& document, const char* key, const std::filesystem::path& value) {
    document[key] = value.empty() ? std::string{} : Utf8(value.wstring());
}

[[nodiscard]] std::filesystem::path ReadPath(const nlohmann::json& document, const char* key) {
    if (!document.contains(key) || !document.at(key).is_string()) return {};
    const auto value = document.at(key).get<std::string>();
    if (value.empty()) return {};
    return std::filesystem::path(Wide(value));
}

}  // namespace

// --- launcher configuration -------------------------------------------------
// The launcher's own settings, kept in a JSON file under its payload root. No
// registry writes, no install scanning.

std::filesystem::path LauncherConfigurationPath(const std::filesystem::path& payload_root) noexcept {
    return payload_root / L"config" / L"launcher.json";
}

LauncherConfigurationLoadResult LoadLauncherConfiguration(
    const std::filesystem::path& path) noexcept {
    LauncherConfigurationLoadResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.win32_error = ERROR_FILE_NOT_FOUND;
        result.message = "no launcher configuration has been saved yet";
        return result;
    }
    try {
        nlohmann::json document;
        stream >> document;
        if (!document.is_object()) {
            result.win32_error = ERROR_INVALID_DATA;
            result.message = "launcher configuration is not an object";
            return result;
        }
        if (document.contains("selectedClient") && document.at("selectedClient").is_string()) {
            result.configuration.selected_client =
                document.at("selectedClient").get<std::string>() == "global"
                    ? NteClient::Global : NteClient::MainlandChina;
        }
        if (document.contains("mainlandChina") && document.at("mainlandChina").is_object()) {
            const auto& entry = document.at("mainlandChina");
            result.configuration.mainland_china.game_directory = ReadPath(entry, "gameDirectory");
            result.configuration.mainland_china.launcher_executable =
                ReadPath(entry, "launcherExecutable");
        }
        if (document.contains("global") && document.at("global").is_object()) {
            const auto& entry = document.at("global");
            result.configuration.global.game_directory = ReadPath(entry, "gameDirectory");
            result.configuration.global.launcher_executable = ReadPath(entry, "launcherExecutable");
        }
        result.loaded = true;
        result.win32_error = ERROR_SUCCESS;
    } catch (const std::exception& error) {
        result.win32_error = ERROR_INVALID_DATA;
        result.message = error.what();
    }
    return result;
}

LauncherConfigurationSaveResult SaveLauncherConfiguration(
    const std::filesystem::path& path, const LauncherConfiguration& configuration) noexcept {
    LauncherConfigurationSaveResult result;
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    try {
        nlohmann::json document;
        document["schemaVersion"] = 1;
        document["selectedClient"] =
            configuration.selected_client == NteClient::Global ? "global" : "mainland";
        nlohmann::json mainland = nlohmann::json::object();
        WritePath(mainland, "gameDirectory", configuration.mainland_china.game_directory);
        WritePath(mainland, "launcherExecutable",
            configuration.mainland_china.launcher_executable);
        document["mainlandChina"] = std::move(mainland);
        nlohmann::json global = nlohmann::json::object();
        WritePath(global, "gameDirectory", configuration.global.game_directory);
        WritePath(global, "launcherExecutable", configuration.global.launcher_executable);
        document["global"] = std::move(global);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            result.win32_error = ERROR_ACCESS_DENIED;
            result.message = "launcher configuration could not be written";
            return result;
        }
        stream << document.dump(2);
    } catch (const std::exception& error) {
        result.win32_error = ERROR_INVALID_DATA;
        result.message = error.what();
    }
    return result;
}

bool IsNteGameDirectory(const std::filesystem::path& directory) noexcept {
    std::error_code code;
    if (directory.empty() || !std::filesystem::is_directory(directory, code)) return false;
    return NameEquals(directory / L"HTGame.exe", L"HTGame.exe") &&
        std::filesystem::is_regular_file(directory / L"HTGame.exe", code);
}

bool IsNteLauncherExecutable(
    const std::filesystem::path& executable, const NteClient client) noexcept {
    std::error_code code;
    if (!NameEquals(executable, LauncherExecutableName(client))) return false;
    return std::filesystem::is_regular_file(executable, code);
}

LauncherClientConfiguration DiscoverLauncherConfiguration(
    const LauncherClientConfiguration& preferred, const LauncherDiscoveryOptions& options) {
    static_cast<void>(options);
    // Nothing is scanned: the preview keeps whatever the user selected.
    return preferred;
}

NteClientLaunchCommand ResolveClientLaunchCommand(const std::filesystem::path& launcher_executable) {
    NteClientLaunchCommand command;
    command.failure = launcher_executable.empty()
        ? "no launcher executable is configured"
        : std::string(kPreviewNotice);
    return command;
}

// --- process inspection and mapping ----------------------------------------

AttachableProcess InspectAttachableProcess(DWORD) noexcept {
    return {};
}

std::vector<AttachableProcess> EnumerateAttachableProcesses(std::wstring_view) noexcept {
    return {};
}

ManualMapLaunchResult LaunchAndManualMapRuntimeCore(
    const ManualMapLaunchOptions&) noexcept {
    ManualMapLaunchResult result;
    result.mapping.error = ManualMapError::ImageUnavailable;
    result.mapping.message = std::string(kPreviewNotice);
    return result;
}

// --- proxy installation -----------------------------------------------------

ProxyInstallationStatus InspectProxyInstallation(
    const std::filesystem::path& game_directory, const ProxyInstallationSource&) {
    ProxyInstallationStatus status;
    status.state = ProxyInstallationState::Unavailable;
    status.error = ProxyInstallationError::SourceUnavailable;
    status.game_directory = game_directory;
    status.message = std::string(kPreviewNotice);
    return status;
}

ProxyInstallationStatus InstallProxyRuntime(
    const std::filesystem::path& game_directory, const ProxyInstallationSource& source) {
    return InspectProxyInstallation(game_directory, source);
}

ProxyInstallationStatus SetProxyEnabled(
    const std::filesystem::path& game_directory, const ProxyInstallationSource& source, bool) {
    return InspectProxyInstallation(game_directory, source);
}

}  // namespace anomaly::launcher

namespace anomaly {

// --- runtime payload -------------------------------------------------------

RuntimeLaunchResult ResolveRuntimeLaunch(const RuntimeLaunchOptions& options) noexcept {
    RuntimeLaunchResult result;
    result.error = RuntimeLaunchError::CoreUnavailable;
    result.runtime_root = options.installation_root;
    result.message =
        "This build contains the launcher UI only, so no runtime payload is resolved.";
    return result;
}

const char* RuntimeLaunchErrorName(const RuntimeLaunchError error) noexcept {
    switch (error) {
    case RuntimeLaunchError::None: return "none";
    case RuntimeLaunchError::InvalidRoot: return "invalid-root";
    case RuntimeLaunchError::CoreUnavailable: return "core-unavailable";
    }
    return "unknown";
}

// --- recovery state --------------------------------------------------------

class RuntimeRecoveryStore::Impl final {
public:
    explicit Impl(std::filesystem::path root) : root(std::move(root)) {}

    std::filesystem::path root;
};

RuntimeRecoveryStore::RuntimeRecoveryStore(
    std::filesystem::path runtime_root, RuntimeRecoveryPolicy policy) noexcept
    : impl_(std::make_unique<Impl>(std::move(runtime_root))) {
    static_cast<void>(policy);
}

RuntimeRecoveryStore::~RuntimeRecoveryStore() = default;

RuntimeRecoveryResult RuntimeRecoveryStore::Load() {
    RuntimeRecoveryResult result;
    result.error = RuntimeRecoveryError::StateUnavailable;
    result.message =
        "This build contains the launcher UI only, so no runtime recovery state is kept.";
    return result;
}

RuntimeRecoveryResult RuntimeRecoveryStore::RecordFailure(const RuntimeFailureObservation&) {
    return Load();
}

RuntimeRecoveryResult RuntimeRecoveryStore::MarkHealthy() {
    return Load();
}

RuntimeRecoveryResult RuntimeRecoveryStore::Restore(RuntimeRecoveryAxis) {
    return Load();
}

const char* RuntimeFailureSourceName(const RuntimeFailureSource source) noexcept {
    switch (source) {
    case RuntimeFailureSource::Unknown: return "unknown";
    case RuntimeFailureSource::RuntimeStartup: return "runtime-startup";
    case RuntimeFailureSource::ProfileOverride: return "profile-override";
    case RuntimeFailureSource::PluginGeneration: return "plugin-generation";
    case RuntimeFailureSource::RenderInitialization: return "render-initialization";
    }
    return "unknown";
}

}  // namespace anomaly
