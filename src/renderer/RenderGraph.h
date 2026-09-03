#pragma once

#include "rhi/CommandEncoder.h"

#include <functional>
#include <string>
#include <vector>

namespace engine {

class RenderGraph final {
public:
    using ExecuteCallback = std::function<void(rhi::IGraphicsCommandEncoder&)>;

    struct ResourceUsage {
        rhi::TextureHandle texture;
        rhi::TextureAspect aspect{rhi::TextureAspect::Color};
        rhi::ResourceState state{rhi::ResourceState::ShaderRead};
    };

    struct ImportedTexture {
        rhi::TextureHandle texture;
        rhi::ResourceState initialState{rhi::ResourceState::Undefined};
        rhi::ResourceState finalState{rhi::ResourceState::Undefined};
        rhi::TextureAspect aspect{rhi::TextureAspect::Color};
    };

    void importTexture(ImportedTexture texture);
    void addGraphicsPass(std::string name, rhi::RenderingInfo rendering,
                         std::vector<ResourceUsage> resources, ExecuteCallback execute);
    void execute(rhi::IGraphicsCommandEncoder& encoder) const;

private:
    struct GraphicsPass {
        std::string name;
        rhi::RenderingInfo rendering;
        std::vector<ResourceUsage> resources;
        ExecuteCallback execute;
    };

    std::vector<ImportedTexture> textures_;
    std::vector<GraphicsPass> passes_;
};

} // namespace engine
