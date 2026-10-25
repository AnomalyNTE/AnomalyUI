// The memory provider this build does not have.
//
// The management surface's developer page is written against
// `anomaly::ModuleMemoryService`, the process-memory service a plugin host
// publishes. AnomalyUI publishes none: `PluginManager::MemoryServices()` returns
// an empty `CoreMemoryServices`, so every caller checks `memory == nullptr` and
// disables the action. These definitions exist so the UI still links, and each
// one answers "unavailable" rather than performing the operation.
//
// There is deliberately no implementation of ReadMemory/WriteMemory/PatchMemory
// that could reach another process: doing so would be the runtime's job, and the
// runtime is not part of this repository.

#include "anomaly/module_memory_service.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace anomaly {

std::vector<ue5mem::ModuleInfo> ModuleMemoryService::EnumerateModules() const {
    return {};
}

std::optional<ue5mem::ModuleInfo> ModuleMemoryService::FindModule(std::wstring_view) const {
    return std::nullopt;
}

std::vector<ue5mem::SectionInfo> ModuleMemoryService::EnumerateSections(
    const ue5mem::ModuleInfo&) const {
    return {};
}

std::vector<ue5mem::RegionInfo> ModuleMemoryService::EnumerateRegions(
    const ue5mem::ModuleInfo&) const {
    return {};
}

std::vector<std::uintptr_t> ModuleMemoryService::ScanSection(
    const ue5mem::ModuleInfo&, std::string_view, const ue5mem::Pattern&, std::size_t) const {
    return {};
}

std::optional<std::uintptr_t> ModuleMemoryService::ResolveRipRelative(
    std::uintptr_t, std::size_t, std::size_t, std::ptrdiff_t) const {
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> ModuleMemoryService::ReadMemory(
    std::uintptr_t, std::size_t) const {
    return std::nullopt;
}

bool ModuleMemoryService::ReadMemoryInto(std::uintptr_t, void*, std::size_t) const {
    return false;
}

bool ModuleMemoryService::WriteMemory(std::uintptr_t, const void*, std::size_t) const {
    return false;
}

bool ModuleMemoryService::PatchMemory(std::uintptr_t, const void*, std::size_t) const {
    return false;
}

bool ModuleMemoryService::ProtectMemory(
    std::uintptr_t, std::size_t, DWORD, DWORD&) const {
    return false;
}

void* ModuleMemoryService::AllocateMemory(std::size_t, DWORD) const {
    return nullptr;
}

bool ModuleMemoryService::FreeMemory(void*) const {
    return false;
}

std::optional<std::uintptr_t> ModuleMemoryService::ResolvePointerChain(
    std::uintptr_t, const std::ptrdiff_t*, std::size_t) const {
    return std::nullopt;
}

std::string ModuleMemoryService::ProtectionName(DWORD) const {
    return "unavailable";
}

std::string ModuleMemoryService::StateName(DWORD) const {
    return "unavailable";
}

std::string ModuleMemoryService::TypeName(DWORD) const {
    return "unavailable";
}

}  // namespace anomaly
