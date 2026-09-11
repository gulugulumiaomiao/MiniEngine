#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace engine {

class IRenderPipeline {
public:
    virtual ~IRenderPipeline() = default;

    virtual void render(class RenderContext& context) = 0;
    virtual void onSwapchainChanged() {}
};

using RenderPipelineFactory = std::function<std::unique_ptr<IRenderPipeline>()>;

class RenderPipelineRegistry {
public:
    void registerPipeline(std::string name, RenderPipelineFactory factory);

    [[nodiscard]] std::unique_ptr<IRenderPipeline> create(const std::string& name) const;

private:
    std::unordered_map<std::string, RenderPipelineFactory> factories_;
};

} // namespace engine
