#include "anomaly/liquid_glass.hpp"

#include "anomaly/platform_ui_theme.hpp"

#include <imgui_internal.h>

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

// Bytecode compiled from src/ui/shaders by fxc at build time.
#include "anomaly/shaders/kGlassBlurPs.h"
#include "anomaly/shaders/kGlassDownsamplePs.h"
#include "anomaly/shaders/kGlassFullScreenVs.h"
#include "anomaly/shaders/kGlassSurfacePs.h"
#include "anomaly/shaders/kGlassSurfaceVs.h"

namespace anomaly::glass {
namespace {

using Microsoft::WRL::ComPtr;

struct alignas(16) GlassConstants final {
    float screen[4]{};
    float rect[4]{};
    float shape[4]{};
    float tint[4]{};
    float optics[4]{};
    float light[4]{};
    float shadow[4]{};
    float extra[4]{};
    float edge[4]{};
};

struct alignas(16) PassConstants final {
    float texel[4]{};
    float pass[4]{};
};

struct RenderTarget final {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> view;
    ComPtr<ID3D11ShaderResourceView> resource;
    UINT width{};
    UINT height{};
};

struct Renderer final {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> full_screen;
    ComPtr<ID3D11VertexShader> quad;
    ComPtr<ID3D11PixelShader> downsample;
    ComPtr<ID3D11PixelShader> blur;
    ComPtr<ID3D11PixelShader> glass;
    ComPtr<ID3D11Buffer> glass_constants;
    ComPtr<ID3D11Buffer> pass_constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> opaque;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11RasterizerState> unclipped;
    ComPtr<ID3D11DepthStencilState> depth_off;
    ComPtr<ID3D11Texture2D> backdrop;
    ComPtr<ID3D11ShaderResourceView> backdrop_view;
    UINT backdrop_width{};
    UINT backdrop_height{};
    DXGI_FORMAT backdrop_format{DXGI_FORMAT_UNKNOWN};
    RenderTarget half;
    RenderTarget half_scratch;
    RenderTarget quarter;
    RenderTarget scratch;
    int captured_frame{-1};
};

std::unique_ptr<Renderer> g_renderer;
bool g_reduce_transparency{};
bool g_reduce_motion{};

enum class CommandKind : std::uint8_t { Capture, Surface };

struct Command final {
    CommandKind kind{CommandKind::Surface};
    int frame{};
    bool clip{true};
    bool blur{true};
    GlassConstants constants;
};

// Callback payloads live here until the frame that recorded them has been
// rendered; the next frame's first command recycles the storage. A deque keeps
// the addresses stable while it grows.
std::deque<Command> g_commands;
int g_commands_frame{-1};

struct WindowSurface final {
    int frame{};
    SurfaceOptions options;
};

std::unordered_map<ImGuiID, WindowSurface> g_window_surfaces;
std::vector<std::unique_ptr<ImDrawList>> g_underlays;

Command& NewCommand() {
    const int frame = ImGui::GetFrameCount();
    if (frame != g_commands_frame) {
        g_commands.clear();
        g_commands_frame = frame;
    }
    Command& command = g_commands.emplace_back();
    command.frame = frame;
    return command;
}

bool CreateTarget(ID3D11Device* device, RenderTarget& target,
    const UINT width, const UINT height) noexcept {
    if (target.texture != nullptr && target.width == width && target.height == height) {
        return true;
    }
    target = {};
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&description, nullptr, &target.texture)) ||
        FAILED(device->CreateRenderTargetView(target.texture.Get(), nullptr, &target.view)) ||
        FAILED(device->CreateShaderResourceView(
            target.texture.Get(), nullptr, &target.resource))) {
        target = {};
        return false;
    }
    target.width = width;
    target.height = height;
    return true;
}

bool EnsureTargets(Renderer& renderer, const D3D11_TEXTURE2D_DESC& source) noexcept {
    if (renderer.backdrop == nullptr || renderer.backdrop_width != source.Width ||
        renderer.backdrop_height != source.Height ||
        renderer.backdrop_format != source.Format) {
        renderer.backdrop.Reset();
        renderer.backdrop_view.Reset();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = source.Width;
        description.Height = source.Height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = source.Format;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(renderer.device->CreateTexture2D(
                &description, nullptr, &renderer.backdrop)) ||
            FAILED(renderer.device->CreateShaderResourceView(
                renderer.backdrop.Get(), nullptr, &renderer.backdrop_view))) {
            renderer.backdrop.Reset();
            renderer.backdrop_view.Reset();
            return false;
        }
        renderer.backdrop_width = source.Width;
        renderer.backdrop_height = source.Height;
        renderer.backdrop_format = source.Format;
    }
    const UINT half_width = (std::max)(1U, (source.Width + 1U) / 2U);
    const UINT half_height = (std::max)(1U, (source.Height + 1U) / 2U);
    const UINT quarter_width = (std::max)(1U, (source.Width + 3U) / 4U);
    const UINT quarter_height = (std::max)(1U, (source.Height + 3U) / 4U);
    return CreateTarget(renderer.device.Get(), renderer.half, half_width, half_height) &&
        CreateTarget(renderer.device.Get(), renderer.half_scratch, half_width, half_height) &&
        CreateTarget(renderer.device.Get(), renderer.quarter, quarter_width, quarter_height) &&
        CreateTarget(renderer.device.Get(), renderer.scratch, quarter_width, quarter_height);
}

template <typename T>
void Upload(ID3D11DeviceContext* context, ID3D11Buffer* buffer, const T& value) noexcept {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &value, sizeof(T));
        context->Unmap(buffer, 0);
    }
}

void CaptureCallback(const ImDrawList*, const ImDrawCmd* draw_command) {
    Renderer* const renderer = g_renderer.get();
    const auto* const command = static_cast<const Command*>(draw_command->UserCallbackData);
    if (renderer == nullptr || command == nullptr) return;
    ID3D11DeviceContext* const context = renderer->context.Get();
    ComPtr<ID3D11RenderTargetView> output;
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(1, &output, &depth);
    if (output == nullptr) return;
    ComPtr<ID3D11Resource> resource;
    output->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;
    if (resource == nullptr || FAILED(resource.As(&texture))) return;
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    if (description.SampleDesc.Count != 1 || !EnsureTargets(*renderer, description)) return;

    ID3D11ShaderResourceView* const unbound[3]{};
    context->PSSetShaderResources(0, 3, unbound);
    context->CopyResource(renderer->backdrop.Get(), texture.Get());

    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(renderer->full_screen.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());
    const float blend_factor[4]{};
    context->OMSetBlendState(renderer->opaque.Get(), blend_factor, 0xffffffffU);
    context->OMSetDepthStencilState(renderer->depth_off.Get(), 0);
    context->RSSetState(renderer->unclipped.Get());
    context->PSSetConstantBuffers(0, 1, renderer->pass_constants.GetAddressOf());
    const auto pass = [&](RenderTarget& target, ID3D11ShaderResourceView* source,
                          const UINT source_width, const UINT source_height,
                          ID3D11PixelShader* shader, const float direction_x,
                          const float direction_y, const float spread) {
        context->PSSetShaderResources(0, 3, unbound);
        context->OMSetRenderTargets(1, target.view.GetAddressOf(), nullptr);
        const D3D11_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(target.width),
            static_cast<float>(target.height), 0.0f, 1.0f};
        context->RSSetViewports(1, &viewport);
        PassConstants constants;
        constants.texel[0] = 1.0f / static_cast<float>(source_width);
        constants.texel[1] = 1.0f / static_cast<float>(source_height);
        constants.texel[2] = direction_x;
        constants.texel[3] = direction_y;
        constants.pass[0] = spread;
        Upload(context, renderer->pass_constants.Get(), constants);
        context->PSSetShader(shader, nullptr, 0);
        context->PSSetShaderResources(0, 1, &source);
        context->Draw(3, 0);
    };
    if (!command->blur) {
        context->OMSetRenderTargets(1, output.GetAddressOf(), depth.Get());
        renderer->captured_frame = command->frame;
        return;
    }
    // Light blur: half resolution, one small Gaussian. Heavy blur: quarter
    // resolution, two wider ones. The face of a surface mixes between them and
    // its rim refracts the sharp copy.
    pass(renderer->half, renderer->backdrop_view.Get(), renderer->backdrop_width,
        renderer->backdrop_height, renderer->downsample.Get(), 0.0f, 0.0f, 0.0f);
    const UINT half_width = renderer->half.width;
    const UINT half_height = renderer->half.height;
    pass(renderer->half_scratch, renderer->half.resource.Get(), half_width, half_height,
        renderer->blur.Get(), 1.0f, 0.0f, 1.0f);
    pass(renderer->half, renderer->half_scratch.resource.Get(), half_width, half_height,
        renderer->blur.Get(), 0.0f, 1.0f, 1.0f);
    pass(renderer->quarter, renderer->half.resource.Get(), half_width, half_height,
        renderer->downsample.Get(), 0.0f, 0.0f, 0.0f);
    const UINT width = renderer->quarter.width;
    const UINT height = renderer->quarter.height;
    pass(renderer->scratch, renderer->quarter.resource.Get(), width, height,
        renderer->blur.Get(), 1.0f, 0.0f, 1.2f);
    pass(renderer->quarter, renderer->scratch.resource.Get(), width, height,
        renderer->blur.Get(), 0.0f, 1.0f, 1.2f);
    pass(renderer->scratch, renderer->quarter.resource.Get(), width, height,
        renderer->blur.Get(), 1.0f, 0.0f, 1.8f);
    pass(renderer->quarter, renderer->scratch.resource.Get(), width, height,
        renderer->blur.Get(), 0.0f, 1.0f, 1.8f);
    context->PSSetShaderResources(0, 3, unbound);
    context->OMSetRenderTargets(1, output.GetAddressOf(), depth.Get());
    renderer->captured_frame = command->frame;
}

void SurfaceCallback(const ImDrawList*, const ImDrawCmd* draw_command) {
    Renderer* const renderer = g_renderer.get();
    const auto* const command = static_cast<const Command*>(draw_command->UserCallbackData);
    if (renderer == nullptr || command == nullptr) return;
    ID3D11DeviceContext* const context = renderer->context.Get();
    UINT viewport_count = 1;
    D3D11_VIEWPORT viewport{};
    context->RSGetViewports(&viewport_count, &viewport);
    if (viewport_count == 0 || viewport.Width <= 0.0f || viewport.Height <= 0.0f) return;
    GlassConstants constants = command->constants;
    constants.screen[0] = viewport.Width;
    constants.screen[1] = viewport.Height;
    constants.screen[2] = 1.0f / viewport.Width;
    constants.screen[3] = 1.0f / viewport.Height;
    const bool backdrop_ready = renderer->captured_frame == command->frame &&
        renderer->backdrop_view != nullptr &&
        static_cast<float>(renderer->backdrop_width) == viewport.Width &&
        static_cast<float>(renderer->backdrop_height) == viewport.Height;
    if (!backdrop_ready) {
        // Nothing was captured for this frame: draw the tint alone rather than
        // refracting a stale picture.
        constants.tint[3] = 1.0f;
        constants.light[3] = 1.0f;
    }
    Upload(context, renderer->glass_constants.Get(), constants);
    context->IASetInputLayout(nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(renderer->quad.Get(), nullptr, 0);
    context->PSSetShader(renderer->glass.Get(), nullptr, 0);
    context->VSSetConstantBuffers(0, 1, renderer->glass_constants.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, renderer->glass_constants.GetAddressOf());
    ID3D11ShaderResourceView* const resources[3]{
        backdrop_ready ? renderer->backdrop_view.Get() : nullptr,
        backdrop_ready ? renderer->quarter.resource.Get() : nullptr,
        backdrop_ready ? renderer->half.resource.Get() : nullptr};
    context->PSSetShaderResources(0, 3, resources);
    context->PSSetSamplers(0, 1, renderer->sampler.GetAddressOf());
    const float blend_factor[4]{};
    context->OMSetBlendState(renderer->blend.Get(), blend_factor, 0xffffffffU);
    D3D11_RECT scissor{0, 0, static_cast<LONG>(viewport.Width),
        static_cast<LONG>(viewport.Height)};
    if (command->clip) {
        scissor.left = (std::max)(0L, static_cast<LONG>(draw_command->ClipRect.x));
        scissor.top = (std::max)(0L, static_cast<LONG>(draw_command->ClipRect.y));
        scissor.right = (std::min)(scissor.right, static_cast<LONG>(draw_command->ClipRect.z));
        scissor.bottom = (std::min)(scissor.bottom, static_cast<LONG>(draw_command->ClipRect.w));
        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) return;
    }
    context->RSSetScissorRects(1, &scissor);
    context->Draw(4, 0);
    ID3D11ShaderResourceView* const unbound[3]{};
    context->PSSetShaderResources(0, 3, unbound);
}

ImVec4 Mix(const ImVec4& from, const ImVec4& to, const float amount) noexcept {
    return {from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
        from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount};
}

ImVec4 ToVec4(const ue5mem::PlatformUiColor& color) noexcept {
    return {color.red, color.green, color.blue, color.alpha};
}

struct Look final {
    ImVec4 tint{};
    float tint_strength{};
    float refraction{};
    float chroma{};
    float blur_mix{1.0f};
    float saturation{1.0f};
    float rim{1.0f};
    float lift{};
    float minimum_alpha{};
    float shadow_alpha{};
    float shadow_offset{};
    float shadow_blur{};
    float gloss{};
    float bevel{12.0f};
    float glow{};
    float magnify{};
    float contact{};
    // How much the inner shoulder darkens, mostly on the side facing away
    // from the light. This is what gives a pane its thickness.
    float shade{};
    float fallback_alpha{0.9f};
};

// Over a flat wallpaper there is little for refraction to bend, so the glass
// has to be carried by its edge: the rim line, the glow inside the bevel and
// the gloss on the upper half. Shadows stay faint and only floating panes get
// one at all.
Look ResolveLook(const SurfaceOptions& options) noexcept {
    const bool dark = DarkAppearance();
    const ImVec4 surface = ToVec4(ue5mem::PlatformUiSurfaceColor());
    Look look;
    look.tint = dark ? Mix(surface, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.15f)
                     : Mix(surface, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.70f);
    look.tint.w = 1.0f;
    // blur_mix is the face blur: 0 sharp, 1 light, 2 heavy.
    switch (options.variant) {
    case Variant::Regular:
        look.tint_strength = dark ? 0.26f : 0.12f;
        look.refraction = 40.0f;
        look.chroma = 0.14f;
        look.blur_mix = 1.45f;
        look.saturation = 1.80f;
        look.rim = dark ? 0.75f : 1.0f;
        look.lift = dark ? 0.01f : 0.03f;
        look.minimum_alpha = 0.90f;
        look.shadow_alpha = dark ? 0.20f : 0.05f;
        look.shadow_offset = 3.0f;
        look.shadow_blur = 14.0f;
        look.gloss = dark ? 0.03f : 0.07f;
        look.bevel = 26.0f;
        look.glow = dark ? 0.12f : 0.22f;
        look.magnify = 0.0f;
        look.contact = dark ? 0.20f : 0.07f;
        look.shade = dark ? 0.16f : 0.10f;
        look.fallback_alpha = 0.84f;
        break;
    case Variant::Clear:
        look.tint_strength = 0.02f;
        look.refraction = 22.0f;
        look.chroma = 0.20f;
        look.blur_mix = 0.15f;
        look.saturation = 1.25f;
        look.rim = 1.0f;
        look.lift = 0.02f;
        look.minimum_alpha = 0.0f;
        look.shadow_alpha = dark ? 0.18f : 0.06f;
        look.shadow_offset = 2.0f;
        look.shadow_blur = 6.0f;
        look.gloss = 0.10f;
        look.bevel = 12.0f;
        look.glow = 0.24f;
        look.magnify = 0.08f;
        look.contact = 0.10f;
        look.shade = 0.10f;
        look.fallback_alpha = 0.35f;
        break;
    case Variant::Material:
        look.tint_strength = dark ? 0.38f : 0.24f;
        look.refraction = 26.0f;
        look.chroma = 0.08f;
        look.blur_mix = 1.85f;
        look.saturation = 1.50f;
        look.rim = dark ? 0.55f : 0.75f;
        look.lift = dark ? 0.01f : 0.05f;
        look.minimum_alpha = 0.94f;
        look.shadow_alpha = 0.0f;
        look.gloss = dark ? 0.02f : 0.04f;
        look.bevel = 20.0f;
        look.glow = dark ? 0.08f : 0.14f;
        look.magnify = 0.0f;
        look.contact = dark ? 0.16f : 0.06f;
        look.shade = dark ? 0.12f : 0.07f;
        look.fallback_alpha = 0.90f;
        break;
    case Variant::Thumb:
        // The selection lens. It is drawn under its label, so it can bend and
        // magnify the glass it sits on freely: a bright two-sided rim, a glow
        // in the lit corner, a shaded far shoulder and a gloss on the face.
        look.tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        look.tint_strength = dark ? 0.10f : 0.30f;
        look.refraction = 16.0f;
        look.chroma = 0.30f;
        look.blur_mix = 0.0f;
        look.saturation = 1.30f;
        look.rim = dark ? 0.90f : 1.20f;
        look.lift = dark ? 0.03f : 0.10f;
        look.minimum_alpha = 0.0f;
        look.shadow_alpha = 0.0f;
        look.gloss = dark ? 0.06f : 0.18f;
        look.bevel = 12.0f;
        look.glow = dark ? 0.25f : 0.45f;
        look.magnify = 0.05f;
        look.contact = 0.0f;
        look.shade = dark ? 0.18f : 0.12f;
        look.fallback_alpha = 0.55f;
        break;
    }
    if (options.tint.w > 0.0f) {
        look.tint = Mix(look.tint, ImVec4(options.tint.x, options.tint.y, options.tint.z, 1.0f),
            options.tint.w);
        look.tint.w = 1.0f;
    }
    if (options.tint_strength >= 0.0f) look.tint_strength = options.tint_strength;
    if (!options.shadow) {
        look.shadow_alpha = 0.0f;
        look.contact = 0.0f;
    }
    return look;
}

ImU32 Color(const ImVec4& color, const float alpha) noexcept {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

void DrawFallback(ImDrawList* list, const ImVec2 min, const ImVec2 max,
    const SurfaceOptions& options, const Look& look) noexcept {
    const float alpha = ImGui::GetStyle().Alpha;
    const float scale = options.scale;
    const float rounding = (std::min)(options.rounding * 1.0f,
        (std::min)(max.x - min.x, max.y - min.y) * 0.5f);
    if (!options.clip) list->PushClipRect(ImVec2(-16384.0f, -16384.0f),
        ImVec2(16384.0f, 16384.0f), false);
    if (look.shadow_alpha > 0.0f) {
        constexpr int kLayers = 5;
        for (int layer = 0; layer < kLayers; ++layer) {
            const float grow = look.shadow_blur * scale *
                static_cast<float>(layer + 1) / static_cast<float>(kLayers) * 0.6f;
            const float layer_alpha = look.shadow_alpha * 0.30f *
                (1.0f - static_cast<float>(layer) / static_cast<float>(kLayers));
            list->AddRectFilled(
                ImVec2(min.x - grow, min.y - grow + look.shadow_offset * scale),
                ImVec2(max.x + grow, max.y + grow + look.shadow_offset * scale),
                Color(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), layer_alpha * alpha), rounding + grow);
        }
    }
    const float fill = g_reduce_transparency ? 0.98f : look.fallback_alpha;
    list->AddRectFilled(min, max, Color(look.tint, fill * alpha), rounding);
    const bool dark = DarkAppearance();
    list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
        Color(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), (dark ? 0.12f : 0.70f) * look.rim * alpha),
        rounding, 0, (std::max)(1.0f, scale));
    if (!options.clip) list->PopClipRect();
}

}  // namespace

bool InitializeRenderer(ID3D11Device* device, ID3D11DeviceContext* context) noexcept {
    ShutdownRenderer();
    if (device == nullptr || context == nullptr) return false;
    try {
        auto renderer = std::make_unique<Renderer>();
        renderer->device = device;
        renderer->context = context;
        if (FAILED(device->CreateVertexShader(kGlassFullScreenVs, sizeof(kGlassFullScreenVs),
                nullptr, &renderer->full_screen)) ||
            FAILED(device->CreateVertexShader(kGlassSurfaceVs, sizeof(kGlassSurfaceVs),
                nullptr, &renderer->quad)) ||
            FAILED(device->CreatePixelShader(kGlassDownsamplePs, sizeof(kGlassDownsamplePs),
                nullptr, &renderer->downsample)) ||
            FAILED(device->CreatePixelShader(kGlassBlurPs, sizeof(kGlassBlurPs),
                nullptr, &renderer->blur)) ||
            FAILED(device->CreatePixelShader(kGlassSurfacePs, sizeof(kGlassSurfacePs),
                nullptr, &renderer->glass))) {
            return false;
        }
        D3D11_BUFFER_DESC buffer{};
        buffer.Usage = D3D11_USAGE_DYNAMIC;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        buffer.ByteWidth = sizeof(GlassConstants);
        if (FAILED(device->CreateBuffer(&buffer, nullptr, &renderer->glass_constants))) return false;
        buffer.ByteWidth = sizeof(PassConstants);
        if (FAILED(device->CreateBuffer(&buffer, nullptr, &renderer->pass_constants))) return false;

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(device->CreateSamplerState(&sampler, &renderer->sampler))) return false;

        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(device->CreateBlendState(&blend, &renderer->opaque))) return false;
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (FAILED(device->CreateBlendState(&blend, &renderer->blend))) return false;

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        if (FAILED(device->CreateRasterizerState(&raster, &renderer->unclipped))) return false;

        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(device->CreateDepthStencilState(&depth, &renderer->depth_off))) return false;
        g_renderer = std::move(renderer);
        return true;
    } catch (...) {
        return false;
    }
}

void ShutdownRenderer() noexcept {
    g_renderer.reset();
    g_commands.clear();
    g_commands_frame = -1;
    g_window_surfaces.clear();
    g_underlays.clear();
}

bool RendererAvailable() noexcept { return g_renderer != nullptr; }

bool Active() noexcept { return g_renderer != nullptr && !g_reduce_transparency; }

void SetReduceTransparency(const bool enabled) noexcept { g_reduce_transparency = enabled; }

bool ReduceTransparency() noexcept { return g_reduce_transparency; }

void SetReduceMotion(const bool enabled) noexcept { g_reduce_motion = enabled; }

bool ReduceMotion() noexcept { return g_reduce_motion; }

bool DarkAppearance() noexcept {
    const auto surface = ue5mem::PlatformUiSurfaceColor();
    return 0.2126f * surface.red + 0.7152f * surface.green + 0.0722f * surface.blue < 0.45f;
}

void CaptureBackdrop(ImDrawList* list, const bool blur) noexcept {
    if (list == nullptr || !Active()) return;
    try {
        Command& command = NewCommand();
        command.kind = CommandKind::Capture;
        command.blur = blur;
        list->AddCallback(CaptureCallback, &command);
        list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    } catch (...) {
    }
}

void DrawSurface(ImDrawList* list, const ImVec2 min, const ImVec2 max,
    const SurfaceOptions& options) noexcept {
    if (list == nullptr || max.x - min.x < 1.0f || max.y - min.y < 1.0f) return;
    const Look look = ResolveLook(options);
    if (!Active()) {
        DrawFallback(list, min, max, options, look);
        return;
    }
    try {
        Command& command = NewCommand();
        command.kind = CommandKind::Surface;
        command.clip = options.clip;
        GlassConstants& constants = command.constants;
        const float scale = options.scale > 0.0f ? options.scale : 1.0f;
        const float margin = look.shadow_alpha > 0.0f
            ? (look.shadow_blur + look.shadow_offset) * scale + 2.0f : 2.0f * scale;
        constants.rect[0] = min.x;
        constants.rect[1] = min.y;
        constants.rect[2] = max.x;
        constants.rect[3] = max.y;
        constants.shape[0] = options.rounding;
        constants.shape[1] = scale;
        constants.shape[2] = margin;
        constants.shape[3] = ImGui::GetStyle().Alpha;
        constants.tint[0] = look.tint.x;
        constants.tint[1] = look.tint.y;
        constants.tint[2] = look.tint.z;
        constants.tint[3] = look.tint_strength;
        constants.optics[0] = look.refraction;
        constants.optics[1] = look.chroma;
        constants.optics[2] = look.blur_mix;
        constants.optics[3] = look.saturation;
        constants.light[0] = look.rim;
        constants.light[1] = look.lift;
        constants.light[2] = DarkAppearance() ? 1.0f : 0.0f;
        constants.light[3] = look.minimum_alpha;
        constants.shadow[0] = look.shadow_alpha;
        constants.shadow[1] = look.shadow_offset * scale;
        constants.shadow[2] = look.shadow_blur * scale;
        constants.shadow[3] = look.gloss;
        constants.extra[0] = look.bevel;
        constants.extra[1] = look.glow;
        constants.extra[2] = look.magnify;
        constants.extra[3] = look.contact;
        constants.edge[0] = look.shade;
        list->AddCallback(SurfaceCallback, &command);
        list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    } catch (...) {
    }
}

void RegisterWindowSurface(const SurfaceOptions& options) noexcept {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr) return;
    ImGuiWindow* const root = window->RootWindow != nullptr ? window->RootWindow : window;
    try {
        g_window_surfaces[root->ID] = {ImGui::GetFrameCount(), options};
    } catch (...) {
    }
}

void PrepareDrawData(ImDrawData* data) noexcept {
    if (data == nullptr || ImGui::GetCurrentContext() == nullptr || !Active()) return;
    try {
        ImGuiContext& context = *GImGui;
        const int frame = ImGui::GetFrameCount();
        struct Pending final {
            ImDrawList* target{};
            ImVec2 min{};
            ImVec2 max{};
            SurfaceOptions options;
        };
        std::vector<Pending> pending;
        for (ImGuiWindow* const window : context.Windows) {
            if (window == nullptr || !window->Active || window->Hidden ||
                window->DrawList == nullptr) {
                continue;
            }
            if ((window->Flags & ImGuiWindowFlags_ChildWindow) != 0 &&
                (window->Flags & ImGuiWindowFlags_Popup) == 0) {
                continue;
            }
            Pending entry;
            entry.target = window->DrawList;
            entry.min = window->Pos;
            entry.max = ImVec2(window->Pos.x + window->Size.x, window->Pos.y + window->Size.y);
            const auto registered = g_window_surfaces.find(window->ID);
            if (registered != g_window_surfaces.end() && registered->second.frame == frame) {
                entry.options = registered->second.options;
            } else if ((window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) != 0) {
                entry.options.variant = Variant::Regular;
                entry.options.rounding = (window->Flags & ImGuiWindowFlags_Tooltip) != 0
                    ? context.Style.PopupRounding * 0.75f : context.Style.PopupRounding;
                entry.options.scale = ImGui::GetIO().FontGlobalScale > 0.0f
                    ? ImGui::GetIO().FontGlobalScale : 1.0f;
                entry.options.clip = false;
                entry.options.shadow = true;
            } else {
                continue;
            }
            pending.push_back(entry);
        }
        std::erase_if(g_window_surfaces,
            [frame](const auto& entry) { return entry.second.frame < frame - 1; });
        std::size_t used = 0;
        for (const Pending& entry : pending) {
            int index = -1;
            for (int list = 0; list < data->CmdLists.Size; ++list) {
                if (data->CmdLists[list] == entry.target) {
                    index = list;
                    break;
                }
            }
            if (index < 0) continue;
            if (used == g_underlays.size()) {
                g_underlays.push_back(
                    std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData()));
            }
            ImDrawList* const underlay = g_underlays[used++].get();
            underlay->_ResetForNewFrame();
            underlay->PushClipRectFullScreen();
            underlay->PushTextureID(ImGui::GetIO().Fonts->TexID);
            CaptureBackdrop(underlay);
            DrawSurface(underlay, entry.min, entry.max, entry.options);
            underlay->_PopUnusedDrawCmd();
            data->CmdLists.insert(data->CmdLists.Data + index, underlay);
            data->CmdListsCount = data->CmdLists.Size;
            data->TotalVtxCount += underlay->VtxBuffer.Size;
            data->TotalIdxCount += underlay->IdxBuffer.Size;
        }
    } catch (...) {
    }
}

void DrawThumb(ImDrawList* list, const ImVec2 min, const ImVec2 max, const float rounding,
    const float scale, const bool accent) noexcept {
    if (list == nullptr || max.x - min.x < 1.0f || max.y - min.y < 1.0f) return;
    const auto& theme = ue5mem::PlatformUiTheme();
    SurfaceOptions options;
    options.variant = Variant::Thumb;
    options.rounding = rounding;
    options.scale = scale;
    options.clip = true;
    if (accent) {
        options.tint = ImVec4(theme.accent.red, theme.accent.green, theme.accent.blue, 0.75f);
        options.tint_strength = DarkAppearance() ? 0.18f : 0.22f;
    }
    if (Active()) {
        // The lens refracts what is already drawn, its own label included.
        CaptureBackdrop(list, false);
        DrawSurface(list, min, max, options);
        return;
    }
    const ImVec4 fill = accent
        ? ImVec4(theme.accent.red, theme.accent.green, theme.accent.blue,
              g_reduce_transparency ? 0.34f : 0.26f)
        : ImVec4(1.0f, 1.0f, 1.0f, DarkAppearance() ? 0.16f : 0.80f);
    list->AddRectFilled(min, max, ImGui::GetColorU32(fill), rounding);
    list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, DarkAppearance() ? 0.14f : 0.65f)),
        rounding, 0, (std::max)(1.0f, scale));
}

ImRect AnimateThumb(const ImGuiID id, const ImRect& target, const ImVec2 origin) noexcept {
    const ImVec2 target_center = target.GetCenter();
    const float x = Animate(id, target_center.x - origin.x, 18.0f);
    const float y = Animate(id ^ 0x632be5abU, target_center.y - origin.y, 18.0f);
    const float width = Animate(id ^ 0x85ebca6bU, target.GetWidth(), 18.0f);
    const float height = Animate(id ^ 0xc2b2ae35U, target.GetHeight(), 18.0f);
    const ImVec2 center(origin.x + x, origin.y + y);
    return ImRect(ImVec2(center.x - width * 0.5f, center.y - height * 0.5f),
        ImVec2(center.x + width * 0.5f, center.y + height * 0.5f));
}

float Animate(const ImGuiID id, const float target, const float speed) noexcept {
    ImGuiStorage* const storage = ImGui::GetStateStorage();
    if (storage == nullptr) return target;
    const ImGuiID key = ImHashStr("##glass-animation", 0, id);
    float* const value = storage->GetFloatRef(key, target);
    if (value == nullptr) return target;
    if (g_reduce_motion) {
        *value = target;
        return target;
    }
    const float delta = (std::max)(0.0f, ImGui::GetIO().DeltaTime);
    *value += (target - *value) * (1.0f - std::exp(-speed * delta));
    if (std::fabs(target - *value) < 0.002f) *value = target;
    return *value;
}

ImVec2 SwitchSize() noexcept {
    const float height = std::round(ImGui::GetFrameHeight() * 0.96f);
    return {std::round(height * 1.66f), height};
}

void DrawSwitch(ImDrawList* list, const ImVec2 min, const ImVec2 max, const float on,
    const float press, const bool hovered, const float scale) noexcept {
    if (list == nullptr) return;
    const auto& theme = ue5mem::PlatformUiTheme();
    const bool dark = DarkAppearance();
    const float height = max.y - min.y;
    const float width = max.x - min.x;
    const float radius = height * 0.5f;
    const ImVec4 accent = ControlAccent();
    const ImVec4 off = dark ? ImVec4(1.0f, 1.0f, 1.0f, hovered ? 0.22f : 0.17f)
                            : ImVec4(0.47f, 0.47f, 0.50f, hovered ? 0.30f : 0.24f);
    const ImVec4 track = Mix(off, ImVec4(accent.x, accent.y, accent.z, 1.0f), on);
    list->AddRectFilled(min, max, ImGui::GetColorU32(track), radius);
    // A light accent needs an edge of its own to read against a light card.
    const float accent_luminance = 0.2126f * accent.x + 0.7152f * accent.y + 0.0722f * accent.z;
    if (!dark && accent_luminance > 0.55f && on > 0.0f) {
        const ImVec4 edge = Mix(ImVec4(accent.x, accent.y, accent.z, 1.0f),
            ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.30f);
        list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f),
            ImGui::GetColorU32(ImVec4(edge.x, edge.y, edge.z, 0.55f * on)), radius, 0,
            (std::max)(1.0f, scale));
    }

    const float inset = (std::max)(2.0f, std::round(height * 0.09f));
    const float knob_radius = radius - inset;
    const float stretch = press * height * 0.30f;
    const float travel = width - inset * 2.0f - knob_radius * 2.0f - stretch;
    const float knob_left = min.x + inset + travel * on;
    const ImVec2 knob_min(knob_left, min.y + inset);
    const ImVec2 knob_max(knob_left + knob_radius * 2.0f + stretch, max.y - inset);
    if (press > 0.02f && Active()) {
        // While the switch is held its knob becomes a clear lens over the track,
        // the one place Apple puts glass into the content layer.
        CaptureBackdrop(list);
        const float grow = press * height * 0.16f;
        SurfaceOptions lens;
        lens.variant = Variant::Clear;
        lens.rounding = knob_radius + grow;
        lens.scale = scale;
        lens.clip = false;
        DrawSurface(list, ImVec2(knob_min.x - grow, knob_min.y - grow),
            ImVec2(knob_max.x + grow, knob_max.y + grow), lens);
    }
    const float solid = Active() ? 1.0f - press : 1.0f;
    if (solid > 0.0f) {
        for (int layer = 0; layer < 3; ++layer) {
            const float grow = static_cast<float>(layer) * 0.8f * scale;
            list->AddRectFilled(ImVec2(knob_min.x - grow, knob_min.y - grow + scale),
                ImVec2(knob_max.x + grow, knob_max.y + grow + 1.5f * scale),
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.07f * solid)),
                knob_radius + grow);
        }
        list->AddRectFilled(knob_min, knob_max,
            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, solid)), knob_radius);
    }
}

bool Switch(const char* label, bool* value) noexcept {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr || window->SkipItems || value == nullptr) return false;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 size = SwitchSize();
    const float height = (std::max)(ImGui::GetFrameHeight(), size.y);
    const ImVec2 position = window->DC.CursorPos;
    const ImRect bounds(position, ImVec2(position.x + size.x +
        (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f),
        position.y + height));
    ImGui::ItemSize(bounds, style.FramePadding.y);
    if (!ImGui::ItemAdd(bounds, id)) return false;
    bool hovered{};
    bool held{};
    const bool pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
    if (pressed) {
        *value = !*value;
        ImGui::MarkItemEdited(id);
    }
    const float top = position.y + (height - size.y) * 0.5f;
    const ImRect track(ImVec2(position.x, top), ImVec2(position.x + size.x, top + size.y));
    const float on = Animate(id, *value ? 1.0f : 0.0f, 15.0f);
    const float press = Animate(id ^ 0x5bd1e995U, held && hovered ? 1.0f : 0.0f, 22.0f);
    ImGui::RenderNavCursor(track, id);
    const float scale = ImGui::GetIO().FontGlobalScale > 0.0f ? ImGui::GetIO().FontGlobalScale : 1.0f;
    DrawSwitch(window->DrawList, track.Min, track.Max, on, press, hovered, scale);
    if (label_size.x > 0.0f) {
        ImGui::RenderText(ImVec2(track.Max.x + style.ItemInnerSpacing.x,
            position.y + (height - label_size.y) * 0.5f), label);
    }
    return pressed;
}

float SegmentedControlWidth(
    const std::span<const char* const> labels, const SegmentedOptions& options) noexcept {
    const float scale = options.scale;
    float width = 3.0f * scale * 2.0f;
    for (const char* const label : labels) {
        width += ImGui::CalcTextSize(label, nullptr, true).x + options.padding_x * scale * 2.0f;
    }
    return width;
}

// Corner radius as a fraction of height. Apple's controls use continuous
// rounded rectangles; a full capsule reads as exaggerated at these sizes.
constexpr float kSegmentRounding = 0.34f;

bool SegmentedControl(const char* id, const std::span<const char* const> labels, int& selected,
    const SegmentedOptions& options) noexcept {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr || window->SkipItems || labels.empty()) return false;
    const float scale = options.scale;
    const float height = options.height * scale;
    const float inset = 3.0f * scale;
    const float padding = options.padding_x * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = SegmentedControlWidth(labels, options);
    ImDrawList* const list = window->DrawList;
    const bool dark = DarkAppearance();
    if (options.glass_track) {
        SurfaceOptions track;
        track.variant = Variant::Regular;
        track.rounding = height * kSegmentRounding;
        track.scale = scale;
        track.shadow = false;
        DrawSurface(list, origin, ImVec2(origin.x + width, origin.y + height), track);
    } else {
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
            ImGui::GetColorU32(dark ? ImVec4(1.0f, 1.0f, 1.0f, 0.07f)
                                    : ImVec4(0.47f, 0.47f, 0.50f, 0.13f)),
            height * kSegmentRounding);
    }

    ImGui::PushID(id);
    const ImGuiID base = window->GetID("##segmented");
    std::vector<float> starts(labels.size());
    std::vector<float> widths(labels.size());
    float x = origin.x + inset;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        starts[index] = x;
        widths[index] = ImGui::CalcTextSize(labels[index], nullptr, true).x + padding * 2.0f;
        x += widths[index];
    }
    const int clamped = std::clamp(selected, 0, static_cast<int>(labels.size()) - 1);
    const std::size_t current_index = static_cast<std::size_t>(clamped);
    const float indicator_rounding = (height - inset * 2.0f) * kSegmentRounding;
    {
        // The lens goes down first so the labels stay crisp on top of it. It
        // swells a little while the control is held, like a pressed droplet.
        const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + width, origin.y + height));
        const float press = Animate(base ^ 0x1b873593U, held ? 1.0f : 0.0f, 20.0f);
        const ImRect target(ImVec2(starts[current_index], origin.y + inset),
            ImVec2(starts[current_index] + widths[current_index], origin.y + height - inset));
        ImRect thumb = AnimateThumb(base, target, origin);
        const float grow = press * 2.0f * scale;
        thumb.Expand(ImVec2(grow * 1.5f, grow));
        DrawThumb(list, thumb.Min, thumb.Max, indicator_rounding + grow, scale, true);
    }
    bool changed = false;
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const bool disabled = index < 32 && (options.disabled_mask & (1U << index)) != 0;
        ImGui::SetCursorScreenPos(ImVec2(starts[index], origin.y + inset));
        ImGui::PushID(static_cast<int>(index));
        ImGui::BeginDisabled(disabled);
        const bool clicked = ImGui::InvisibleButton(
            "##segment", ImVec2(widths[index], height - inset * 2.0f));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        ImGui::PopID();
        if (clicked && !disabled && selected != static_cast<int>(index)) {
            selected = static_cast<int>(index);
            changed = true;
        }
        const char* const label = labels[index];
        const char* const label_end = ImGui::FindRenderedTextEnd(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, label_end);
        const bool current = static_cast<int>(index) == clamped;
        ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ink.w *= disabled ? 0.35f : current ? 1.0f : hovered ? 0.92f : 0.72f;
        list->AddText(ImVec2(starts[index] + (widths[index] - label_size.x) * 0.5f,
                origin.y + (height - label_size.y) * 0.5f),
            ImGui::GetColorU32(ink), label, label_end);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::SetCursorPos(cursor);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width, height));
    return changed;
}

namespace {

constexpr int kTabBarStyleColors = 6;
constexpr int kTabBarStyleVars = 4;

void PushTabBarStyle() noexcept {
    const ImVec4 clear(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Tab, clear);
    ImGui::PushStyleColor(ImGuiCol_TabHovered, clear);
    ImGui::PushStyleColor(ImGuiCol_TabSelected, clear);
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, clear);
    ImGui::PushStyleColor(ImGuiCol_TabDimmed, clear);
    ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected, clear);
    const float height = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, height * kSegmentRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, 0.0f);
}

void PopTabBarStyle() noexcept {
    ImGui::PopStyleVar(kTabBarStyleVars);
    ImGui::PopStyleColor(kTabBarStyleColors);
}

float CurrentScale() noexcept {
    const float scale = ImGui::GetIO().FontGlobalScale;
    return scale > 0.0f ? scale : 1.0f;
}

}  // namespace

bool BeginTabBar(const char* id, const ImGuiTabBarFlags flags) noexcept {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr || window->SkipItems) return false;
    const float scale = CurrentScale();
    const float inset = 3.0f * scale;
    // The bar sits inside a glass capsule as wide as its tabs were last frame;
    // the first frame of a new bar has no width yet and simply skips the track.
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + inset, ImGui::GetCursorPosY() + inset));
    const ImGuiID bar_id = window->GetID(id);
    const ImGuiTabBar* const previous = GImGui->TabBars.GetByKey(bar_id);
    const ImVec2 origin = window->DC.CursorPos;
    const float height = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    if (previous != nullptr && previous->WidthAllTabs > 0.0f) {
        const float available = window->WorkRect.Max.x - origin.x;
        const float width = (std::min)(previous->WidthAllTabs, available);
        // Plugin windows have their own glass body, so the track refracts that.
        CaptureBackdrop(window->DrawList);
        SurfaceOptions track;
        track.variant = Variant::Regular;
        track.rounding = (height + inset * 2.0f) * kSegmentRounding;
        track.scale = scale;
        track.shadow = false;
        track.tint_strength = DarkAppearance() ? 0.30f : 0.20f;
        DrawSurface(window->DrawList, ImVec2(origin.x - inset, origin.y - inset),
            ImVec2(origin.x + width + inset, origin.y + height + inset), track);
        for (const ImGuiTabItem& tab : previous->Tabs) {
            if (tab.ID != previous->SelectedTabId || tab.Width <= 0.0f) continue;
            const float left = origin.x + tab.Offset - previous->ScrollingAnim;
            const ImRect target(ImVec2(left, origin.y), ImVec2(left + tab.Width, origin.y + height));
            const ImRect thumb = AnimateThumb(bar_id ^ 0x7f4a7c15U, target, origin);
            window->DrawList->PushClipRect(ImVec2(origin.x - inset, origin.y - inset),
                ImVec2(origin.x + width + inset, origin.y + height + inset), true);
            DrawThumb(window->DrawList, thumb.Min, thumb.Max, height * kSegmentRounding,
                scale, true);
            window->DrawList->PopClipRect();
            break;
        }
    }
    PushTabBarStyle();
    if (!ImGui::BeginTabBar(id, flags)) {
        PopTabBarStyle();
        return false;
    }
    return true;
}

void EndTabBar() noexcept {
    ImGui::EndTabBar();
    PopTabBarStyle();
    ImGui::Dummy(ImVec2(0.0f, 3.0f * CurrentScale()));
}

ImVec2 GlyphInkCenter(ImFont* font, const float size, const char* glyph) noexcept {
    if (font == nullptr || glyph == nullptr || *glyph == '\0') return ImVec2(0.0f, 0.0f);
    unsigned int codepoint = 0;
    ImTextCharFromUtf8(&codepoint, glyph, nullptr);
    const ImFontGlyph* const found = font->FindGlyphNoFallback(static_cast<ImWchar>(codepoint));
    if (found == nullptr || font->FontSize <= 0.0f) {
        const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, glyph);
        return ImVec2(extent.x * 0.5f, extent.y * 0.5f);
    }
    const float k = size / font->FontSize;
    return ImVec2((found->X0 + found->X1) * 0.5f * k, (found->Y0 + found->Y1) * 0.5f * k);
}

bool SliderScalar(const char* label, const ImGuiDataType data_type, void* value,
    const void* minimum, const void* maximum, const char* format,
    const ImGuiSliderFlags flags) noexcept {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr || window->SkipItems || value == nullptr) return false;
    ImGuiContext& context = *GImGui;
    const ImGuiStyle& style = context.Style;
    const ImGuiID id = window->GetID(label);
    const float width = ImGui::CalcItemWidth();
    const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);
    const float height = ImGui::GetFrameHeight();
    const ImVec2 position = window->DC.CursorPos;
    const ImRect frame(position, ImVec2(position.x + width, position.y + height));
    const ImRect total(frame.Min, ImVec2(frame.Max.x +
        (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), frame.Max.y));
    const bool input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;
    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id, &frame, input_allowed ? ImGuiItemFlags_Inputable : 0)) {
        return false;
    }
    if (format == nullptr) format = ImGui::DataTypeGetInfo(data_type)->PrintFmt;
    const bool hovered = ImGui::ItemHoverable(frame, id, context.LastItemData.ItemFlags);
    bool typing = input_allowed && ImGui::TempInputIsActive(id);
    if (!typing) {
        const bool clicked = hovered && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, id);
        const bool activate = clicked || context.NavActivateId == id;
        if (activate && clicked) ImGui::SetKeyOwner(ImGuiKey_MouseLeft, id);
        if (activate && input_allowed &&
            ((clicked && context.IO.KeyCtrl) ||
                (context.NavActivateId == id &&
                    (context.NavActivateFlags & ImGuiActivateFlags_PreferInput) != 0))) {
            typing = true;
        }
        if (activate && !typing) {
            ImGui::SetActiveID(id, window);
            ImGui::SetFocusID(id, window);
            ImGui::FocusWindow(window);
            context.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        }
    }
    if (typing) {
        const bool clamp = (flags & ImGuiSliderFlags_ClampOnInput) != 0;
        return ImGui::TempInputScalar(frame, id, label, data_type, value, format,
            clamp ? minimum : nullptr, clamp ? maximum : nullptr);
    }

    // The value column is as wide as the widest value the slider can show.
    char text[64];
    const char* const text_end =
        text + ImGui::DataTypeFormatString(text, IM_ARRAYSIZE(text), data_type, value, format);
    float value_width = ImGui::CalcTextSize(text, text_end).x;
    for (const void* bound : {minimum, maximum}) {
        char bound_text[64];
        const int length = ImGui::DataTypeFormatString(
            bound_text, IM_ARRAYSIZE(bound_text), data_type, bound, format);
        value_width = (std::max)(value_width, ImGui::CalcTextSize(bound_text, bound_text + length).x);
    }
    const float scale = CurrentScale();
    const float gap = 10.0f * scale;
    const float knob = std::round(height * 0.66f);
    const ImRect track(frame.Min, ImVec2((std::max)(frame.Min.x + knob * 2.0f,
        frame.Max.x - value_width - gap), frame.Max.y));

    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, knob);
    ImRect grab;
    const bool changed = ImGui::SliderBehavior(
        track, id, data_type, value, minimum, maximum, format, flags, &grab);
    ImGui::PopStyleVar();
    if (changed) ImGui::MarkItemEdited(id);

    ImDrawList* const list = window->DrawList;
    const auto& theme = ue5mem::PlatformUiTheme();
    const bool dark = DarkAppearance();
    const bool active = context.ActiveId == id;
    ImGui::RenderNavCursor(frame, id);
    const float center_y = frame.GetCenter().y;
    const float rail = (std::max)(3.0f, std::round(4.0f * scale));
    const float knob_x = grab.Max.x > grab.Min.x ? grab.GetCenter().x : track.Min.x + knob * 0.5f;
    const ImVec2 rail_min(track.Min.x + knob * 0.5f - rail * 0.5f, center_y - rail * 0.5f);
    const ImVec2 rail_max(track.Max.x - knob * 0.5f + rail * 0.5f, center_y + rail * 0.5f);
    list->AddRectFilled(rail_min, rail_max, ImGui::GetColorU32(dark
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.18f) : ImVec4(0.47f, 0.47f, 0.50f, 0.26f)), rail * 0.5f);
    static_cast<void>(theme);
    list->AddRectFilled(rail_min, ImVec2(knob_x, rail_max.y),
        ImGui::GetColorU32(ControlAccent()), rail * 0.5f);
    const float press = Animate(id ^ 0x2545f491U, active ? 1.0f : 0.0f, 22.0f);
    const ImVec2 knob_min(knob_x - knob * 0.5f, center_y - knob * 0.5f);
    const ImVec2 knob_max(knob_x + knob * 0.5f, center_y + knob * 0.5f);
    if (press > 0.02f && Active()) {
        CaptureBackdrop(list, false);
        const float grow = press * knob * 0.22f;
        SurfaceOptions lens;
        lens.variant = Variant::Clear;
        lens.rounding = knob * 0.5f + grow;
        lens.scale = scale;
        lens.clip = false;
        DrawSurface(list, ImVec2(knob_min.x - grow * 1.3f, knob_min.y - grow),
            ImVec2(knob_max.x + grow * 1.3f, knob_max.y + grow), lens);
    }
    const float solid = Active() ? 1.0f - press : 1.0f;
    if (solid > 0.0f) {
        for (int layer = 0; layer < 3; ++layer) {
            const float spread = static_cast<float>(layer) * 0.8f * scale;
            list->AddCircleFilled(ImVec2(knob_x, center_y + scale), knob * 0.5f + spread,
                ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.07f * solid)));
        }
        list->AddCircleFilled(ImVec2(knob_x, center_y), knob * 0.5f,
            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, solid)));
    }
    const ImVec2 text_size = ImGui::CalcTextSize(text, text_end);
    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (!hovered && !active) ink.w *= 0.72f;
    list->AddText(ImVec2(frame.Max.x - text_size.x, center_y - text_size.y * 0.5f),
        ImGui::GetColorU32(ink), text, text_end);
    if (label_size.x > 0.0f) {
        ImGui::RenderText(ImVec2(frame.Max.x + style.ItemInnerSpacing.x,
            frame.Min.y + style.FramePadding.y), label);
    }
    return changed;
}

bool SliderFloat(const char* label, float* value, const float minimum, const float maximum,
    const char* format, const ImGuiSliderFlags flags) noexcept {
    return SliderScalar(label, ImGuiDataType_Float, value, &minimum, &maximum, format, flags);
}

bool SliderInt(const char* label, int* value, const int minimum, const int maximum,
    const char* format, const ImGuiSliderFlags flags) noexcept {
    return SliderScalar(label, ImGuiDataType_S32, value, &minimum, &maximum, format, flags);
}

ImVec4 ControlAccent() noexcept {
    const auto& accent = ue5mem::PlatformUiTheme().accent;
    return ImVec4(accent.red, accent.green, accent.blue, 1.0f);
}

bool BeginCombo(const char* label, const char* preview, const ImGuiComboFlags flags) noexcept {
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (window == nullptr || window->SkipItems) return false;
    // The frame is measured before the call: once open, the combo makes its
    // popup the current window.
    const ImVec2 position = window->DC.CursorPos;
    const float width = (flags & ImGuiComboFlags_NoPreview) != 0
        ? ImGui::GetFrameHeight() : ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    ImDrawList* const list = window->DrawList;
    const bool open = ImGui::BeginCombo(label, preview, flags | ImGuiComboFlags_NoArrowButton);
    const float scale = CurrentScale();
    const float half = (std::max)(3.0f, height * 0.13f);
    const ImVec2 center(position.x + width - height * 0.5f, position.y + height * 0.5f);
    ImVec4 ink = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ink.w *= 0.70f;
    const float lift = open ? -half * 0.25f : half * 0.25f;
    const float tip = open ? -half * 0.55f : half * 0.55f;
    list->PathLineTo(ImVec2(center.x - half, center.y - tip + lift));
    list->PathLineTo(ImVec2(center.x, center.y + tip + lift));
    list->PathLineTo(ImVec2(center.x + half, center.y - tip + lift));
    list->PathStroke(ImGui::GetColorU32(ink), 0, (std::max)(1.4f, 1.5f * scale));
    return open;
}

bool Combo(const char* label, int* current, const char* const items[], const int count) noexcept {
    if (current == nullptr || items == nullptr || count <= 0) return false;
    const char* const preview = *current >= 0 && *current < count ? items[*current] : "";
    bool changed = false;
    if (BeginCombo(label, preview)) {
        for (int index = 0; index < count; ++index) {
            ImGui::PushID(index);
            const bool selected = index == *current;
            if (ImGui::Selectable(items[index], selected)) {
                changed = changed || !selected;
                *current = index;
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

}  // namespace anomaly::glass
