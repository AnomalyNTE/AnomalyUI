cmake_minimum_required(VERSION 3.22)

# AnomalyUI is the standalone extraction of Anomaly's user interface: the
# launcher window, the platform (overlay) management surface, the plugin UI host
# and the C ABI those three exchange.
#
# It is UI only. Nothing here maps, calls or executes a plugin binary, reads or
# writes another process, hooks the game, installs a proxy layer or contacts a
# network source. See README.md ("What was left behind") for the boundary.
project(AnomalyUI VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "AnomalyUI currently targets Windows x64 only")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(ANOMALYUI_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(ANOMALYUI_IS_MULTI_CONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}/anomaly")
include_directories(BEFORE "${GENERATED_DIR}")

# ---------------------------------------------------------------------------
# Third-party dependencies, pinned to the versions the extracted UI was written
# against. FETCHCONTENT_SOURCE_DIR_<NAME> may point at an offline copy.
# ---------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

set(JSON_BuildTests OFF)
set(JSON_Install OFF)
set(JSON_MultipleHeaders OFF)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)
unset(JSON_BuildTests)
unset(JSON_Install)
unset(JSON_MultipleHeaders)

# Package manifests are validated against the schema before the catalog accepts
# them, exactly as the runtime host does.
set(JSON_VALIDATOR_INSTALL OFF)
set(JSON_VALIDATOR_BUILD_TESTS OFF)
set(JSON_VALIDATOR_BUILD_EXAMPLES OFF)
set(JSON_VALIDATOR_SHARED_LIBS OFF)
FetchContent_Declare(
    json_schema_validator
    URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.3.0.tar.gz
    URL_HASH SHA256=2c00b50023c7d557cdaa71c0777f5bcff996c4efd7a539e58beaa4219fa2a5e1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json_schema_validator)
unset(JSON_VALIDATOR_INSTALL)
unset(JSON_VALIDATOR_BUILD_TESTS)
unset(JSON_VALIDATOR_BUILD_EXAMPLES)
unset(JSON_VALIDATOR_SHARED_LIBS)

# ---------------------------------------------------------------------------
# Generated headers and resources: the embedded en-US catalog, the manifest
# schema, and the two .rc files carrying the header logo and the launcher icon.
# ---------------------------------------------------------------------------
set(ANOMALY_HOST_EN_US_CATALOG_FILE "${CMAKE_SOURCE_DIR}/locales/host/en-US.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ANOMALY_HOST_EN_US_CATALOG_FILE}")
file(READ "${ANOMALY_HOST_EN_US_CATALOG_FILE}" ANOMALY_HOST_EN_US_CATALOG_JSON)
set(ANOMALY_HOST_EN_US_CATALOG_LITERALS "")
string(LENGTH "${ANOMALY_HOST_EN_US_CATALOG_JSON}" ANOMALY_HOST_EN_US_CATALOG_LENGTH)
set(ANOMALY_HOST_EN_US_CATALOG_OFFSET 0)
while(ANOMALY_HOST_EN_US_CATALOG_OFFSET LESS ANOMALY_HOST_EN_US_CATALOG_LENGTH)
    math(EXPR ANOMALY_HOST_EN_US_CATALOG_REMAINING
        "${ANOMALY_HOST_EN_US_CATALOG_LENGTH} - ${ANOMALY_HOST_EN_US_CATALOG_OFFSET}")
    if(ANOMALY_HOST_EN_US_CATALOG_REMAINING GREATER 8000)
        set(ANOMALY_HOST_EN_US_CATALOG_CHUNK_LENGTH 8000)
    else()
        set(ANOMALY_HOST_EN_US_CATALOG_CHUNK_LENGTH ${ANOMALY_HOST_EN_US_CATALOG_REMAINING})
    endif()
    string(SUBSTRING "${ANOMALY_HOST_EN_US_CATALOG_JSON}"
        ${ANOMALY_HOST_EN_US_CATALOG_OFFSET} ${ANOMALY_HOST_EN_US_CATALOG_CHUNK_LENGTH}
        ANOMALY_HOST_EN_US_CATALOG_CHUNK)
    string(APPEND ANOMALY_HOST_EN_US_CATALOG_LITERALS
        "    R\"anomaly_catalog(${ANOMALY_HOST_EN_US_CATALOG_CHUNK})anomaly_catalog\"\n")
    math(EXPR ANOMALY_HOST_EN_US_CATALOG_OFFSET
        "${ANOMALY_HOST_EN_US_CATALOG_OFFSET} + ${ANOMALY_HOST_EN_US_CATALOG_CHUNK_LENGTH}")
endwhile()
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/i18n_embedded_catalog.hpp.in"
    "${GENERATED_DIR}/anomaly/i18n_embedded_catalog.hpp"
    @ONLY)

file(READ "${CMAKE_SOURCE_DIR}/schemas/plugin-manifest.schema.json"
    ANOMALY_PLUGIN_MANIFEST_SCHEMA_JSON)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/plugin_manifest_schema.hpp.in"
    "${GENERATED_DIR}/plugin_manifest_schema.hpp"
    @ONLY)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/logo-header.png" ANOMALY_PLATFORM_LOGO_PATH)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/logo.png" ANOMALY_ESC_MENU_LOGO_PATH)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/apps/launcher/AnomalyLauncher.ico"
    ANOMALY_LAUNCHER_ICON_PATH)
set(PLATFORM_UI_RESOURCES_RC "${GENERATED_DIR}/platform_ui_resources.rc")
set(LAUNCHER_RESOURCES_RC "${GENERATED_DIR}/AnomalyLauncher.rc")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/platform_ui_resources.rc.in"
    "${PLATFORM_UI_RESOURCES_RC}" @ONLY)
configure_file(
    "${CMAKE_SOURCE_DIR}/apps/launcher/AnomalyLauncher.rc.in"
    "${LAUNCHER_RESOURCES_RC}" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/schemas/plugin-manifest.schema.json"
    "${CMAKE_SOURCE_DIR}/logo-header.png"
    "${CMAKE_SOURCE_DIR}/logo.png"
    "${CMAKE_SOURCE_DIR}/apps/launcher/AnomalyLauncher.ico")

# ---------------------------------------------------------------------------
# SDK surface: plugin packages are built against the pure C ABI in
# include/anomaly/sdk, which is what plugin pages are drawn through.
# ---------------------------------------------------------------------------
add_library(anomaly_sdk INTERFACE)
add_library(Anomaly::sdk ALIAS anomaly_sdk)
set_target_properties(anomaly_sdk PROPERTIES EXPORT_NAME sdk)
target_compile_features(anomaly_sdk INTERFACE c_std_11 cxx_std_20)
target_include_directories(anomaly_sdk INTERFACE
    $<BUILD_INTERFACE:${GENERATED_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk/AnomalyPlugin.cmake")

set(ANOMALYUI_DEFINITIONS UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
function(anomalyui_configure_target target)
    target_include_directories(${target} PUBLIC include)
    target_compile_definitions(${target} PUBLIC ${ANOMALYUI_DEFINITIONS})
    target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
endfunction()

# ---------------------------------------------------------------------------
# Platform primitives: configuration files, localization, atomic storage,
# service registry.
# ---------------------------------------------------------------------------
add_library(anomaly_i18n STATIC src/i18n/i18n.cpp)
anomalyui_configure_target(anomaly_i18n)
target_include_directories(anomaly_i18n PRIVATE "${GENERATED_DIR}")
target_link_libraries(anomaly_i18n PRIVATE nlohmann_json::nlohmann_json)

add_library(anomaly_config STATIC src/platform/windows/config.cpp)
anomalyui_configure_target(anomaly_config)
target_link_libraries(anomaly_config PUBLIC anomaly_i18n)

add_library(anomaly_reliable_storage STATIC src/services/reliable_storage.cpp)
anomalyui_configure_target(anomaly_reliable_storage)

# The adapter service registry is the only piece of src/game the UI needs: the
# management surface lists the services a game adapter would publish, and this
# build publishes none.
add_library(anomaly_adapter_services STATIC src/game/ue5/adapter_service_registry.cpp)
anomalyui_configure_target(anomaly_adapter_services)

# ---------------------------------------------------------------------------
# Plugin data model: manifests, semver, compatibility, packages, catalog,
# dependency resolution and enablement. All of it is data; no entry binary is
# resolved, mapped or called.
# ---------------------------------------------------------------------------
add_library(anomaly_semver STATIC src/plugin/semver.cpp)
anomalyui_configure_target(anomaly_semver)

add_library(anomaly_plugin_manifest STATIC
    src/plugin/plugin_manifest.cpp
    src/plugin/plugin_capability_policy.cpp)
anomalyui_configure_target(anomaly_plugin_manifest)
target_include_directories(anomaly_plugin_manifest PRIVATE "${GENERATED_DIR}")
target_link_libraries(anomaly_plugin_manifest
    PUBLIC anomaly_semver
    PRIVATE nlohmann_json::nlohmann_json nlohmann_json_schema_validator::validator)

add_library(anomaly_plugin_compatibility STATIC src/plugin/plugin_compatibility.cpp)
anomalyui_configure_target(anomaly_plugin_compatibility)
target_link_libraries(anomaly_plugin_compatibility PUBLIC anomaly_plugin_manifest)

add_library(anomaly_plugin_package STATIC src/plugin/plugin_package.cpp)
anomalyui_configure_target(anomaly_plugin_package)

add_library(anomaly_plugin_catalog STATIC src/plugin/plugin_catalog.cpp)
anomalyui_configure_target(anomaly_plugin_catalog)
target_link_libraries(anomaly_plugin_catalog
    PUBLIC anomaly_plugin_compatibility anomaly_plugin_package)

add_library(anomaly_plugin_dependency_resolver STATIC src/plugin/plugin_dependency_resolver.cpp)
anomalyui_configure_target(anomaly_plugin_dependency_resolver)
target_link_libraries(anomaly_plugin_dependency_resolver PUBLIC anomaly_plugin_catalog)

add_library(anomaly_plugin_enablement STATIC src/plugin/plugin_enablement.cpp)
anomalyui_configure_target(anomaly_plugin_enablement)
target_link_libraries(anomaly_plugin_enablement
    PUBLIC anomaly_plugin_catalog PRIVATE nlohmann_json::nlohmann_json)

add_library(anomaly_plugin_scope STATIC src/plugin/plugin_scope.cpp)
anomalyui_configure_target(anomaly_plugin_scope)

# ---------------------------------------------------------------------------
# UI resources: the window/font/texture registry plus its decoders.
# ---------------------------------------------------------------------------
add_library(anomaly_ui_resources STATIC
    src/ui/ui_resource_decoder.cpp
    src/ui/ui_resource_registry.cpp)
anomalyui_configure_target(anomaly_ui_resources)
target_link_libraries(anomaly_ui_resources PUBLIC anomaly_plugin_scope ole32 shell32)

# ---------------------------------------------------------------------------
# The UI itself.
# ---------------------------------------------------------------------------
# Liquid glass shaders live in src/ui/shaders and are compiled at build time
# into bytecode headers, so the renderer never compiles HLSL at run time.
file(GLOB ANOMALYUI_FXC_CANDIDATES
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x64/fxc.exe")
list(SORT ANOMALYUI_FXC_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
set(ANOMALYUI_FXC_HINTS "")
foreach(candidate IN LISTS ANOMALYUI_FXC_CANDIDATES)
    get_filename_component(candidate_directory "${candidate}" DIRECTORY)
    list(APPEND ANOMALYUI_FXC_HINTS "${candidate_directory}")
endforeach()
find_program(ANOMALYUI_FXC fxc
    HINTS
        "${CMAKE_WINDOWS_KITS_10_DIR}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        ${ANOMALYUI_FXC_HINTS}
    REQUIRED)
set(ANOMALYUI_SHADER_DIR "${GENERATED_DIR}/anomaly/shaders")
file(MAKE_DIRECTORY "${ANOMALYUI_SHADER_DIR}")
set(ANOMALYUI_SHADER_HEADERS "")
function(anomalyui_compile_shader source entry profile variable)
    set(output "${ANOMALYUI_SHADER_DIR}/${variable}.h")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${ANOMALYUI_FXC}" /nologo /O3 /Ges /T ${profile} /E ${entry}
                /Vn ${variable} /Fh "${output}" "${CMAKE_SOURCE_DIR}/${source}"
        DEPENDS "${CMAKE_SOURCE_DIR}/${source}"
        COMMENT "Compiling ${source} (${entry})"
        VERBATIM)
    set(ANOMALYUI_SHADER_HEADERS ${ANOMALYUI_SHADER_HEADERS} "${output}" PARENT_SCOPE)
endfunction()
anomalyui_compile_shader(src/ui/shaders/glass_pass.hlsl vs_full vs_4_0 kGlassFullScreenVs)
anomalyui_compile_shader(src/ui/shaders/glass_pass.hlsl ps_down ps_4_0 kGlassDownsamplePs)
anomalyui_compile_shader(src/ui/shaders/glass_pass.hlsl ps_blur ps_4_0 kGlassBlurPs)
anomalyui_compile_shader(src/ui/shaders/glass_surface.hlsl vs_quad vs_4_0 kGlassSurfaceVs)
anomalyui_compile_shader(src/ui/shaders/glass_surface.hlsl ps_glass ps_4_0 kGlassSurfacePs)

add_library(anomaly_imgui STATIC
    ${ANOMALYUI_SHADER_HEADERS}
    src/ui/shaders/glass_pass.hlsl
    src/ui/shaders/glass_surface.hlsl
    src/ui/host_ui_service.cpp
    src/ui/liquid_glass.cpp
    src/ui/platform_ui_theme.cpp
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
target_include_directories(anomaly_imgui PUBLIC "${imgui_SOURCE_DIR}" PRIVATE include)
target_compile_definitions(anomaly_imgui PUBLIC ${ANOMALYUI_DEFINITIONS})
target_compile_options(anomaly_imgui PRIVATE /W3 /EHsc /utf-8)
target_link_libraries(anomaly_imgui PUBLIC d3d11 dxgi dxguid)
# The .hlsl files are listed for the IDE only; fxc builds them above.
set_source_files_properties(
    src/ui/shaders/glass_pass.hlsl src/ui/shaders/glass_surface.hlsl
    PROPERTIES HEADER_FILE_ONLY TRUE)

add_library(anomaly_platform_ui_model STATIC src/ui/platform_ui_model.cpp)
anomalyui_configure_target(anomaly_platform_ui_model)
target_link_libraries(anomaly_platform_ui_model
    PUBLIC anomaly_plugin_dependency_resolver anomaly_plugin_enablement)

add_library(anomaly_platform_settings STATIC src/ui/platform_settings.cpp)
anomalyui_configure_target(anomaly_platform_settings)
target_link_libraries(anomaly_platform_settings
    PUBLIC anomaly_reliable_storage PRIVATE nlohmann_json::nlohmann_json)

add_library(anomaly_platform_ui_layout STATIC src/ui/platform_ui_layout.cpp)
anomalyui_configure_target(anomaly_platform_ui_layout)
target_link_libraries(anomaly_platform_ui_layout PRIVATE nlohmann_json::nlohmann_json)

# The management surface, the UI-only plugin host and the demo plugin pages.
add_library(anomalyui_ui STATIC
    src/ui/platform_host.cpp
    src/host/plugin_ui_host.cpp
    src/host/demo_plugin_pages.cpp
    src/host/no_memory_service.cpp
    src/repository/plugin_repository_config.cpp)
anomalyui_configure_target(anomalyui_ui)
target_include_directories(anomalyui_ui PRIVATE include src "${GENERATED_DIR}")
target_link_libraries(anomalyui_ui
    PUBLIC
        anomaly_imgui anomaly_platform_ui_model anomaly_platform_settings
        anomaly_platform_ui_layout anomaly_plugin_catalog anomaly_plugin_enablement
        anomaly_plugin_dependency_resolver anomaly_adapter_services anomaly_ui_resources
        anomaly_config anomaly_i18n anomaly_reliable_storage
    PRIVATE nlohmann_json::nlohmann_json ole32 shell32)

# The launcher window plus the inert backend that stands in for the parts of the
# launcher this repository does not ship.
add_library(anomalyui_launcher_static STATIC src/launcher/preview_backend.cpp)
anomalyui_configure_target(anomalyui_launcher_static)
target_link_libraries(anomalyui_launcher_static PRIVATE nlohmann_json::nlohmann_json Advapi32)

# ---------------------------------------------------------------------------
# Applications.
# ---------------------------------------------------------------------------
# The standalone management-surface preview: a real Win32 window plus a D3D11
# swap chain, with no game process involved.
add_executable(anomalyui_preview WIN32
    apps/ui_preview/main.cpp
    "${PLATFORM_UI_RESOURCES_RC}")
set_target_properties(anomalyui_preview PROPERTIES OUTPUT_NAME "AnomalyUIPreview")
target_include_directories(anomalyui_preview PRIVATE include src "${GENERATED_DIR}")
target_compile_definitions(anomalyui_preview PRIVATE ${ANOMALYUI_DEFINITIONS})
target_compile_options(anomalyui_preview PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(anomalyui_preview PRIVATE anomalyui_ui ole32 shell32)

# The launcher window.
add_executable(anomalyui_launcher WIN32
    apps/launcher/main.cpp
    apps/launcher/AnomalyLauncher.manifest
    "${LAUNCHER_RESOURCES_RC}")
set_target_properties(anomalyui_launcher PROPERTIES OUTPUT_NAME "AnomalyLauncher")
target_include_directories(anomalyui_launcher PRIVATE include)
target_compile_definitions(anomalyui_launcher PRIVATE ${ANOMALYUI_DEFINITIONS})
target_compile_options(anomalyui_launcher PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_options(anomalyui_launcher PRIVATE /MANIFESTUAC:NO /DELAYLOAD:dwmapi.dll)
target_link_libraries(anomalyui_launcher PRIVATE
    anomaly_config anomaly_imgui anomaly_platform_settings anomaly_platform_ui_layout
    anomaly_ui_resources anomalyui_launcher_static
    ole32 shell32 delayimp dcomp)

# ---------------------------------------------------------------------------
# Demo plugin packages. Their manifests drive the management surface's plugin
# list and their ids bind to the preview's demo pages; the entry binaries are
# built so a package looks exactly like the real thing, and nothing in this
# repository ever loads them.
# ---------------------------------------------------------------------------
set(ANOMALYUI_DEMO_PACKAGE_ROOT "${CMAKE_BINARY_DIR}/packages")

# The only demo package: a real plugin binary plus the manifest the management
# surface lists it from. Its page source is the shared header the preview draws.
anomaly_add_plugin(anomaly_example_widget_gallery
    SOURCES examples/widget_gallery/plugin.cpp
    MANIFEST examples/widget_gallery/manifest.json
    PACKAGE_NAME WidgetGallery
    OUTPUT_DIRECTORY "${ANOMALYUI_DEMO_PACKAGE_ROOT}/WidgetGallery")
target_compile_definitions(anomaly_example_widget_gallery PRIVATE ${ANOMALYUI_DEFINITIONS})
target_compile_options(anomaly_example_widget_gallery PRIVATE /utf-8)
target_include_directories(anomaly_example_widget_gallery PRIVATE "${CMAKE_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# Runtime layout. Both shells resolve the shared data root as <exe>/Anomaly.
# ---------------------------------------------------------------------------
function(anomalyui_stage_data_root target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Anomaly"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/config/anomaly.ini"
                "$<TARGET_FILE_DIR:${target}>/Anomaly/anomaly.ini"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_SOURCE_DIR}/CONTACT"
                "$<TARGET_FILE_DIR:${target}>/Anomaly/CONTACT"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/locales/host"
                "$<TARGET_FILE_DIR:${target}>/Anomaly/locales/host"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets/fonts"
                "$<TARGET_FILE_DIR:${target}>/Anomaly/assets/fonts"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets/images"
                "$<TARGET_FILE_DIR:${target}>/Anomaly/assets/images"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${CMAKE_SOURCE_DIR}/config/themes"
                "$<TARGET_FILE_DIR:${target}>/Anomaly/config/themes"
        VERBATIM)
endfunction()

anomalyui_stage_data_root(anomalyui_preview)
anomalyui_stage_data_root(anomalyui_launcher)

function(anomalyui_stage_preview_plugin target package_name)
    if(NOT TARGET ${target})
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:anomalyui_preview>/Anomaly/plugins/${package_name}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "$<TARGET_FILE_DIR:${target}>"
                "$<TARGET_FILE_DIR:anomalyui_preview>/Anomaly/plugins/${package_name}"
        VERBATIM)
endfunction()

anomalyui_stage_preview_plugin(anomaly_example_widget_gallery WidgetGallery)
add_dependencies(anomalyui_preview anomaly_example_widget_gallery)
