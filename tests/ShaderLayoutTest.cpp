#include "renderer/AssetManager.h"
#include "renderer/Shader.h"

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

engine::ShaderPropertyDesc property(std::string name, engine::ShaderPropertyType type) {
    engine::ShaderPropertyDesc result;
    result.name = std::move(name);
    result.type = type;
    return result;
}

bool memberEquals(const engine::UniformBlockLayout& layout, std::string_view name,
                  std::uint32_t offset, std::uint32_t size,
                  std::uint32_t alignment) {
    const engine::UniformMemberLayout* member = layout.findMember(name);
    return member && member->offset == offset && member->size == size &&
           member->alignment == alignment;
}

} // namespace

int main() {
    using namespace engine;

    const std::vector<ShaderPropertyDesc> synthetic{
        property("Scalar", ShaderPropertyType::Float),
        property("Uv", ShaderPropertyType::Vec2),
        property("RangeValue", ShaderPropertyType::Range),
        property("Direction", ShaderPropertyType::Vec3),
        property("Enabled", ShaderPropertyType::Boolean),
        property("Texture", ShaderPropertyType::Texture2D),
    };
    const UniformBlockLayout syntheticLayout =
        buildUniformBlockLayout(synthetic);
    if (syntheticLayout.byteSize != 64 || syntheticLayout.members.size() != 5 ||
        !memberEquals(syntheticLayout, "Scalar", 0, 4, 4) ||
        !memberEquals(syntheticLayout, "Uv", 8, 8, 8) ||
        !memberEquals(syntheticLayout, "RangeValue", 16, 4, 4) ||
        !memberEquals(syntheticLayout, "Direction", 32, 16, 16) ||
        !memberEquals(syntheticLayout, "Enabled", 48, 4, 4) ||
        syntheticLayout.findMember("Texture") != nullptr) {
        return 1;
    }

    const ShaderAsset& shader = *ASSET_MANAGER.loadShaderAsset(
        std::filesystem::path{MINI_TEST_ASSET_DIR} /
        "shaders/vertex_color.shader.json");
    const UniformBlockLayout realLayout =
        buildUniformBlockLayout(shader.properties);
    if (realLayout.byteSize != 64 || realLayout.members.size() != 4 ||
        !memberEquals(realLayout, "BaseColor", 0, 16, 16) ||
        !memberEquals(realLayout, "UvScale", 16, 8, 8) ||
        !memberEquals(realLayout, "EmissiveColor", 32, 16, 16) ||
        !memberEquals(realLayout, "DebugParams", 48, 16, 16)) {
        return 2;
    }

    const std::vector<ShaderPropertyDesc> textureOnly{
        property("MainTexture", ShaderPropertyType::Texture2D)};
    const UniformBlockLayout emptyLayout =
        buildUniformBlockLayout(textureOnly);
    if (emptyLayout.byteSize != 0 || !emptyLayout.members.empty() ||
        isUniformProperty(ShaderPropertyType::Texture2D) ||
        !isUniformProperty(ShaderPropertyType::Color)) {
        return 3;
    }

}
