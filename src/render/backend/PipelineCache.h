#pragma once

#include "render/shader/ShaderCompiler.h"
#include "rhi/api/PipelineDesc.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

class ShaderPass;
class Shader;
class RhiShaderCache;
struct VertexLayout;

class PipelineCache final {
    public:
    PipelineCache(rhi::IDevice& device, rhi::BindGroupLayoutHandle sceneLayout,
                  rhi::BindGroupLayoutHandle materialLayout, CompiledShaderCache& compiledShaders,
                  ShaderProgramCache& programs, RhiShaderCache& shaders);

    [[nodiscard]] rhi::GraphicsPipelineHandle
    getOrCreate(const Shader& shader, const ShaderPass& pass, const ShaderVariantKey& variant,
                const VertexLayout& vertexLayout, rhi::TextureFormat colorFormat);
    void clear();
    void invalidate(std::span<const CompiledShaderId> shaders, std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);

    private:
    struct Slot {
        rhi::GraphicsPipelineHandle pipeline;
        ShaderProgramId program{};
        CompiledShaderId vertex{};
        CompiledShaderId fragment{};
        std::uint64_t key{};
        bool alive{};
    };

    struct RetiredPipeline {
        rhi::GraphicsPipelineHandle pipeline;
        std::uint64_t serial{};
    };

    [[nodiscard]] static std::uint64_t makeKey(const ShaderProgram& program, const ShaderPass& pass,
                                               const VertexLayout& vertexLayout,
                                               rhi::TextureFormat colorFormat);
    [[nodiscard]] static std::uint64_t makeFallbackKey(const Shader& shader, const ShaderPass& pass,
                                                       const ShaderVariantKey& variant,
                                                       const VertexLayout& vertexLayout,
                                                       rhi::TextureFormat colorFormat);
    [[nodiscard]] rhi::GraphicsPipelineDesc
    makeDesc(const ShaderPass& pass, const VertexLayout& vertexLayout,
             rhi::TextureFormat colorFormat, rhi::ShaderHandle vertexShader,
             std::string vertexEntry, rhi::ShaderHandle fragmentShader,
             std::string fragmentEntry) const;

    rhi::IDevice& device_;
    rhi::BindGroupLayoutHandle sceneLayout_;
    rhi::BindGroupLayoutHandle materialLayout_;
    CompiledShaderCache& compiledShaders_;
    ShaderProgramCache& programs_;
    RhiShaderCache& shaders_;
    std::unordered_map<std::uint64_t, std::uint32_t> entries_;
    std::unordered_map<std::uint64_t, std::uint32_t> fallbackEntries_;
    std::vector<Slot> slots_;
    std::vector<RetiredPipeline> retired_;
};

} // namespace engine
