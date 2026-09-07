#pragma once

#include "render/material/Material.h"
#include "render/renderer/RenderResources.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace engine {

class IRenderBackend;
class RenderScene;
struct MeshData;
struct MeshDesc;
struct MeshBuildRecipe;
class Window;

class Renderer final {
  public:
    Renderer(Window& window, bool vsync);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] MeshHandle createMesh(const MeshDesc& desc,
                                        const MeshData& data);
    [[nodiscard]] MeshHandle
    createProceduralMesh(const MeshBuildRecipe& recipe);
    [[nodiscard]] MeshHandle loadMesh(const VirtualPath& meshPath);
    [[nodiscard]] MaterialHandle loadMaterial(const VirtualPath& materialPath);
    void destroyMesh(MeshHandle handle);
    void destroyMaterial(MaterialHandle handle);
    void setMaterialFloat(MaterialHandle handle, std::string_view name,
                          float value);
    void setMaterialVec2(MaterialHandle handle, std::string_view name,
                         const math::Vec2& value);
    void setMaterialVec3(MaterialHandle handle, std::string_view name,
                         const math::Vec3& value);
    void setMaterialVec4(MaterialHandle handle, std::string_view name,
                         const math::Vec4& value);
    void setMaterialBool(MaterialHandle handle, std::string_view name,
                         bool value);
    void setMaterialTexture(MaterialHandle handle, std::string_view name,
                            std::string value);
    void setMaterialShader(MaterialHandle handle,
                           const VirtualPath& shaderPath);
    void renderFrame(const RenderScene& scene);
    void waitIdle();

  private:
    std::unique_ptr<IRenderBackend> backend_;
};

} // namespace engine
