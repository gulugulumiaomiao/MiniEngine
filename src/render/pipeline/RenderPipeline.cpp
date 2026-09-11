#include "render/pipeline/RenderPipeline.h"

#include "core/logging/Log.h"

namespace engine {

void RenderPipelineRegistry::registerPipeline(std::string name, RenderPipelineFactory factory) {
    factories_.emplace(std::move(name), std::move(factory));
}

std::unique_ptr<IRenderPipeline> RenderPipelineRegistry::create(const std::string& name) const {
    const auto found = factories_.find(name);
    if (found == factories_.end()) {
        Log::fatal("RenderPipeline", "Unknown render pipeline '{}'", name.c_str());
        return nullptr;
    }
    return found->second();
}

} // namespace engine
