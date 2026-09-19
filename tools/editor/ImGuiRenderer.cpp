#include "tools/editor/ImGuiRenderer.h"

#include "core/logging/Log.h"
#include "rhi/api/CommandEncoder.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <cstddef>
#include <span>

namespace engine::editor {
namespace {

// The editor UI pipeline is built straight from the RHI, bypassing the ShaderLab asset
// pipeline, so its two shaders are baked in here as SPIR-V words instead of being compiled
// from a shader folder at build time. uint32 keeps the words four byte aligned for
// vkCreateShaderModule. The GLSL each array was compiled from is preserved as a comment;
// regenerate the words with:
//   glslc --target-env=vulkan1.3 -O -mfmt=num [-DSRGB_TARGET] <source>
//
// Vertex shader. Positions already arrive in clip space: ImGuiRenderer folds ImGui's
// display offset and size into the vertex copy it has to make anyway, which keeps this
// pipeline free of any uniform buffer (the RHI exposes no push constants).
//
//   #version 450
//   layout(location = 0) in vec2 inPosition;
//   layout(location = 1) in vec2 inUv;
//   layout(location = 2) in vec4 inColor;
//   layout(location = 0) out vec2 outUv;
//   layout(location = 1) out vec4 outColor;
//   void main() {
//       outUv = inUv;
//       outColor = inColor;
//       gl_Position = vec4(inPosition, 0.0, 1.0);
//   }
constexpr std::uint32_t kVertexSpirv[] = {
    0x07230203,0x00010600,0x000d000b,0x00000023,
    0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,
    0x00000000,0x0003000e,0x00000000,0x00000001,
    0x000b000f,0x00000000,0x00000004,0x6e69616d,
    0x00000000,0x00000009,0x0000000b,0x0000000f,
    0x00000011,0x00000018,0x0000001b,0x00040047,
    0x00000009,0x0000001e,0x00000000,0x00040047,
    0x0000000b,0x0000001e,0x00000001,0x00040047,
    0x0000000f,0x0000001e,0x00000001,0x00040047,
    0x00000011,0x0000001e,0x00000002,0x00030047,
    0x00000016,0x00000002,0x00050048,0x00000016,
    0x00000000,0x0000000b,0x00000000,0x00050048,
    0x00000016,0x00000001,0x0000000b,0x00000001,
    0x00050048,0x00000016,0x00000002,0x0000000b,
    0x00000003,0x00050048,0x00000016,0x00000003,
    0x0000000b,0x00000004,0x00040047,0x0000001b,
    0x0000001e,0x00000000,0x00020013,0x00000002,
    0x00030021,0x00000003,0x00000002,0x00030016,
    0x00000006,0x00000020,0x00040017,0x00000007,
    0x00000006,0x00000002,0x00040020,0x00000008,
    0x00000003,0x00000007,0x0004003b,0x00000008,
    0x00000009,0x00000003,0x00040020,0x0000000a,
    0x00000001,0x00000007,0x0004003b,0x0000000a,
    0x0000000b,0x00000001,0x00040017,0x0000000d,
    0x00000006,0x00000004,0x00040020,0x0000000e,
    0x00000003,0x0000000d,0x0004003b,0x0000000e,
    0x0000000f,0x00000003,0x00040020,0x00000010,
    0x00000001,0x0000000d,0x0004003b,0x00000010,
    0x00000011,0x00000001,0x00040015,0x00000013,
    0x00000020,0x00000000,0x0004002b,0x00000013,
    0x00000014,0x00000001,0x0004001c,0x00000015,
    0x00000006,0x00000014,0x0006001e,0x00000016,
    0x0000000d,0x00000006,0x00000015,0x00000015,
    0x00040020,0x00000017,0x00000003,0x00000016,
    0x0004003b,0x00000017,0x00000018,0x00000003,
    0x00040015,0x00000019,0x00000020,0x00000001,
    0x0004002b,0x00000019,0x0000001a,0x00000000,
    0x0004003b,0x0000000a,0x0000001b,0x00000001,
    0x0004002b,0x00000006,0x0000001d,0x00000000,
    0x0004002b,0x00000006,0x0000001e,0x3f800000,
    0x00050036,0x00000002,0x00000004,0x00000000,
    0x00000003,0x000200f8,0x00000005,0x0004003d,
    0x00000007,0x0000000c,0x0000000b,0x0003003e,
    0x00000009,0x0000000c,0x0004003d,0x0000000d,
    0x00000012,0x00000011,0x0003003e,0x0000000f,
    0x00000012,0x0004003d,0x00000007,0x0000001c,
    0x0000001b,0x00050051,0x00000006,0x0000001f,
    0x0000001c,0x00000000,0x00050051,0x00000006,
    0x00000020,0x0000001c,0x00000001,0x00070050,
    0x0000000d,0x00000021,0x0000001f,0x00000020,
    0x0000001d,0x0000001e,0x00050041,0x0000000e,
    0x00000022,0x00000018,0x0000001a,0x0003003e,
    0x00000022,0x00000021,0x000100fd,0x00010038
};
//
// Fragment shader. The RHI maps BindingType::SampledTexture to a combined image sampler,
// so the font atlas (and any future ImGui::Image texture) is a single sampler2D.
//
//   #version 450
//   layout(location = 0) in vec2 inUv;
//   layout(location = 1) in vec4 inColor;
//   layout(location = 0) out vec4 outColor;
//   layout(set = 0, binding = 0) uniform sampler2D uTexture;
//   void main() {
//       outColor = inColor * texture(uTexture, inUv);
//   }
constexpr std::uint32_t kFragmentSpirv[] = {
    0x07230203,0x00010600,0x000d000b,0x0000001b,
    0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,
    0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0009000f,0x00000004,0x00000004,0x6e69616d,
    0x00000000,0x0000000b,0x0000000e,0x00000013,
    0x00000017,0x00030010,0x00000004,0x00000007,
    0x00040047,0x0000000b,0x0000001e,0x00000001,
    0x00040047,0x0000000e,0x0000001e,0x00000000,
    0x00040047,0x00000013,0x00000021,0x00000000,
    0x00040047,0x00000013,0x00000022,0x00000000,
    0x00040047,0x00000017,0x0000001e,0x00000000,
    0x00020013,0x00000002,0x00030021,0x00000003,
    0x00000002,0x00030016,0x00000006,0x00000020,
    0x00040017,0x00000007,0x00000006,0x00000004,
    0x00040020,0x0000000a,0x00000001,0x00000007,
    0x0004003b,0x0000000a,0x0000000b,0x00000001,
    0x00040020,0x0000000d,0x00000003,0x00000007,
    0x0004003b,0x0000000d,0x0000000e,0x00000003,
    0x00090019,0x00000010,0x00000006,0x00000001,
    0x00000000,0x00000000,0x00000000,0x00000001,
    0x00000000,0x0003001b,0x00000011,0x00000010,
    0x00040020,0x00000012,0x00000000,0x00000011,
    0x0004003b,0x00000012,0x00000013,0x00000000,
    0x00040017,0x00000015,0x00000006,0x00000002,
    0x00040020,0x00000016,0x00000001,0x00000015,
    0x0004003b,0x00000016,0x00000017,0x00000001,
    0x00050036,0x00000002,0x00000004,0x00000000,
    0x00000003,0x000200f8,0x00000005,0x0004003d,
    0x00000007,0x0000000c,0x0000000b,0x0004003d,
    0x00000011,0x00000014,0x00000013,0x0004003d,
    0x00000015,0x00000018,0x00000017,0x00050057,
    0x00000007,0x00000019,0x00000014,0x00000018,
    0x00050085,0x00000007,0x0000001a,0x0000000c,
    0x00000019,0x0003003e,0x0000000e,0x0000001a,
    0x000100fd,0x00010038
};
// Variant built with SRGB_TARGET. ImGui authors its style colors directly in sRGB space,
// expecting them to land in the framebuffer as written; an sRGB attachment makes the
// hardware encode on write, which would brighten the whole theme, so the colors are
// decoded here first. Decoding per fragment rather than per vertex also keeps gradients
// ramping in the sRGB space ImGui intends. Alpha carries no encoding and is left alone.
// On top of the shader above it inserts, before the final multiply:
//
//   vec3 SrgbToLinear(vec3 color) {
//       return mix(color / 12.92,
//                  pow((color + 0.055) / 1.055, vec3(2.4)),
//                  greaterThan(color, vec3(0.04045)));
//   }
//   ... color.rgb = SrgbToLinear(color.rgb);
constexpr std::uint32_t kFragmentSrgbSpirv[] = {
    0x07230203,0x00010600,0x000d000b,0x00000062,
    0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,
    0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0009000f,0x00000004,0x00000004,0x6e69616d,
    0x00000000,0x00000028,0x0000003a,0x0000003f,
    0x00000043,0x00030010,0x00000004,0x00000007,
    0x00040047,0x00000028,0x0000001e,0x00000001,
    0x00040047,0x0000003a,0x0000001e,0x00000000,
    0x00040047,0x0000003f,0x00000021,0x00000000,
    0x00040047,0x0000003f,0x00000022,0x00000000,
    0x00040047,0x00000043,0x0000001e,0x00000000,
    0x00020013,0x00000002,0x00030021,0x00000003,
    0x00000002,0x00030016,0x00000006,0x00000020,
    0x00040017,0x00000007,0x00000006,0x00000003,
    0x0004002b,0x00000006,0x00000012,0x3d6147ae,
    0x0004002b,0x00000006,0x00000018,0x4019999a,
    0x0006002c,0x00000007,0x00000019,0x00000018,
    0x00000018,0x00000018,0x0004002b,0x00000006,
    0x0000001c,0x3d25aee6,0x0006002c,0x00000007,
    0x0000001d,0x0000001c,0x0000001c,0x0000001c,
    0x00020014,0x0000001e,0x00040017,0x0000001f,
    0x0000001e,0x00000003,0x00040017,0x00000024,
    0x00000006,0x00000004,0x00040020,0x00000027,
    0x00000001,0x00000024,0x0004003b,0x00000027,
    0x00000028,0x00000001,0x00040020,0x00000039,
    0x00000003,0x00000024,0x0004003b,0x00000039,
    0x0000003a,0x00000003,0x00090019,0x0000003c,
    0x00000006,0x00000001,0x00000000,0x00000000,
    0x00000000,0x00000001,0x00000000,0x0003001b,
    0x0000003d,0x0000003c,0x00040020,0x0000003e,
    0x00000000,0x0000003d,0x0004003b,0x0000003e,
    0x0000003f,0x00000000,0x00040017,0x00000041,
    0x00000006,0x00000002,0x00040020,0x00000042,
    0x00000001,0x00000041,0x0004003b,0x00000042,
    0x00000043,0x00000001,0x0006002c,0x00000007,
    0x0000005c,0x00000012,0x00000012,0x00000012,
    0x0004002b,0x00000006,0x0000005e,0x3d9e8391,
    0x0006002c,0x00000007,0x0000005f,0x0000005e,
    0x0000005e,0x0000005e,0x0004002b,0x00000006,
    0x00000060,0x3f72a76f,0x0006002c,0x00000007,
    0x00000061,0x00000060,0x00000060,0x00000060,
    0x00050036,0x00000002,0x00000004,0x00000000,
    0x00000003,0x000200f8,0x00000005,0x0004003d,
    0x00000024,0x00000029,0x00000028,0x0008004f,
    0x00000007,0x0000002c,0x00000029,0x00000029,
    0x00000000,0x00000001,0x00000002,0x00050085,
    0x00000007,0x0000004b,0x0000002c,0x0000005f,
    0x00050081,0x00000007,0x0000004e,0x0000002c,
    0x0000005c,0x00050085,0x00000007,0x00000050,
    0x0000004e,0x00000061,0x0007000c,0x00000007,
    0x00000051,0x00000001,0x0000001a,0x00000050,
    0x00000019,0x000500ba,0x0000001f,0x00000053,
    0x0000002c,0x0000001d,0x000600a9,0x00000007,
    0x00000054,0x00000053,0x00000051,0x0000004b,
    0x00050051,0x00000006,0x00000032,0x00000054,
    0x00000000,0x00060052,0x00000024,0x00000056,
    0x00000032,0x00000029,0x00000000,0x00050051,
    0x00000006,0x00000035,0x00000054,0x00000001,
    0x00060052,0x00000024,0x00000058,0x00000035,
    0x00000056,0x00000001,0x00050051,0x00000006,
    0x00000038,0x00000054,0x00000002,0x00060052,
    0x00000024,0x0000005a,0x00000038,0x00000058,
    0x00000002,0x0004003d,0x0000003d,0x00000040,
    0x0000003f,0x0004003d,0x00000041,0x00000044,
    0x00000043,0x00050057,0x00000024,0x00000045,
    0x00000040,0x00000044,0x00050085,0x00000024,
    0x00000046,0x0000005a,0x00000045,0x0003003e,
    0x0000003a,0x00000046,0x000100fd,0x00010038
};

static_assert(sizeof(ImDrawVert) == 20, "The UI pipeline hardcodes ImGui's vertex layout");

// Extra headroom so a slowly growing UI does not reallocate its buffers every frame.
constexpr std::uint32_t kGeometrySlack = 4096;

// ImTextureID is a 64 bit integer in this ImGui version, which fits a whole RHI bind
// group handle. Encoding it directly means ImGui::Image works as soon as a caller
// registers a bind group, without a side table. Live handles always carry a non-zero
// generation, so a zeroed ImDrawCmd::TextureId can never collide with a real handle.
[[nodiscard]] ImTextureID toTextureId(rhi::BindGroupHandle group) {
    return (static_cast<ImTextureID>(group.generation) << 32U) |
           static_cast<ImTextureID>(group.index);
}

[[nodiscard]] rhi::BindGroupHandle toBindGroup(ImTextureID id) {
    return {static_cast<std::uint32_t>(id & 0xFFFFFFFFU),
            static_cast<std::uint32_t>(id >> 32U)};
}

template <typename T> [[nodiscard]] std::span<const std::byte> asBytes(const std::vector<T>& data) {
    return std::as_bytes(std::span{data});
}

[[nodiscard]] bool isSrgb(rhi::TextureFormat format) {
    return format == rhi::TextureFormat::Rgba8Srgb || format == rhi::TextureFormat::Bgra8Srgb;
}

} // namespace

ImGuiRenderer::~ImGuiRenderer() {
    shutdown();
}

bool ImGuiRenderer::initialize(rhi::IDevice& device, rhi::TextureFormat colorFormat) {
    if (device_)
        return true;
    device_ = &device;
    colorFormat_ = colorFormat;

    const rhi::BindGroupLayoutEntry textureEntry{
        .binding = 0,
        .type = rhi::BindingType::SampledTexture,
        .visibility = rhi::ShaderVisibility::Fragment,
    };
    textureLayout_ = device_->createBindGroupLayout({
        .entries = std::span{&textureEntry, 1},
        .debugName = "ImGuiTextureLayout",
    });
    if (!textureLayout_) {
        Log::error("ImGuiRenderer", "Cannot create the UI bind group layout");
        shutdown();
        return false;
    }

    if (!createFontTexture() || !createPipeline()) {
        shutdown();
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "MiniEngineRhi";
    // The renderer honors ImDrawCmd::VtxOffset, so ImGui may keep 16 bit indices even
    // for draw lists above 64K vertices instead of splitting them.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    return true;
}

void ImGuiRenderer::shutdown() {
    if (!device_)
        return;
    for (Geometry& geometry : geometry_)
        releaseGeometry(geometry);
    for (SceneTexture& texture : sceneTextures_) {
        if (texture.group)
            device_->destroyBindGroup(texture.group);
        texture = {};
    }
    destroyPipeline();
    if (fontBindGroup_)
        device_->destroyBindGroup(fontBindGroup_);
    if (sampler_)
        device_->destroySampler(sampler_);
    if (fontView_)
        device_->destroyTextureView(fontView_);
    if (fontTexture_)
        device_->destroyTexture(fontTexture_);
    // The layout outlives the pipelines that reference it, so it goes last.
    if (textureLayout_)
        device_->destroyBindGroupLayout(textureLayout_);
    fontBindGroup_ = {};
    sampler_ = {};
    fontView_ = {};
    fontTexture_ = {};
    textureLayout_ = {};
    colorFormat_ = rhi::TextureFormat::Undefined;
    device_ = nullptr;
    if (ImGui::GetCurrentContext())
        ImGui::GetIO().Fonts->SetTexID(0);
}

void ImGuiRenderer::onColorFormatChanged(rhi::TextureFormat colorFormat) {
    if (!device_ || colorFormat == colorFormat_)
        return;
    colorFormat_ = colorFormat;
    destroyPipeline();
    if (!createPipeline()) {
        Log::error("ImGuiRenderer", "Cannot rebuild the UI pipeline for the new color format");
    }
}

bool ImGuiRenderer::createFontTexture() {
    unsigned char* pixels{};
    int width{};
    int height{};
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0) {
        Log::error("ImGuiRenderer", "The ImGui font atlas is empty");
        return false;
    }

    const auto atlasWidth = static_cast<std::uint32_t>(width);
    const auto atlasHeight = static_cast<std::uint32_t>(height);
    fontTexture_ = device_->createTexture({
        .format = rhi::TextureFormat::Rgba8Unorm,
        .width = atlasWidth,
        .height = atlasHeight,
        .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination,
        .debugName = "ImGuiFontAtlas",
    });
    fontView_ = device_->createTextureView({
        .texture = fontTexture_,
        .format = rhi::TextureFormat::Rgba8Unorm,
    });
    // Nearest wrapping would bleed neighbouring glyphs into each other at the atlas
    // edges; ImGui always samples the atlas with clamped coordinates.
    sampler_ = device_->createSampler({
        .addressU = rhi::SamplerAddressMode::ClampToEdge,
        .addressV = rhi::SamplerAddressMode::ClampToEdge,
    });
    if (!fontTexture_ || !fontView_ || !sampler_) {
        Log::error("ImGuiRenderer", "Cannot create the ImGui font atlas resources");
        return false;
    }

    const std::size_t byteCount = static_cast<std::size_t>(atlasWidth) * atlasHeight * 4U;
    const rhi::TextureUploadRegion region{
        .width = atlasWidth,
        .height = atlasHeight,
        .data = std::span{reinterpret_cast<const std::byte*>(pixels), byteCount},
    };
    // uploadTexture leaves the image in the shader read state, which is what the
    // bind group below declares.
    device_->uploadTexture(fontTexture_, std::span{&region, 1});

    const rhi::BindGroupEntry entry{
        .binding = 0,
        .type = rhi::BindingType::SampledTexture,
        .textureView = fontView_,
        .sampler = sampler_,
    };
    fontBindGroup_ = device_->createBindGroup({
        .layout = textureLayout_,
        .entries = std::span{&entry, 1},
        .debugName = "ImGuiFontAtlas",
    });
    if (!fontBindGroup_) {
        Log::error("ImGuiRenderer", "Cannot create the ImGui font bind group");
        return false;
    }
    ImGui::GetIO().Fonts->SetTexID(toTextureId(fontBindGroup_));
    // The pixels now live on the GPU, so release ImGui's CPU side copy of the atlas.
    ImGui::GetIO().Fonts->ClearTexData();
    return true;
}

bool ImGuiRenderer::createPipeline() {
    if (!vertexShader_) {
        vertexShader_ = device_->createShader({
            .stage = rhi::ShaderStage::Vertex,
            .bytecode = std::as_bytes(std::span{kVertexSpirv}),
            .debugName = "ImGuiVertex",
        });
    }
    if (!fragmentShader_) {
        const std::span<const std::uint32_t> fragment =
            isSrgb(colorFormat_) ? std::span<const std::uint32_t>{kFragmentSrgbSpirv}
                                 : std::span<const std::uint32_t>{kFragmentSpirv};
        fragmentShader_ = device_->createShader({
            .stage = rhi::ShaderStage::Fragment,
            .bytecode = std::as_bytes(fragment),
            .debugName = "ImGuiFragment",
        });
    }
    if (!vertexShader_ || !fragmentShader_) {
        Log::error("ImGuiRenderer", "Cannot create the UI shader modules");
        return false;
    }

    rhi::GraphicsPipelineDesc desc;
    desc.vertexShader = vertexShader_;
    desc.fragmentShader = fragmentShader_;
    desc.bindGroupLayouts = {textureLayout_};
    desc.vertexBindings = {{.binding = 0, .stride = static_cast<std::uint32_t>(sizeof(ImDrawVert))}};
    desc.vertexAttributes = {
        {.location = 0,
         .binding = 0,
         .format = rhi::VertexFormat::Vec2Float32,
         .offset = static_cast<std::uint32_t>(offsetof(ImDrawVert, pos))},
        {.location = 1,
         .binding = 0,
         .format = rhi::VertexFormat::Vec2Float32,
         .offset = static_cast<std::uint32_t>(offsetof(ImDrawVert, uv))},
        {.location = 2,
         .binding = 0,
         .format = rhi::VertexFormat::UInt8x4Normalized,
         .offset = static_cast<std::uint32_t>(offsetof(ImDrawVert, col))},
    };
    // ImGui emits both winding orders and expects straight alpha blending; the overlay
    // draws on top of the finished frame without a depth buffer.
    desc.raster.cull = rhi::CullMode::None;
    desc.depthStencil.depthTestEnable = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.blend.mode = rhi::BlendMode::Alpha;
    desc.colorFormats = {colorFormat_};
    pipeline_ = device_->createGraphicsPipeline(desc);
    if (!pipeline_) {
        Log::error("ImGuiRenderer", "Cannot create the UI graphics pipeline");
        return false;
    }
    return true;
}

void ImGuiRenderer::destroyPipeline() {
    if (pipeline_)
        device_->destroyGraphicsPipeline(pipeline_);
    if (fragmentShader_)
        device_->destroyShader(fragmentShader_);
    if (vertexShader_)
        device_->destroyShader(vertexShader_);
    pipeline_ = {};
    fragmentShader_ = {};
    vertexShader_ = {};
}

bool ImGuiRenderer::reserveGeometry(Geometry& geometry,
                                    std::uint32_t vertexCount,
                                    std::uint32_t indexCount) {
    // Recreating a buffer here is safe: the swapchain waited on this frame's fence, so
    // no in-flight submission still reads the geometry of the frame being recorded.
    if (vertexCount > geometry.vertexCapacity) {
        if (geometry.vertexBuffer)
            device_->destroyBuffer(geometry.vertexBuffer);
        geometry.vertexCapacity = vertexCount + kGeometrySlack;
        geometry.vertexBuffer = device_->createBuffer({
            .size = std::uint64_t{geometry.vertexCapacity} * sizeof(ImDrawVert),
            .usage = rhi::BufferUsage::Vertex,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "ImGuiVertexBuffer",
        });
        if (!geometry.vertexBuffer) {
            geometry.vertexCapacity = 0;
            Log::error("ImGuiRenderer", "Cannot grow the UI vertex buffer");
            return false;
        }
    }
    if (indexCount > geometry.indexCapacity) {
        if (geometry.indexBuffer)
            device_->destroyBuffer(geometry.indexBuffer);
        geometry.indexCapacity = indexCount + kGeometrySlack;
        geometry.indexBuffer = device_->createBuffer({
            .size = std::uint64_t{geometry.indexCapacity} * sizeof(std::uint32_t),
            .usage = rhi::BufferUsage::Index,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "ImGuiIndexBuffer",
        });
        if (!geometry.indexBuffer) {
            geometry.indexCapacity = 0;
            Log::error("ImGuiRenderer", "Cannot grow the UI index buffer");
            return false;
        }
    }
    return true;
}

void ImGuiRenderer::releaseGeometry(Geometry& geometry) {
    if (geometry.vertexBuffer)
        device_->destroyBuffer(geometry.vertexBuffer);
    if (geometry.indexBuffer)
        device_->destroyBuffer(geometry.indexBuffer);
    geometry = {};
}

bool ImGuiRenderer::setSceneTexture(std::uint32_t frameIndex, rhi::TextureViewHandle view) {
    if (!device_ || frameIndex >= sceneTextures_.size())
        return false;
    SceneTexture& texture = sceneTextures_[frameIndex];
    if (texture.view == view && texture.group)
        return true;
    if (texture.group)
        device_->destroyBindGroup(texture.group);
    texture = {};
    if (!view)
        return false;
    const rhi::BindGroupEntry entry{
        .binding = 0,
        .type = rhi::BindingType::SampledTexture,
        .textureView = view,
        .sampler = sampler_,
    };
    texture.group = device_->createBindGroup({
        .layout = textureLayout_,
        .entries = std::span{&entry, 1},
        .debugName = "ImGuiSceneTexture",
    });
    if (!texture.group)
        return false;
    texture.view = view;
    return true;
}

void ImGuiRenderer::render(rhi::IGraphicsCommandEncoder& encoder,
                           const ImDrawData& drawData,
                           std::uint32_t frameIndex) {
    if (!device_ || !pipeline_ || drawData.TotalIdxCount <= 0)
        return;
    const float framebufferWidth = drawData.DisplaySize.x * drawData.FramebufferScale.x;
    const float framebufferHeight = drawData.DisplaySize.y * drawData.FramebufferScale.y;
    if (framebufferWidth <= 0.0F || framebufferHeight <= 0.0F)
        return;
    if (frameIndex >= geometry_.size()) {
        Log::error("ImGuiRenderer", "Frame index %u exceeds the UI geometry buffers", frameIndex);
        return;
    }

    Geometry& geometry = geometry_[frameIndex];
    const auto vertexCount = static_cast<std::uint32_t>(drawData.TotalVtxCount);
    const auto indexCount = static_cast<std::uint32_t>(drawData.TotalIdxCount);
    if (!reserveGeometry(geometry, vertexCount, indexCount))
        return;

    // Fold ImGui's screen space transform into the copy the upload needs anyway, so the
    // vertex shader receives clip space positions and needs no uniform buffer. Vulkan's
    // clip space is y-down like ImGui's, hence no flip.
    const float scaleX = 2.0F / drawData.DisplaySize.x;
    const float scaleY = 2.0F / drawData.DisplaySize.y;
    vertexStaging_.clear();
    vertexStaging_.reserve(vertexCount);
    indexStaging_.clear();
    indexStaging_.reserve(indexCount);
    for (const ImDrawList* list : drawData.CmdLists) {
        for (const ImDrawVert& source : list->VtxBuffer) {
            ImDrawVert vertex = source;
            vertex.pos.x = (source.pos.x - drawData.DisplayPos.x) * scaleX - 1.0F;
            vertex.pos.y = (source.pos.y - drawData.DisplayPos.y) * scaleY - 1.0F;
            vertexStaging_.push_back(vertex);
        }
        for (ImDrawIdx index : list->IdxBuffer)
            indexStaging_.push_back(static_cast<std::uint32_t>(index));
    }
    device_->uploadBuffer(geometry.vertexBuffer, asBytes(vertexStaging_));
    device_->uploadBuffer(geometry.indexBuffer, asBytes(indexStaging_));

    encoder.beginDebugLabel("ImGui", {0.4F, 0.7F, 1.0F, 1.0F});
    rhi::DrawStateDesc uiDrawState;
    uiDrawState.raster.cull = rhi::CullMode::None;
    uiDrawState.depthStencil.depthTestEnable = false;
    uiDrawState.depthStencil.depthWriteEnable = false;
    uiDrawState.blend.mode = rhi::BlendMode::Alpha;

    encoder.bindPipeline(pipeline_);
    encoder.setDrawState(uiDrawState);
    encoder.bindVertexBuffer(0, geometry.vertexBuffer);
    encoder.bindIndexBuffer(geometry.indexBuffer, 0, rhi::IndexFormat::UInt32);
    encoder.setViewport({.width = framebufferWidth, .height = framebufferHeight});

    std::uint32_t vertexBase{};
    std::uint32_t indexBase{};
    for (const ImDrawList* list : drawData.CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                // Draw callbacks may only reset the state this backend owns; the editor
                // panels use none, so anything else is reported instead of guessed at.
                if (command.UserCallback == ImDrawCallback_ResetRenderState) {
                    encoder.bindPipeline(pipeline_);
                    encoder.setDrawState(uiDrawState);
                    encoder.bindVertexBuffer(0, geometry.vertexBuffer);
                    encoder.bindIndexBuffer(geometry.indexBuffer, 0, rhi::IndexFormat::UInt32);
                    encoder.setViewport({.width = framebufferWidth, .height = framebufferHeight});
                } else {
                    Log::warn("ImGuiRenderer", "Ignoring an unsupported ImGui draw callback");
                }
                continue;
            }
            if (command.ElemCount == 0 || command.GetTexID() == 0)
                continue;

            // Clip rects are in ImGui's logical units while the scissor is in
            // framebuffer pixels, so the framebuffer scale applies here only.
            const float minX = std::max(
                (command.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x, 0.0F);
            const float minY = std::max(
                (command.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y, 0.0F);
            const float maxX =
                std::min((command.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x,
                         framebufferWidth);
            const float maxY =
                std::min((command.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y,
                         framebufferHeight);
            if (maxX <= minX || maxY <= minY)
                continue;

            encoder.setScissor({
                .x = static_cast<std::int32_t>(minX),
                .y = static_cast<std::int32_t>(minY),
                .width = static_cast<std::uint32_t>(maxX - minX),
                .height = static_cast<std::uint32_t>(maxY - minY),
            });
            const rhi::BindGroupHandle group = command.GetTexID() == kSceneTextureId
                ? sceneTextures_[frameIndex].group : toBindGroup(command.GetTexID());
            if (!group)
                continue;
            encoder.bindGroup(0, group);
            encoder.drawIndexed({
                .indexCount = command.ElemCount,
                .firstIndex = command.IdxOffset + indexBase,
                .vertexOffset = static_cast<std::int32_t>(command.VtxOffset + vertexBase),
            });
        }
        vertexBase += static_cast<std::uint32_t>(list->VtxBuffer.Size);
        indexBase += static_cast<std::uint32_t>(list->IdxBuffer.Size);
    }
    encoder.endDebugLabel();
}

} // namespace engine::editor
