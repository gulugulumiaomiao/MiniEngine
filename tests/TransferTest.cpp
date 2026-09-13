#include "core/serialization/BinaryTransfer.h"
#include "core/serialization/JsonTransfer.h"
#include "core/serialization/Transferable.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshBuilder.h"
#include "render/material/Material.h"
#include "render/shader/Shader.h"
#include "render/texture/Texture.h"
#include "runtime/config/EngineConfig.h"
#include "scene/node/Node.h"
#include "scene/scene/SceneAsset.h"
#include <nlohmann/json.hpp>

#include <concepts>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace transfer_test {

struct Sample final : public engine::Transferable {
    bool enabled{true};
    std::int32_t count{-7};
    std::uint64_t mask{0xfedcba9876543210ULL};
    float weight{2.5F};
    double distance{1234.5};
    std::string name{"Sample"};
    engine::VirtualPath path{"assets://data/sample.bin"};
    engine::math::Vec2 uv{0.25F, 0.75F};
    engine::math::Vec3 position{1.0F, 2.0F, 3.0F};
    engine::math::Vec4 color{0.1F, 0.2F, 0.3F, 1.0F};
    engine::math::Mat33 basis{1.0F};
    engine::math::Mat44 matrix{1.0F};
    engine::math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    std::vector<std::uint32_t> values{3, 5, 8};
    std::vector<std::byte> bytes{std::byte{1}, std::byte{2}, std::byte{255}};
    std::optional<std::int32_t> optional{42};
    std::variant<float, std::string> variant{std::string{"text"}};

    [[nodiscard]] bool transfer(engine::Transfer& archive) override;
};

static_assert(!std::equality_comparable<engine::Transferable>);
static_assert(!std::equality_comparable<Sample>);

bool Sample::transfer(engine::Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("enabled", enabled) &&
           archive.transfer("count", count) && archive.transfer("mask", mask) &&
           archive.transfer("weight", weight) && archive.transfer("distance", distance) &&
           archive.transfer("name", name) && archive.transfer("path", path) &&
           archive.transfer("uv", uv) && archive.transfer("position", position) &&
           archive.transfer("color", color) && archive.transfer("basis", basis) &&
           archive.transfer("matrix", matrix) && archive.transfer("rotation", rotation) &&
           archive.transfer("values", values) && archive.transfer("bytes", bytes) &&
           archive.transfer("optional", optional) && archive.transfer("variant", variant) &&
           archive.endObject();
}

bool equal(const Sample& left, const Sample& right) {
    return left.enabled == right.enabled && left.count == right.count && left.mask == right.mask &&
           left.weight == right.weight && left.distance == right.distance &&
           left.name == right.name && left.path == right.path && left.uv == right.uv &&
           left.position == right.position && left.color == right.color &&
           left.basis == right.basis && left.matrix == right.matrix &&
           left.rotation == right.rotation && left.values == right.values &&
           left.bytes == right.bytes && left.optional == right.optional &&
           left.variant == right.variant;
}

// 每次仅改变一个字段，验证派生类没有遗漏比较项，且 == 与 != 在两个方向上均一致。
template <typename T, typename Member, typename Value>
bool checkMemberComparison(Member T::* member, const Value& value, const char* field) {
    static_assert(std::equality_comparable<T>);
    const T original;
    T changed = original;
    const bool initiallyEqual = original == changed && changed == original &&
                                !(original != changed) && !(changed != original);
    changed.*member = value;
    if (!initiallyEqual || original == changed || changed == original || !(original != changed) ||
        !(changed != original)) {
        std::fprintf(stderr, "字段比较失败：%s\n", field);
        return false;
    }
    return true;
}

bool testDerivedEquality() {
    using namespace engine;
#define CHECK_MEMBER(Type, Member, ...)                                                            \
    if (!checkMemberComparison(&Type::Member, __VA_ARGS__, #Type "::" #Member))                    \
    return false

    CHECK_MEMBER(VertexSemantic, type, VertexSemanticType::Color);
    CHECK_MEMBER(VertexSemantic, index, 1);
    CHECK_MEMBER(VertexBinding, binding, 1);
    CHECK_MEMBER(VertexBinding, stride, 16);
    CHECK_MEMBER(VertexBinding, inputRate, VertexInputRate::Instance);
    CHECK_MEMBER(VertexAttribute, semantic, VertexSemantic{VertexSemanticType::Color, 0});
    CHECK_MEMBER(VertexAttribute, format, VertexFormat::Vec4Float32);
    CHECK_MEMBER(VertexAttribute, location, 1);
    CHECK_MEMBER(VertexAttribute, binding, 1);
    CHECK_MEMBER(VertexAttribute, offset, 16);
    CHECK_MEMBER(VertexLayout, bindings, std::vector<VertexBinding>{VertexBinding{}});
    CHECK_MEMBER(VertexLayout, attributes, std::vector<VertexAttribute>{VertexAttribute{}});
    CHECK_MEMBER(Aabb, minimum, math::Vec3{1.0F});
    CHECK_MEMBER(Aabb, maximum, math::Vec3{1.0F});
    CHECK_MEMBER(BoundingSphere, center, math::Vec3{1.0F});
    CHECK_MEMBER(BoundingSphere, radius, 1.0F);
    const MeshBounds bounds{Aabb{math::Vec3{-1.0F}, math::Vec3{1.0F}},
                            BoundingSphere{math::Vec3{0.0F}, 1.0F}};
    CHECK_MEMBER(MeshBounds, aabb, bounds.aabb);
    CHECK_MEMBER(MeshBounds, sphere, bounds.sphere);
    CHECK_MEMBER(SubMesh, firstIndex, 1);
    CHECK_MEMBER(SubMesh, indexCount, 3);
    CHECK_MEMBER(SubMesh, vertexOffset, -1);
    CHECK_MEMBER(SubMesh, materialSlot, 1);
    CHECK_MEMBER(SubMesh, bounds, bounds);
    CHECK_MEMBER(VertexStream, binding, 1);
    CHECK_MEMBER(VertexStream, vertexCount, 3);
    CHECK_MEMBER(VertexStream, bytes, std::vector<std::byte>{std::byte{1}});

    CHECK_MEMBER(PlaneGeometry, size, math::Vec2{2.0F});
    CHECK_MEMBER(PlaneGeometry, segmentsX, 2);
    CHECK_MEMBER(PlaneGeometry, segmentsZ, 2);
    CHECK_MEMBER(BoxGeometry, size, math::Vec3{2.0F});
    CHECK_MEMBER(BoxGeometry, segmentsX, 2);
    CHECK_MEMBER(BoxGeometry, segmentsY, 2);
    CHECK_MEMBER(BoxGeometry, segmentsZ, 2);
    CHECK_MEMBER(UvSphereGeometry, radius, 1.0F);
    CHECK_MEMBER(UvSphereGeometry, longitudeSegments, 8);
    CHECK_MEMBER(UvSphereGeometry, latitudeSegments, 4);
    CHECK_MEMBER(CylinderGeometry, bottomRadius, 1.0F);
    CHECK_MEMBER(CylinderGeometry, topRadius, 1.0F);
    CHECK_MEMBER(CylinderGeometry, height, 2.0F);
    CHECK_MEMBER(CylinderGeometry, radialSegments, 8);
    CHECK_MEMBER(CylinderGeometry, heightSegments, 2);
    CHECK_MEMBER(CylinderGeometry, capBottom, false);
    CHECK_MEMBER(CylinderGeometry, capTop, false);
    CHECK_MEMBER(MeshPrimitive, value, PlaneGeometry{math::Vec2{2.0F}, 1, 1});
    CHECK_MEMBER(MeshPrimitive, value, BoxGeometry{});
    CHECK_MEMBER(MeshPrimitivePart, primitive, MeshPrimitive{BoxGeometry{}});
    CHECK_MEMBER(MeshPrimitivePart, translation, math::Vec3{1.0F});
    const math::Quat rotation{0.0F, 1.0F, 0.0F, 0.0F};
    CHECK_MEMBER(MeshPrimitivePart, rotation, rotation);
    CHECK_MEMBER(MeshPrimitivePart, scale, math::Vec3{2.0F});
    CHECK_MEMBER(MeshPrimitivePart, materialSlot, 1);
    CHECK_MEMBER(MeshBuildRecipe, name, "changed");
    CHECK_MEMBER(MeshBuildRecipe, parts, std::vector<MeshPrimitivePart>{MeshPrimitivePart{}});
    CHECK_MEMBER(MeshBuildRecipe, vertexLayout, PrimitiveVertexLayout::Position);
    CHECK_MEMBER(MeshBuildRecipe, indexPolicy, MeshIndexPolicy::UInt16);
    CHECK_MEMBER(MeshBuildRecipe, usage, MeshUsage::Dynamic);
    CHECK_MEMBER(MeshBuildRecipe, keepCpuCopy, true);

    CHECK_MEMBER(TransformComponentAsset, position, math::Vec3{1.0F});
    CHECK_MEMBER(TransformComponentAsset, rotation, rotation);
    CHECK_MEMBER(TransformComponentAsset, scale, math::Vec3{2.0F});
    CHECK_MEMBER(CameraComponentAsset, projection, CameraProjection::Orthographic);
    CHECK_MEMBER(CameraComponentAsset, fieldOfView, 75.0F);
    CHECK_MEMBER(CameraComponentAsset, orthographicSize, 10.0F);
    CHECK_MEMBER(CameraComponentAsset, nearPlane, 0.2F);
    CHECK_MEMBER(CameraComponentAsset, farPlane, 500.0F);
    CHECK_MEMBER(CameraComponentAsset, clearColor, math::Vec4{1.0F});
    CHECK_MEMBER(CameraComponentAsset, cullingMask, 1U);
    CHECK_MEMBER(CameraComponentAsset, priority, 1);
    CHECK_MEMBER(CameraComponentAsset, primary, true);
    CHECK_MEMBER(CameraComponentAsset, enabled, false);
    CHECK_MEMBER(LightComponentAsset, type, LightType::Point);
    CHECK_MEMBER(LightComponentAsset, color, math::Vec3{0.5F});
    CHECK_MEMBER(LightComponentAsset, intensity, 2.0F);
    CHECK_MEMBER(LightComponentAsset, range, 20.0F);
    CHECK_MEMBER(LightComponentAsset, innerSpotAngle, 25.0F);
    CHECK_MEMBER(LightComponentAsset, outerSpotAngle, 35.0F);
    CHECK_MEMBER(LightComponentAsset, castShadow, true);
    CHECK_MEMBER(LightComponentAsset, cullingMask, 1U);
    CHECK_MEMBER(LightComponentAsset, enabled, false);
    CHECK_MEMBER(MaterialComponentAsset,
                 materials,
                 std::vector<VirtualPath>{VirtualPath{"assets://materials/test.material.json"}});
    CHECK_MEMBER(MaterialComponentAsset, enabled, false);
    CHECK_MEMBER(MeshComponentAsset, sourceType, MeshComponentSourceType::Primitive);
    CHECK_MEMBER(MeshComponentAsset, mesh, VirtualPath{"assets://meshes/test.mesh.json"});
    MeshBuildRecipe recipe;
    recipe.keepCpuCopy = true;
    CHECK_MEMBER(MeshComponentAsset, primitiveRecipe, recipe);
    CHECK_MEMBER(MeshComponentAsset, enabled, false);
    CHECK_MEMBER(MeshComponentAsset, visible, false);
    CHECK_MEMBER(MeshComponentAsset, castShadow, false);
    CHECK_MEMBER(MeshComponentAsset, receiveShadow, false);
    CHECK_MEMBER(MeshComponentAsset, layerMask, 2U);
    CHECK_MEMBER(SceneNodeAsset, id, 1U);
    CHECK_MEMBER(SceneNodeAsset, parent, 1U);
    CHECK_MEMBER(SceneNodeAsset, name, "changed");
    CHECK_MEMBER(SceneNodeAsset, active, false);
    CHECK_MEMBER(
        SceneNodeAsset, components, std::vector<SceneComponentAsset>{TransformComponentAsset{}});

#undef CHECK_MEMBER
    // 容器长度不变时，布局和节点也必须能识别内部成员的变化。
    VertexLayout layout;
    layout.bindings.push_back(VertexBinding{});
    VertexLayout changedLayout = layout;
    changedLayout.bindings[0].stride = 16;
    SceneNodeAsset node;
    node.components.push_back(CameraComponentAsset{});
    SceneNodeAsset changedNode = node;
    std::get<CameraComponentAsset>(changedNode.components[0]).enabled = false;
    return layout != changedLayout && node != changedNode;
}

// 根对象与命名字段必须产生相同内容，且不影响相邻字段。
template <typename T> bool checkObjectScopes(T source = {}) {
    using namespace engine;
    static_assert(std::derived_from<T, Transferable>);
    JsonWriter direct;
    Transferable& polymorphic = source;
    if (!polymorphic.transfer(direct) || !direct.valid())
        return false;
    const auto expected = nlohmann::json::parse(direct.toString());
    if (!expected.is_object() || expected.contains(""))
        return false;
    T decoded;
    JsonReader directReader{direct.toString()};
    if (!decoded.transfer(directReader) || !directReader.valid())
        return false;
    JsonWriter decodedWriter;
    if (!decoded.transfer(decodedWriter) ||
        nlohmann::json::parse(decodedWriter.toString()) != expected)
        return false;

    std::uint32_t before = 17, after = 29;
    JsonWriter nested;
    std::vector<T> items{source, source};
    std::vector<T> empty;
    std::optional<T> present{source}, absent;
    std::variant<std::uint32_t, T> variant{std::in_place_index<1>, source};
    if (!nested.beginObject({}) || !nested.transfer("before", before) ||
        !nested.transfer("child", polymorphic) || !nested.transfer("items", items) ||
        !nested.transfer("empty", empty) || !nested.transfer("present", present) ||
        !nested.transfer("absent", absent) || !nested.transfer("variant", variant) ||
        !nested.transfer("after", after) || !nested.endObject())
        return false;
    const auto tree = nlohmann::json::parse(nested.toString());
    if (tree.size() != 8 || tree.at("before") != 17 || tree.at("after") != 29 ||
        tree.at("child") != expected ||
        tree.at("items") != nlohmann::json::array({expected, expected}) ||
        tree.at("empty") != nlohmann::json::array() || tree.at("present").at("value") != expected ||
        tree.at("absent") != nlohmann::json{{"has_value", false}} ||
        tree.at("variant").at("type") != 1 || tree.at("variant").at("value") != expected)
        return false;
    JsonReader nestedReader{nested.toString()};
    items.clear();
    present.reset();
    absent.emplace();
    variant.template emplace<0>(0);
    before = after = 0;
    if (!nestedReader.beginObject({}) || !nestedReader.transfer("before", before) ||
        !nestedReader.transfer("child", decoded) || !nestedReader.transfer("items", items) ||
        !nestedReader.transfer("empty", empty) || !nestedReader.transfer("present", present) ||
        !nestedReader.transfer("absent", absent) || !nestedReader.transfer("variant", variant) ||
        !nestedReader.transfer("after", after) || !nestedReader.endObject() ||
        !nestedReader.valid() || before != 17 || after != 29 || items.size() != 2 ||
        !empty.empty() || !present || absent || variant.index() != 1)
        return false;
    for (T* value : {&decoded, &items[0], &items[1], &*present, &std::get<1>(variant)}) {
        JsonWriter writer;
        if (!value->transfer(writer) || nlohmann::json::parse(writer.toString()) != expected)
            return false;
    }

    BinaryWriter binary, namedBinary;
    if (!source.transfer(binary) || !namedBinary.transfer({}, source) ||
        binary.bytes() != namedBinary.bytes())
        return false;
    BinaryReader binaryReader{binary.bytes()};
    if (!decoded.transfer(binaryReader) || !binaryReader.finished())
        return false;
    JsonWriter fromBinary;
    if (!decoded.transfer(fromBinary) || nlohmann::json::parse(fromBinary.toString()) != expected)
        return false;
    for (const char* invalid : {"null", "[]", "42", "true"}) {
        JsonReader reader{invalid};
        if (decoded.transfer(reader) || reader.valid())
            return false;
    }
    return true;
}

bool testObjectScopes() {
    using namespace engine;
    ShaderPassAsset pass;
    pass.pass.program.vertexSource = VirtualPath{"assets://shaders/test.vert"};
    pass.pass.program.fragmentSource = VirtualPath{"assets://shaders/test.frag"};
    SubShaderDesc subShader;
    subShader.passes.push_back(pass);
    ShaderAsset shader;
    shader.name = "Test";
    shader.subShaders.push_back(subShader);
    MaterialAsset material;
    material.shader = VirtualPath{"assets://shaders/test.shader.json"};
    material.properties.emplace("Tint", math::Vec4{1.0F});
    TextureAsset texture;
    texture.desc = {
        TextureType::Texture2D, TextureFormat::Rgba8Srgb, TextureColorSpace::Srgb, 1, 1, 1};
    texture.mipData.emplace_back(1, 1, std::vector<std::byte>(4));
    MeshComponentAsset mesh;
    mesh.mesh = VirtualPath{"assets://meshes/test.mesh.json"};
    MeshBuildRecipe recipe;
    recipe.parts.emplace_back(PlaneGeometry{});
    auto meshAsset = MeshBuilder::buildAsset(recipe);
    if (!meshAsset || !checkObjectScopes(*meshAsset) || !checkObjectScopes<SceneAsset>())
        return false;
    return checkObjectScopes<Sample>() && checkObjectScopes<VertexSemantic>() &&
           checkObjectScopes<VertexBinding>() && checkObjectScopes<VertexAttribute>() &&
           checkObjectScopes<VertexLayout>() && checkObjectScopes<Aabb>() &&
           checkObjectScopes<BoundingSphere>() && checkObjectScopes<MeshBounds>() &&
           checkObjectScopes<SubMesh>() && checkObjectScopes<MeshDesc>() &&
           checkObjectScopes<MeshBuildRecipe>() && checkObjectScopes<VertexStream>() &&
           checkObjectScopes<MeshData>() && checkObjectScopes<PlaneGeometry>() &&
           checkObjectScopes<BoxGeometry>() && checkObjectScopes<UvSphereGeometry>() &&
           checkObjectScopes<CylinderGeometry>() && checkObjectScopes<MeshPrimitive>() &&
           checkObjectScopes<MeshPrimitivePart>() && checkObjectScopes<TransformComponentAsset>() &&
           checkObjectScopes<CameraComponentAsset>() && checkObjectScopes<LightComponentAsset>() &&
           checkObjectScopes<MaterialComponentAsset>() && checkObjectScopes(mesh) &&
           checkObjectScopes<SceneNodeAsset>() && checkObjectScopes<ShaderPropertyDesc>() &&
           checkObjectScopes<ShaderInterfaceVariable>() && checkObjectScopes<RenderStateDesc>() &&
           checkObjectScopes(pass) && checkObjectScopes(subShader) && checkObjectScopes(shader) &&
           checkObjectScopes(material) && checkObjectScopes(texture) &&
           checkObjectScopes<TextureDesc>() && checkObjectScopes<TextureMipData>() &&
           checkObjectScopes<WindowConfig>() && checkObjectScopes<RenderConfig>() &&
           checkObjectScopes<EngineConfig>();
}

bool testLegacyShapeAndFailureScope() {
    using namespace engine;
    VertexBinding binding{1, 16, VertexInputRate::Vertex};
    JsonWriter writer;
    if (!binding.transfer(writer) ||
        nlohmann::json::parse(writer.toString()) !=
            nlohmann::json{{"binding", 1}, {"stride", 16}, {"input_rate", 0}})
        return false;
    const std::vector<std::byte> legacy{std::byte{1},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{16},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0},
                                        std::byte{0}};
    BinaryWriter binary;
    if (!binding.transfer(binary) || binary.bytes() != legacy)
        return false;
    BinaryReader binaryReader{legacy};
    VertexBinding decoded;
    if (!decoded.transfer(binaryReader) || !binaryReader.finished() || decoded != binding)
        return false;

    // 深层失败后命名字段必须恢复父作用域，显式清错才允许继续。
    JsonReader reader{
        R"({"child":{"bindings":[{"binding":0,"stride":"bad","input_rate":0}]},"after":29})"};
    VertexLayout layout;
    if (!reader.beginObject({}) || reader.transfer("child", layout) || reader.valid())
        return false;
    reader.clearError();
    std::uint32_t after{};
    return reader.transfer("after", after) && after == 29 && reader.endObject();
}

} // namespace transfer_test

int main() {
    using namespace engine;
    using transfer_test::Sample;

    if (!transfer_test::testDerivedEquality())
        return 7;
    if (!transfer_test::testObjectScopes())
        return 8;
    if (!transfer_test::testLegacyShapeAndFailureScope())
        return 9;

    Sample source;
    BinaryWriter binaryWriter;
    if (!binaryWriter.transfer("root", source) || !binaryWriter.valid())
        return 1;
    const std::vector<std::byte> binary = binaryWriter.takeBytes();
    Sample binaryResult;
    BinaryReader binaryReader{binary};
    if (!binaryReader.transfer("root", binaryResult) || !binaryReader.finished() ||
        !transfer_test::equal(source, binaryResult))
        return 2;

    JsonWriter jsonWriter;
    if (!jsonWriter.transfer("root", source) || !jsonWriter.valid())
        return 3;
    const std::string json = jsonWriter.toString();
    Sample jsonResult;
    JsonReader jsonReader{json};
    if (!jsonReader.transfer("root", jsonResult) || !jsonReader.valid() ||
        !transfer_test::equal(source, jsonResult))
        return 4;

    JsonReader malformed{"{not-json"};
    Sample ignored;
    if (malformed.valid() || malformed.transfer("root", ignored))
        return 5;

    std::vector<std::byte> truncated = binary;
    truncated.pop_back();
    BinaryReader truncatedReader{truncated};
    if (truncatedReader.transfer("root", ignored) || truncatedReader.valid()) {
        return 6;
    }
    return 0;
}
