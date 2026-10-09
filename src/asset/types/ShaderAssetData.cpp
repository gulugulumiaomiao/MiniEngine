#include "asset/types/ShaderAssetData.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"

#include <algorithm>

namespace engine {

namespace {

constexpr std::uint32_t kShaderAssetMagic = 0x52444853U;
constexpr std::uint16_t kShaderAssetVersion = 3;

template <typename Enum>
bool transferShaderEnum(Transfer& archive, std::string_view name, Enum& value, Enum maximum) {
    return archive.transfer(name, value) && value >= static_cast<Enum>(0) && value <= maximum;
}

} // namespace

bool ShaderInterfaceAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", name) &&
           archive.transfer("semantic", semantic) &&
           transferShaderEnum(archive, "type", type, ShaderAssetValueType::Vec4) &&
           archive.transfer("location", location) &&
           transferShaderEnum(
               archive, "interpolation", interpolation, ShaderAssetInterpolation::NoPerspective) &&
           archive.endObject();
}

bool ShaderRenderStateAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) &&
           transferShaderEnum(archive, "cull", cull, ShaderAssetCullMode::Back) &&
           transferShaderEnum(
               archive, "front_face", frontFace, ShaderAssetFrontFace::CounterClockwise) &&
           transferShaderEnum(archive, "fill", fill, ShaderAssetFillMode::Wireframe) &&
           transferShaderEnum(
               archive, "topology", topology, ShaderAssetPrimitiveTopology::LineList) &&
           archive.transfer("depth_write", depthWrite) &&
           transferShaderEnum(archive, "depth_test", depthTest, ShaderAssetDepthCompare::Always) &&
           transferShaderEnum(archive, "blend", blend, ShaderAssetBlendMode::PremultipliedAlpha) &&
           archive.transfer("color_mask", colorMask) && archive.endObject();
}

bool ShaderPropertyAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", name) &&
           archive.transfer("display_name", displayName) &&
           transferShaderEnum(archive, "property_type", type, ShaderAssetPropertyType::Matrix) &&
           archive.transfer("default_value", defaultValue) && archive.transfer("range", range) &&
           archive.transfer("attributes", attributes) && archive.endObject();
}

bool ShaderPassAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("name", pass.name) &&
           transferShaderEnum(archive, "pass_type", pass.type, ShaderAssetPassType::ShadowCaster) &&
           archive.transfer("vertex_source", pass.program.vertexSource) &&
           archive.transfer("fragment_source", pass.program.fragmentSource) &&
           archive.transfer("vertex_input", pass.vertexInput) &&
           archive.transfer("varyings", pass.varyings) &&
           archive.transfer("fragment_outputs", pass.fragmentOutputs) &&
           archive.transfer("features", pass.features) &&
           archive.transfer("render_state", renderState) && archive.endObject();
}

bool SubShaderAssetData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("render_pipeline", renderPipeline) &&
           archive.transfer("render_queue", renderQueue) && archive.transfer("passes", passes) &&
           archive.endObject();
}

const ShaderPassAssetDesc& SubShaderAssetData::requirePass(ShaderAssetPassType type) const {
    const auto found = std::ranges::find_if(
        passes, [type](const ShaderPassAssetData& asset) { return asset.pass.type == type; });
    if (found == passes.end()) {
        Log::fatal("ShaderAsset", "SubShader has no required pass");
    }
    return found->pass;
}

bool ShaderAssetData::transfer(Transfer& archive) {
    std::uint32_t magic = kShaderAssetMagic;
    std::uint16_t version = kShaderAssetVersion;
    return archive.beginObject({}) && archive.transfer("magic", magic) &&
           magic == kShaderAssetMagic && archive.transfer("version", version) &&
           version == kShaderAssetVersion && archive.transfer("name", name) &&
           archive.transfer("properties", properties) &&
           archive.transfer("global_properties", globalProperties) &&
           archive.transfer("sub_shaders", subShaders) && !subShaders.empty() &&
           archive.endObject();
}

} // namespace engine
