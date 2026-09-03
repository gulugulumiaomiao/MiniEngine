#pragma once

#include "renderer/Material.h"
#include "renderer/RenderResources.h"

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace engine {

class IRenderBackend;
class RenderScene;
struct MeshData;
struct MeshDesc;
class Window;

class Renderer final {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] MeshHandle createMesh(const MeshDesc& desc, const MeshData& data);
    [[nodiscard]] MaterialHandle loadMaterial(
        const std::filesystem::path& materialPath);
    void destroyMesh(MeshHandle handle);
    void destroyMaterial(MaterialHandle handle);
    void setMaterialFloat(MaterialHandle handle, std::string_view name, float value);
    void setMaterialVec2(MaterialHandle handle, std::string_view name,
                         const math::Vec2& value);
    void setMaterialVec3(MaterialHandle handle, std::string_view name,
                         const math::Vec3& value);
    void setMaterialVec4(MaterialHandle handle, std::string_view name,
                         const math::Vec4& value);
    void setMaterialBool(MaterialHandle handle, std::string_view name, bool value);
    void setMaterialTexture(MaterialHandle handle, std::string_view name,
                            std::string value);
    void setMaterialShader(MaterialHandle handle,
                           const std::filesystem::path& shaderPath);
    void renderFrame(const RenderScene& scene);
    void waitIdle();

private:
    std::unique_ptr<IRenderBackend> backend_;
};

} // namespace engine
