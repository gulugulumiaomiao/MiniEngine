#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/shader/ShaderManager.h"
#include "asset/manager/AssetManager.h"
#include "asset/importer/FileWatcher.h"
#include "TestAssetEnvironment.h"

#include <cstring>
#include <filesystem>

int main() {
    using namespace engine;
    const std::filesystem::path fixtureCopy =
        std::filesystem::temp_directory_path() / "MiniEngineMaterialFixtures" / "assets";
    std::error_code fixtureError;
    std::filesystem::remove_all(fixtureCopy.parent_path(), fixtureError);
    std::filesystem::create_directories(fixtureCopy.parent_path(), fixtureError);
    std::filesystem::copy(MINI_TEST_MATERIAL_FIXTURE_DIR,
                          fixtureCopy,
                          std::filesystem::copy_options::recursive,
                          fixtureError);
    if (fixtureError)
        return 21;
    AssetManager& assets = ASSET_MANAGER;
    MaterialManager& materials = MATERIAL_MANAGER;
    materials.clear();
    if (!test::initializeAssetEnvironment(MINI_TEST_ASSET_DIR))
        return 22;
    const auto cachedShaderA =
        assets.loadAsset<ShaderAsset>(VirtualPath{"asset://shaders/vertex_color.shader.json"});
    const auto cachedShaderB = assets.loadAsset<ShaderAsset>(
        VirtualPath{"asset://shaders/../shaders/vertex_color.shader.json"});
    const auto cachedMaterialA = assets.loadAsset<MaterialAsset>(
        VirtualPath{"asset://materials/warm_vertex_color.material.json"});
    const auto cachedMaterialB = assets.loadAsset<MaterialAsset>(
        VirtualPath{"asset://materials/./warm_vertex_color.material.json"});
    if (cachedShaderA != cachedShaderB || cachedMaterialA != cachedMaterialB ||
        cachedMaterialA->shader.string() != "asset://shaders/vertex_color.shader.json") {
        return 17;
    }
    const MaterialHandle warm = materials.load(cachedMaterialA->assetPath());
    const MaterialHandle coolShared =
        materials.load(VirtualPath{"asset://materials/cool_vertex_color.material.json"});
    Material& warmData = *materials.find(warm);
    if (materials.load(cachedMaterialA->assetPath()) != warm ||
        materials.find(cachedMaterialA->assetPath()) != &warmData || materials.size() != 2) {
        return 24;
    }
    const SubShader* runtimeSubShader = warmData.shader().selectSubShader("MiniForward");
    if (!runtimeSubShader || !runtimeSubShader->findPass(ShaderPassType::Forward)) {
        return 16;
    }
    if (warmData.shaderHandle() != materials.find(coolShared)->shaderHandle() ||
        warmData.shader().assetPath() != materials.find(coolShared)->shader().assetPath()) {
        return 13;
    }
    const math::Vec4 color = warmData.getVec4("BaseColor");
    if (color.x != 1.0F || color.y != 0.55F || warmData.renderQueue != 2000 ||
        warmData.uniformData.size() != 64 || !warmData.dirty() || warmData.version() != 1) {
        return 1;
    }
    if (warmData.getVec2("UvScale") != math::Vec2{1.0F, 1.0F}) {
        return 2;
    }
    warmData.markClean();
    warmData.setVec4("BaseColor", {0.1F, 0.2F, 0.3F, 1.0F});
    if (!warmData.dirty() || warmData.version() != 2 || warmData.getVec4("BaseColor").z != 0.3F) {
        return 3;
    }
    materials.destroy(warm);
    if (materials.find(warm) != nullptr) {
        return 19;
    }
    materials.destroy(coolShared);
    const MaterialHandle reused =
        materials.load(VirtualPath{"asset://materials/cool_vertex_color.material.json"});
    if (reused.index != coolShared.index || reused.generation == coolShared.generation) {
        return 10;
    }
    if (materials.find(reused) == nullptr) {
        return 20;
    }
    materials.clear();
    if (materials.find(cachedMaterialA->assetPath()) || materials.size() != 0) {
        return 25;
    }
    FILE_WATCHER.stop();
    if (!test::initializeAssetEnvironment(fixtureCopy))
        return 23;
    MaterialManager& valueMaterials = MATERIAL_MANAGER;
    const MaterialHandle invalid =
        valueMaterials.load(VirtualPath{"asset://material_invalid.material.json"});
    if (invalid) {
        return 14;
    }
    const MaterialHandle values =
        valueMaterials.load(VirtualPath{"asset://material_values.material.json"});
    Material& valueData = *valueMaterials.find(values);
    if (valueData.getFloat("FloatValue") != 2.5F || valueData.getFloat("RangeValue") != 0.25F ||
        !valueData.getBool("Enabled") || valueData.getVec2("Uv") != math::Vec2{1.0F, 2.0F} ||
        valueData.getVec3("Direction") != math::Vec3{4.0F, 5.0F, 6.0F} ||
        valueData.getVec4("Params") != math::Vec4{1.0F, 2.0F, 3.0F, 4.0F} ||
        valueData.getVec4("Tint") != math::Vec4{0.25F, 0.5F, 0.75F, 1.0F} ||
        valueData.getTexture("MainTexture") != "asset://textures/black.png") {
        return 5;
    }
    valueData.markClean();
    const std::uint64_t versionBeforeRejectedSet = valueData.version();
    valueData.setFloat("Uv", 9.0F);
    valueData.setFloat("MissingProperty", 9.0F);
    valueData.setTexture("MissingTexture", "invalid");
    if (valueData.dirty() || valueData.version() != versionBeforeRejectedSet ||
        valueData.getVec2("Uv") != math::Vec2{1.0F, 2.0F}) {
        return 15;
    }
    const UniformMemberLayout& direction = valueData.uniformLayout.requireMember("Direction");
    const auto bytes = valueData.uniformBytes();
    if (direction.size != 16 || bytes[direction.offset + 12] != std::byte{0} ||
        bytes[direction.offset + 13] != std::byte{0} ||
        bytes[direction.offset + 14] != std::byte{0} ||
        bytes[direction.offset + 15] != std::byte{0}) {
        return 6;
    }
    std::uint32_t encodedBool = 0;
    const UniformMemberLayout& enabled = valueData.uniformLayout.requireMember("Enabled");
    std::memcpy(&encodedBool, bytes.data() + enabled.offset, sizeof(encodedBool));
    if (encodedBool != 1U) {
        return 7;
    }
    valueData.setTexture("MainTexture", "gray");
    valueData.setBool("Enabled", false);
    valueData.setFloat("RangeValue", 0.75F);
    valueData.setVec2("Uv", {3.0F, 4.0F});
    valueData.setVec3("Direction", {7.0F, 8.0F, 9.0F});
    valueData.setVec4("Params", {5.0F, 6.0F, 7.0F, 8.0F});
    if (valueData.getTexture("MainTexture") != "gray" || valueData.getBool("Enabled") ||
        valueData.getFloat("RangeValue") != 0.75F ||
        valueData.getVec2("Uv") != math::Vec2{3.0F, 4.0F} ||
        valueData.getVec3("Direction") != math::Vec3{7.0F, 8.0F, 9.0F} ||
        valueData.getVec4("Params") != math::Vec4{5.0F, 6.0F, 7.0F, 8.0F}) {
        return 8;
    }

    const std::uint64_t versionBeforeShaderSwitch = valueData.version();
    valueMaterials.setShader(values, VirtualPath{"asset://material_switch.shader.json"});
    if (valueData.shader().name() != "Tests/MaterialSwitch" ||
        valueData.getFloat("RangeValue") != 0.75F ||
        valueData.getVec3("Direction") != math::Vec3{7.0F, 8.0F, 9.0F} ||
        valueData.getVec2("NewUv") != math::Vec2{0.5F, 0.5F} ||
        valueData.getTexture("MainTexture") != "gray" ||
        valueData.version() != versionBeforeShaderSwitch + 1 || !valueData.dirty()) {
        return 9;
    }
    MATERIAL_MANAGER.clear();
    SHADER_MANAGER.clear();
    test::shutdownAssetEnvironment();
}
