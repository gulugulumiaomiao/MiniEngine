#include "render/gpu/material/MaterialStorage.h"

#include "core/logging/Log.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/GraphicsPipelineStorage.h"
#include "render/material/MaterialManager.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderProgram.h"
#include "render/texture/Sampler.h"
#include "render/uniform/Ubo.h"
#include "rhi/api/Device.h"

#include <span>
#include <string_view>
#include <vector>

namespace engine {

MaterialStorage::MaterialStorage() = default;
MaterialStorage::~MaterialStorage() = default;

bool MaterialStorage::initialize(std::uint32_t frameCount) {
    if (initialized()) {
        Log::error("MaterialStorage", "Manager is already initialized");
        return false;
    }
    if (!cache_.initialize(frameCount, kMaxResidentMaterials)) {
        Log::error("MaterialStorage", "Cannot initialize Material binding cache");
        return false;
    }
    initialized_ = true;
    MATERIAL_RESOURCE_MANAGER.setDestroyObserver([this](RID handle) { invalidate(handle); });
    return true;
}

std::uint64_t MaterialStorage::cacheKey(RID handle) {
    return handleKey(handle);
}

namespace {

[[nodiscard]] bool sameTextureBindings(const std::vector<rhi::TextureBinding>& lhs,
                                       std::span<const rhi::TextureBinding> rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs[i].view != rhs[i].view || lhs[i].sampler != rhs[i].sampler)
            return false;
    }
    return true;
}

} // namespace

MaterialPassState MaterialStorage::resolvePass(const Material& material,
                                               const Mesh& mesh,
                                               ShaderPassType passType,
                                               std::string_view renderPipeline,
                                               rhi::PixelFormat colorFormat,
                                               rhi::PixelFormat depthFormat) {
    const SubShader* subShader = material.shader().selectSubShader(renderPipeline);
    if (!subShader)
        return {};
    const ShaderPass* pass = subShader->findPass(passType);
    if (!pass)
        return {};
    const ShaderVariantKey variant = pass->variantKey(material.keywords);
    const Ref<ShaderProgram> program =
        GRAPHICS_PIPELINE_STORAGE.getOrCreateProgram(material.shader(), *pass, variant);
    if (!program)
        return {};
    const rhi::RID pipeline =
        GRAPHICS_PIPELINE_STORAGE.resolve(*program, mesh, colorFormat, depthFormat);
    if (!pipeline)
        return {};
    return {pass, pipeline, resolve(material, *program), program->setCount()};
}

Ref<UniformSet> MaterialStorage::resolve(const Material& material, const ShaderProgram& program) {
    // A pass whose SPIR-V stripped the material block (e.g. depth-only shadow caster) has no set 1;
    // such draws legitimately bind no material set.
    if (!initialized() || !material.resourceId() || program.setCount() < 2)
        return {};
    const Ref<UniformSetLayout> setLayout = program.setLayout(1);
    if (!setLayout)
        return {};
    if (!collectTextureBindings(material))
        return {};

    const std::uint64_t key = cacheKey(material.resourceId());
    MaterialStorageCacheSlot slot = cache_.acquire(key);
    MaterialStorageEntry& entry = *slot.resource;

    // Rebuild when the slot is fresh/evicted or the material's shader changed its set-1 layout.
    if (entry.pendingRelease || !entry.materialSet || entry.setLayout.get() != setLayout.get()) {
        entry.materialSet = makeRef<UniformSet>(setLayout, program.uboLayouts(1));
        entry.setLayout = setLayout;
        entry.uniformVersion = 0; // force the uniform write below
        entry.textureBindings.clear();
        entry.pendingRelease = false;
    }

    if (entry.uniformVersion != material.version()) {
        if (Ubo* ubo = entry.materialSet->uboAt(0))
            ubo->setRawBytes(material.uniformBytes());
        entry.uniformVersion = material.version();
    }

    if (!sameTextureBindings(entry.textureBindings, textureScratch_)) {
        // Material textures occupy set-1 bindings 1..N in property-declaration order (binding 0 is
        // the material uniform block). Positions match binding indices here (contiguous from 0).
        std::uint32_t binding = 1;
        for (const ShaderPropertyDesc& property : material.shader().properties()) {
            if (property.type != ShaderPropertyType::Texture2D)
                continue;
            const Ref<Texture> texture = material.resolveTexture(property.name);
            if (texture)
                entry.materialSet->setResourceAt(binding, texture);
            ++binding;
        }
        entry.textureBindings = textureScratch_;
    }

    entry.materialSet->upload();
    entry.ownerKey = key;
    return entry.materialSet;
}

bool MaterialStorage::collectTextureBindings(const Material& material) {
    textureScratch_.clear();
    for (const ShaderPropertyDesc& property : material.shader().properties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const Ref<Texture> texture = material.resolveTexture(property.name);
        const Ref<Sampler> sampler = material.resolveSampler(property.name);
        const rhi::TextureBinding resolved =
            texture ? (sampler ? texture->binding(sampler) : texture->binding())
                    : rhi::TextureBinding{};
        if (!resolved.view || !resolved.sampler) {
            Log::error(
                "MaterialStorage", "Material Texture is unavailable: %s", property.name.c_str());
            return false;
        }
        textureScratch_.push_back(resolved);
    }
    return true;
}

void MaterialStorage::invalidate(RID handle) {
    if (!initialized())
        return;
    // Extracted entries hold Ref<UniformSet>; wait for the GPU then let them drop (freeing the
    // descriptor set + material UBO automatically).
    std::vector<MaterialStorageEntry> resources = cache_.extract(cacheKey(handle));
    if (resources.empty())
        return;
    if (rhi::IDevice* device = RHI_DEVICE)
        device->waitIdle();
}

void MaterialStorage::beginFrame(std::uint32_t frameIndex) {
    if (initialized())
        cache_.beginFrame(frameIndex);
}

void MaterialStorage::shutdown() {
    MATERIAL_RESOURCE_MANAGER.setDestroyObserver({});
    if (!initialized())
        return;
    cache_.reset(); // drops every entry -> releases each Ref<UniformSet>
    initialized_ = false;
}

} // namespace engine
