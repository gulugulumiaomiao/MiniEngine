#pragma once
#include "render/material/Material.h"
namespace engine::test {
inline MaterialCreateInfo materialInfo(std::vector<ShaderPropertyInfo> properties = {}) {
    ShaderCreateInfo shader;
    shader.properties = std::move(properties);
    shader.name = "Tests/RuntimeMaterial";
    ShaderPassCreateInfo pass;
    pass.name = "Forward";
    pass.program = {VirtualPath{"memory://test.vert"}, VirtualPath{"memory://test.frag"}};
    SubShaderCreateInfo sub;
    sub.passes.push_back(pass);
    shader.subShaders.push_back(sub);
    MaterialCreateInfo info;
    info.shader = makeRef<Shader>(std::move(shader));
    return info;
}
} // namespace engine::test
