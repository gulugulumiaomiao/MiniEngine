#include "asset/types/TextureAsset.h"
#include "render/texture/Texture.h"

#include "asset/manager/AssetManager.h"
#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "render/texture/Sampler.h"
#include "rhi/api/Device.h"
#include "rhi/api/ResourceDesc.h"

#include <algorithm>
#include <bit>
#include <string>
#include <utility>
#include <vector>

namespace engine {
namespace {

constexpr std::uint32_t kTextureMagic = 0x52584554U;
constexpr std::uint16_t kTextureVersion = 5;

[[nodiscard]] rhi::PixelFormat tryToRhiTextureFormat(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return rhi::PixelFormat::Rgba8Unorm;
    case TextureFormat::Rgba8Srgb: return rhi::PixelFormat::Rgba8Srgb;
    case TextureFormat::Depth32Float: return rhi::PixelFormat::Depth32Float;
    case TextureFormat::Bgra8Unorm: return rhi::PixelFormat::Bgra8Unorm;
    case TextureFormat::Bgra8Srgb: return rhi::PixelFormat::Bgra8Srgb;
    case TextureFormat::R8Unorm: return rhi::PixelFormat::R8Unorm;
    case TextureFormat::Rg8Unorm: return rhi::PixelFormat::Rg8Unorm;
    case TextureFormat::R16Float: return rhi::PixelFormat::R16Float;
    case TextureFormat::Rg16Float: return rhi::PixelFormat::Rg16Float;
    case TextureFormat::Rgba16Float: return rhi::PixelFormat::Rgba16Float;
    case TextureFormat::R32Float: return rhi::PixelFormat::R32Float;
    case TextureFormat::Rg32Float: return rhi::PixelFormat::Rg32Float;
    case TextureFormat::Rgba32Float: return rhi::PixelFormat::Rgba32Float;
    case TextureFormat::Depth16Unorm: return rhi::PixelFormat::Depth16Unorm;
    }
    return rhi::PixelFormat::Undefined;
}

[[nodiscard]] rhi::TextureType toRhi(TextureType type) {
    switch (type) {
    case TextureType::Texture2D: return rhi::TextureType::Texture2D;
    case TextureType::Texture2DArray: return rhi::TextureType::Texture2DArray;
    case TextureType::Texture3D: return rhi::TextureType::Texture3D;
    case TextureType::TextureCube: return rhi::TextureType::TextureCube;
    case TextureType::TextureCubeArray: return rhi::TextureType::TextureCubeArray;
    }
    Log::fatal("Texture", "Unsupported Texture type");
}


[[nodiscard]] rhi::SwizzleComponent toRhiSwizzle(SwizzleChannel channel) {
    switch (channel) {
    case SwizzleChannel::Zero: return rhi::SwizzleComponent::Zero;
    case SwizzleChannel::One: return rhi::SwizzleComponent::One;
    case SwizzleChannel::R: return rhi::SwizzleComponent::R;
    case SwizzleChannel::G: return rhi::SwizzleComponent::G;
    case SwizzleChannel::B: return rhi::SwizzleComponent::B;
    case SwizzleChannel::A: return rhi::SwizzleComponent::A;
    case SwizzleChannel::Identity: return rhi::SwizzleComponent::Identity;
    }
    return rhi::SwizzleComponent::Identity;
}

[[nodiscard]] rhi::TextureViewDesc toRhiView(const TextureViewDesc& view,
                                             TextureFormat textureFormat) {
    rhi::TextureViewDesc out;
    out.type = toRhi(view.type);
    switch (view.format) {
    case TextureViewFormat::MatchTexture: out.format = toRhi(textureFormat); break;
    case TextureViewFormat::Rgba8Unorm: out.format = rhi::PixelFormat::Rgba8Unorm; break;
    case TextureViewFormat::Rgba8Srgb: out.format = rhi::PixelFormat::Rgba8Srgb; break;
    }
    out.baseMip = view.baseMip;
    out.mipCount = view.mipCount;
    out.baseLayer = view.baseLayer;
    out.layerCount = view.layerCount;
    out.aspect = rhi::isDepthFormat(toRhi(textureFormat))
        ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
    out.swizzle = {toRhiSwizzle(view.swizzle.r),
                   toRhiSwizzle(view.swizzle.g),
                   toRhiSwizzle(view.swizzle.b),
                   toRhiSwizzle(view.swizzle.a)};
    return out;
}

} // namespace

rhi::PixelFormat toRhi(TextureFormat format) {
    const auto result = tryToRhiTextureFormat(format);
    if (result != rhi::PixelFormat::Undefined) return result;
    Log::fatal("Texture", "Unsupported Texture format");
}

bool computeTextureLayout(const TextureDesc& desc, TextureMipLayout& layout) {
    layout = {};
    if (desc.type != TextureType::Texture2D || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.samples != rhi::SampleCount::One) {
        return false;
    }
    return rhi::computeTextureLayout(tryToRhiTextureFormat(desc.format), desc.width, desc.height,
                                     desc.mipCount, layout);
}

bool validateTextureAsset(const TextureDesc& desc, std::span<const std::uint8_t> pixels) {
    // Asset files store tightly packed bytes for any supported non-compressed format.
    if (desc.usage != (rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination) ||
        desc.samples != rhi::SampleCount::One || desc.tiling != rhi::TextureTiling::Optimal ||
        desc.sampler.maxAnisotropy < 1.0F) {
        return false;
    }
    const auto validFilter = [](TextureFilterMode value) {
        return value == TextureFilterMode::Point || value == TextureFilterMode::Bilinear ||
               value == TextureFilterMode::Trilinear;
    };
    const auto validAddress = [](TextureAddressMode value) {
        return value == TextureAddressMode::Repeat || value == TextureAddressMode::MirroredRepeat ||
               value == TextureAddressMode::ClampToEdge;
    };
    if (!validFilter(desc.sampler.filterMode) || !validAddress(desc.sampler.addressModeU) ||
        !validAddress(desc.sampler.addressModeV)) {
        return false;
    }
    TextureMipLayout layout;
    if (!computeTextureLayout(desc, layout))
        return false;
    return pixels.size() == layout.totalSize;
}

// ---- Texture ----

Texture::Texture(const TextureDesc& desc)
    : type_(desc.type), format_(desc.format), width_(desc.width),
      height_(desc.height), depth_(desc.depth), arrayLayers_(desc.arrayLayers),
      mipCount_(desc.mipCount), usage_(desc.usage), samples_(desc.samples), tiling_(desc.tiling) {
    const rhi::TextureDesc deviceDesc{.dimension = toRhi(desc.type),
                                      .format = toRhi(desc.format),
                                      .width = desc.width,
                                      .height = desc.height,
                                      .depth = desc.depth,
                                      .arrayLayers = desc.arrayLayers,
                                      .mipCount = desc.mipCount,
                                      .usage = usage_,
                                      .tiling = desc.tiling,
                                      .samples = desc.samples,
                                      .debugName = desc.debugName};
    initializeGpu(deviceDesc, desc.sampler);
}

void Texture::initializeGpu(const rhi::TextureDesc& deviceDesc, const SamplerDesc& sampler) {
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device) {
        Log::error("Texture", "No active device for texture creation");
        return;
    }
    deviceUid_ = device->uid();
    texture_ = device->texture_create(deviceDesc);
    if (!texture_) {
        Log::error("Texture", "Failed to create RHI texture");
        return;
    }
    // 层2 拥有默认 view：用层2 TextureViewDesc（全范围、格式跟随纹理）翻译成层3 desc 后创建。
    TextureViewDesc viewDesc;
    viewDesc.type = type_;
    viewDesc.mipCount = mipCount_;
    viewDesc.layerCount = arrayLayers_;
    view_ = device->texture_view_create(texture_, toRhiView(viewDesc, format_));
    sampler_ = device->sampler_create(toRhi(sampler));
    if (!view_ || !sampler_)
        Log::error("Texture", "Failed to create default view/sampler");
}

Texture::~Texture() {
    if (asset_ && asset_->instance_ == this)
        asset_->instance_ = nullptr;
    // 采纳的外部绑定（阴影贴图/RT）：view/sampler 归产出方（RenderGraph/RgTexturePool）所有，
    // 此处不销毁。
    if (!ownsGpu_)
        return;
    // 层2 拥有默认 view：先毁 view 再毁 texture；默认 sampler 由设备去重持有、不在此销毁。
    // 跨设备安全：asset-backed 纹理在其设备仍 active 时销毁（工程 teardown 顺序保证）；内建纹理
    // 在设备切换时由静态方法先 detachFromDeadDevice() 置空句柄，故此处 view_/texture_ 为空即跳过。
    if (rhi::IDevice* device = rhi::IDevice::active(); device && device->uid() == deviceUid_) {
        if (view_)
            device->texture_view_destroy(view_);
        if (texture_)
            device->texture_destroy(texture_);
    }
}

Ref<Texture> Texture::adoptBinding(rhi::RID view, rhi::RID sampler) {
    return Ref<Texture>(new Texture(view, sampler));
}

rhi::PixelFormat Texture::pixelFormat() const { return toRhi(format_); }

Ref<Texture> Texture::create(const TextureDesc& desc) {
    const auto pixelFormat = tryToRhiTextureFormat(desc.format);
    const bool color = rhi::isColorFormat(pixelFormat);
    const bool depth = rhi::isDepthFormat(pixelFormat);
    if (!rhi::IDevice::active() || desc.type != TextureType::Texture2D || (!color && !depth) ||
        desc.width == 0 || desc.height == 0 || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.mipCount == 0 || desc.usage == rhi::TextureUsage::None ||
        (desc.samples != rhi::SampleCount::One && desc.samples != rhi::SampleCount::Two &&
         desc.samples != rhi::SampleCount::Four && desc.samples != rhi::SampleCount::Eight) ||
        (desc.samples != rhi::SampleCount::One && desc.mipCount != 1) ||
        desc.mipCount > std::bit_width(std::max(desc.width, desc.height)) ||
        (desc.tiling != rhi::TextureTiling::Optimal && desc.tiling != rhi::TextureTiling::Linear) ||
        (color && rhi::hasFlag(desc.usage, rhi::TextureUsage::DepthStencilAttachment)) ||
        (depth && rhi::hasFlag(desc.usage, rhi::TextureUsage::ColorAttachment))) {
        Log::error("Texture", "Invalid or unsupported texture description or no active device");
        return {};
    }
    Ref<Texture> texture(new Texture(desc));
    if (!texture->textureHandle() || !texture->defaultView() || !texture->defaultSampler())
        return {};
    return texture;
}

Ref<Texture> Texture::createRenderTexture(std::uint32_t width, std::uint32_t height,
                                         TextureFormat format, rhi::TextureUsage usage) {
    TextureDesc desc{TextureType::Texture2D, format, width, height};
    desc.usage = usage;
    desc.debugName = "RenderTexture";
    desc.sampler.addressModeU = TextureAddressMode::ClampToEdge;
    desc.sampler.addressModeV = TextureAddressMode::ClampToEdge;
    return create(desc);
}

Ref<Texture> Texture::createColor(std::uint32_t width, std::uint32_t height,
                                 TextureFormat format) {
    if (!rhi::isColorFormat(tryToRhiTextureFormat(format))) {
        Log::error("Texture", "Color texture requires a color format");
        return {};
    }
    return createRenderTexture(width, height, format,
        rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled);
}

Ref<Texture> Texture::createDepth(std::uint32_t width, std::uint32_t height, TextureFormat format) {
    return createRenderTexture(width, height, format,
        rhi::TextureUsage::DepthStencilAttachment);
}

Ref<Texture> Texture::createShadowMap(std::uint32_t width, std::uint32_t height, TextureFormat format) {
    return createRenderTexture(width, height, format,
        rhi::TextureUsage::DepthStencilAttachment | rhi::TextureUsage::Sampled);
}

Texture::Texture(rhi::RID view, rhi::RID sampler)
    : view_(view), sampler_(sampler), ownsGpu_(false) {}

void Texture::bindAsset(TextureAsset* asset) {
    asset_ = asset;
}

const VirtualPath& Texture::assetPath() const {
    static const VirtualPath kEmpty;
    return asset_ ? asset_->assetPath() : kEmpty;
}

AssetId Texture::assetId() const {
    return asset_ ? asset_->assetId() : AssetId{};
}

rhi::TextureBinding Texture::binding(const Ref<Sampler>& sampler) const {
    return {view_, sampler ? sampler->rhiHandle() : sampler_};
}

void Texture::upload(std::span<const std::uint8_t> pixels) {
    if (!texture_)
        return;
    if (!rhi::hasFlag(usage_, rhi::TextureUsage::TransferDestination) ||
        !rhi::hasFlag(usage_, rhi::TextureUsage::Sampled) || samples_ != rhi::SampleCount::One) {
        Log::error("Texture", "Texture was not created for pixel uploads");
        return;
    }
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device || device->uid() != deviceUid_) {
        Log::error("Texture", "No active device for texture upload");
        return;
    }
    TextureDesc desc;
    desc.type = type_;
    desc.format = format_;
    desc.width = width_;
    desc.height = height_;
    desc.depth = depth_;
    desc.arrayLayers = arrayLayers_;
    desc.mipCount = mipCount_;
    TextureMipLayout layout;
    if (!computeTextureLayout(desc, layout) || pixels.size() != layout.totalSize) {
        Log::error("Texture", "Pixel data does not match the texture layout");
        return;
    }
    std::vector<rhi::TextureUploadRegion> uploads;
    uploads.reserve(layout.offsets.size());
    std::uint32_t mipWidth = width_;
    std::uint32_t mipHeight = height_;
    for (std::uint32_t level = 0; level < layout.offsets.size(); ++level) {
        const std::size_t begin = layout.offsets[level];
        const std::span<const std::byte> data{
            reinterpret_cast<const std::byte*>(pixels.data() + begin), layout.sizes[level]};
        uploads.push_back({level, 0, mipWidth, mipHeight, data});
        mipWidth = std::max(1U, mipWidth / 2U);
        mipHeight = std::max(1U, mipHeight / 2U);
    }
    device->texture_upload(texture_, uploads);
}

Ref<Texture> Texture::clone() const {
    if (!asset_) {
        Log::error("Texture", "clone() requires an asset-backed texture");
        return {};
    }
    return asset_->clone();
}

Ref<Texture> Texture::defaultWhite() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[4] = {0xffU, 0xffU, 0xffU, 0xffU};
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Srgb, 1, 1, 1};
        if (instance)
            instance->detachFromDeadDevice(); // 旧设备已亡，弃置句柄避免 ~Texture 误销毁
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

Ref<Texture> Texture::defaultBlack() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[4] = {0x00U, 0x00U, 0x00U, 0xffU};
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Srgb, 1, 1, 1};
        if (instance)
            instance->detachFromDeadDevice(); // 旧设备已亡，弃置句柄避免 ~Texture 误销毁
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

Ref<Texture> Texture::defaultNormal() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[4] = {0x80U, 0x80U, 0xffU, 0xffU};
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Unorm, 1, 1, 1};
        if (instance)
            instance->detachFromDeadDevice(); // 旧设备已亡，弃置句柄避免 ~Texture 误销毁
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

Ref<Texture> Texture::errorTexture() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[16] = {
            0xffU,
            0x00U,
            0xffU,
            0xffU,
            0x00U,
            0x00U,
            0x00U,
            0xffU,
            0x00U,
            0x00U,
            0x00U,
            0xffU,
            0xffU,
            0x00U,
            0xffU,
            0xffU,
        };
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Srgb, 2, 2, 1};
        if (instance)
            instance->detachFromDeadDevice(); // 旧设备已亡，弃置句柄避免 ~Texture 误销毁
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

// ---- TextureAsset ----

TextureAsset::~TextureAsset() {
    // 双向观察者：若 asset 先于其唯一实例销毁（如工程关闭时 AssetManager 缓存先清），
    // 必须清除实例的回指指针，否则 ~Texture 会解引用悬垂的 asset_（UAF）。
    if (instance_)
        instance_->asset_ = nullptr;
}

bool TextureAsset::transfer(Transfer& archive) {
    std::uint32_t magic = kTextureMagic;
    std::uint16_t version = kTextureVersion;
    TextureAssetDesc decodedDesc;
    std::vector<std::uint8_t> decodedPixels;
    TextureAssetDesc& targetDesc = archive.reading() ? decodedDesc : desc;
    std::vector<std::uint8_t>& targetPixels = archive.reading() ? decodedPixels : pixels;
    if ((archive.writing() && !validateTextureAsset(desc, pixels)) || !archive.beginObject({}) ||
        !archive.transfer("magic", magic) || magic != kTextureMagic ||
        !archive.transfer("version", version) || version != kTextureVersion ||
        !archive.transfer("description", targetDesc) || !archive.transfer("pixels", targetPixels) ||
        !archive.endObject() ||
        (archive.reading() && !validateTextureAsset(decodedDesc, decodedPixels))) {
        Log::error(
            "TextureAsset", "Invalid TextureAsset payload: %s", assetPath().string().c_str());
        return false;
    }
    if (archive.reading()) {
        desc = decodedDesc;
        pixels = std::move(decodedPixels);
        // 就地重传（热重载）后把新像素推送给唯一实例，保持活链接。
        syncInstance();
    }
    return true;
}

Ref<Texture> TextureAsset::instantiate() {
    if (instance_)
        return Ref<Texture>(instance_); // 复用唯一实例（addRef）
    Ref<Texture> texture(new Texture(desc));
    if (!texture->textureHandle()) {
        Log::error(
            "TextureAsset", "Failed to instantiate texture for %s", assetPath().string().c_str());
        return {};
    }
    texture->bindAsset(this);
    texture->initialize(pixels);
    instance_ = texture.get();
    return texture;
}

Ref<Texture> TextureAsset::clone() {
    Ref<Texture> texture(new Texture(desc));
    if (!texture->textureHandle()) {
        Log::error("TextureAsset", "Failed to clone texture for %s", assetPath().string().c_str());
        return {};
    }
    texture->initialize(pixels); // 脱离实例：不登记、不链接 asset
    return texture;
}

void TextureAsset::syncInstance() {
    if (instance_)
        instance_->upload(pixels);
}

Ref<Texture> resolveTextureReference(std::string_view reference) {
    if (reference.empty())
        return Texture::defaultWhite();
    if (reference == "engine://textures/white")
        return Texture::defaultWhite();
    if (reference == "engine://textures/black")
        return Texture::defaultBlack();
    if (reference == "engine://textures/normal")
        return Texture::defaultNormal();
    if (reference == "engine://textures/error")
        return Texture::errorTexture();

    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    if (path.valid() && path.scheme() == "assets") {
        if (const Ref<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(path)) {
            if (Ref<Texture> texture = asset->instantiate())
                return texture;
        }
    }
    Log::warn("Texture",
              "Using the error Texture for unresolved reference: %.*s",
              static_cast<int>(reference.size()),
              reference.data());
    return Texture::errorTexture();
}

} // namespace engine
