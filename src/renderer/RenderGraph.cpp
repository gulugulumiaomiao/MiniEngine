#include "renderer/RenderGraph.h"

#include "core/Log.h"

#include <algorithm>
#include <stdexcept>

namespace engine {

void RenderGraph::importTexture(ImportedTexture texture) {
    textures_.push_back(texture);
}

void RenderGraph::addGraphicsPass(std::string name, rhi::RenderingInfo rendering,
                                  std::vector<ResourceUsage> resources,
                                  ExecuteCallback execute) {
    passes_.push_back({std::move(name), std::move(rendering), std::move(resources),
                       std::move(execute)});
}

void RenderGraph::execute(rhi::IGraphicsCommandEncoder& encoder) const {
    struct State {
        rhi::TextureHandle texture;
        rhi::TextureAspect aspect;
        rhi::ResourceState current;
        rhi::ResourceState final;
    };
    std::vector<State> states;
    states.reserve(textures_.size());
    for (const ImportedTexture& texture : textures_) {
        states.push_back({texture.texture, texture.aspect, texture.initialState,
                          texture.finalState});
    }

    for (const GraphicsPass& pass : passes_) {
        std::vector<rhi::TextureBarrier> barriers;
        for (const ResourceUsage& usage : pass.resources) {
            auto state = std::ranges::find_if(states, [&usage](const State& candidate) {
                return candidate.texture == usage.texture;
            });
            if (state == states.end()) {
                Log::fatal("RenderGraph",
                           "Pass uses a texture that was not imported");
            }
            if (state->current != usage.state) {
                barriers.push_back({usage.texture, usage.aspect, state->current, usage.state});
                state->current = usage.state;
            }
        }
        encoder.beginDebugLabel(pass.name, {0.25F, 0.55F, 1.0F, 1.0F});
        encoder.resourceBarriers(barriers);
        encoder.beginRendering(pass.rendering);
        pass.execute(encoder);
        encoder.endRendering();
        encoder.endDebugLabel();
    }

    std::vector<rhi::TextureBarrier> finalBarriers;
    for (const State& state : states) {
        if (state.current != state.final) {
            finalBarriers.push_back({state.texture, state.aspect, state.current, state.final});
        }
    }
    encoder.resourceBarriers(finalBarriers);
}

} // namespace engine
