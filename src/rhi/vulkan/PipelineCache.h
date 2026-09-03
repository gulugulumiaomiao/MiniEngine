#pragma once

#include "rhi/RhiTypes.h"

#include <vulkan/vulkan.h>

#include "renderer/ShaderCompiler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

class GraphicsPipeline;
class ShaderPass;
class Shader;
class RhiShaderCache;
struct VertexLayout;

class PipelineCache final {
public:
    PipelineCache(VkDevice device, VkDescriptorSetLayout sceneLayout,
                  VkDescriptorSetLayout materialLayout,
                  CompiledShaderCache& compiledShaders,
                  ShaderProgramCache& programs, RhiShaderCache& shaders);

    [[nodiscard]] rhi::GraphicsPipelineHandle getOrCreate(
        const Shader& shader, const ShaderPass& pass,
        const ShaderVariantKey& variant,
        const VertexLayout& vertexLayout,
        VkFormat colorFormat);
    [[nodiscard]] const GraphicsPipeline& resolve(
        rhi::GraphicsPipelineHandle handle) const;
    void clear();
    void invalidate(std::span<const CompiledShaderId> shaders,
                    std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);

private:
    struct Slot {
        std::unique_ptr<GraphicsPipeline> pipeline;
        ShaderProgramId program{};
        CompiledShaderId vertex{};
        CompiledShaderId fragment{};
        std::uint64_t key{};
        std::uint32_t generation{1};
        bool alive{};
    };

    struct RetiredPipeline {
        std::unique_ptr<GraphicsPipeline> pipeline;
        std::uint64_t serial{};
    };

    [[nodiscard]] static std::uint64_t makeKey(
        const ShaderProgram& program, const ShaderPass& pass,
        const VertexLayout& vertexLayout, VkFormat colorFormat);

    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorSetLayout sceneLayout_{VK_NULL_HANDLE};
    VkDescriptorSetLayout materialLayout_{VK_NULL_HANDLE};
    CompiledShaderCache& compiledShaders_;
    ShaderProgramCache& programs_;
    RhiShaderCache& shaders_;
    std::unordered_map<std::uint64_t, std::uint32_t> entries_;
    std::vector<Slot> slots_;
    std::vector<RetiredPipeline> retired_;
};

} // namespace engine
