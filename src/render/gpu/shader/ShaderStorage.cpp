#include "render/gpu/shader/ShaderStorage.h"

#include "core/base/BuildConfig.h"
#include "core/logging/Log.h"
#include "render/gpu/pipeline/GraphicsPipelineStorage.h"
#include "render/gpu/shader/ShaderStorageFactory.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace engine {

ShaderStorage::ShaderStorage() = default;
ShaderStorage::~ShaderStorage() = default;

bool ShaderStorage::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("ShaderStorage", "Manager is already initialized");
        return false;
    }
    ShaderCompilePipelineConfig config;
#if defined(MINI_PUBLISH)
    config.mode = ShaderCompileMode::PackagedRuntime;
#else
    config.mode = ShaderCompileMode::DevelopmentRuntime;
#endif
    config.preprocessorConfig.includeSearchPaths = {VirtualPath{"assets://shaders/include"}};
    config.compilerOptions.compilerVersion = MINI_GLSLC_EXECUTABLE;
#if !defined(NDEBUG)
    config.compilerOptions.optimization = ShaderOptimization::Debug;
#else
    config.compilerOptions.optimization = ShaderOptimization::Release;
#endif
    compilePipeline_ = std::make_unique<ShaderCompilePipeline>(std::move(config));
    device_ = &device;
    factory_ = std::make_unique<ShaderStorageFactory>(device);
    SHADER_RESOURCE_MANAGER.setDestroyObserver([this](RID handle) { release(handle); });
    return true;
}

RID ShaderStorage::getOrCreateProgram(const Shader& shader,
                                      const ShaderPass& pass,
                                      const ShaderVariantKey& variant) {
    return initialized() ? compilePipeline_->getOrCreate(shader, pass, variant) : RID{};
}

const ShaderProgram& ShaderStorage::resolveProgram(RID handle) const {
    return compilePipeline_->resolveProgram(handle);
}

const CompiledShader& ShaderStorage::resolveCompiled(RID handle) const {
    return compilePipeline_->resolveCompiled(handle);
}

rhi::RID ShaderStorage::resolve(RID handle) {
    if (!initialized())
        return {};
    const CompiledShader& shader = compilePipeline_->resolveCompiled(handle);
    if (const ShaderStorageEntry* cached = cache_.find(shader.id))
        return cached->shader;
    ShaderStorageEntry created;
    if (!factory_->create(shader, created))
        return {};
    auto stored = cache_.store(shader.id, std::move(created));
    if (stored.replaced)
        factory_->release(*stored.replaced);
    return stored.stored->shader;
}

std::vector<CompiledShaderId> ShaderStorage::invalidateChanged(std::uint64_t retireSerial) {
    if (!initialized())
        return {};
    std::vector<CompiledShaderId> changed = compilePipeline_->invalidateChanged();
    invalidate(changed, retireSerial);
    return changed;
}

void ShaderStorage::invalidate(std::span<const CompiledShaderId> shaders,
                               std::uint64_t retireSerial) {
    if (!initialized() || shaders.empty())
        return;
    auto modules = cache_.extractIf([&](CompiledShaderId id, const ShaderStorageEntry&) {
        return std::ranges::find(shaders, id) != shaders.end();
    });
    for (auto& entry : modules)
        retired_.push_back({std::move(entry.second), retireSerial});
}

void ShaderStorage::collect(std::uint64_t completedSerial) {
    if (!initialized())
        return;
    std::erase_if(retired_, [&](RetiredShader& retired) {
        if (retired.serial > completedSerial)
            return false;
        factory_->release(retired.resource);
        return true;
    });
}

void ShaderStorage::release(RID handle) {
    (void)handle;
    if (!initialized())
        return;
    device_->waitIdle();
    GRAPHICS_PIPELINE_STORAGE.clear();
    for (RetiredShader& retired : retired_)
        factory_->release(retired.resource);
    retired_.clear();
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
}

void ShaderStorage::shutdown() {
    SHADER_RESOURCE_MANAGER.setDestroyObserver({});
    if (!initialized())
        return;
    for (RetiredShader& retired : retired_)
        factory_->release(retired.resource);
    retired_.clear();
    for (auto& entry : cache_.extractAll())
        factory_->release(entry.second);
    factory_.reset();
    compilePipeline_->clear();
    compilePipeline_.reset();
    device_ = nullptr;
}

} // namespace engine
