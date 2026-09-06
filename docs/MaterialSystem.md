# 引擎层 Material 系统

Material 是引擎运行时对象，不属于 RHI。它保存属性、Keyword、RenderQueue 和 `ShaderHandle`；Vulkan Buffer、Descriptor 与 Pipeline 仍由后端缓存管理。

## 数据结构

```cpp
class Material {
    ShaderHandle shaderHandle_;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    std::unordered_map<std::string, std::string> textures;
};
```

`Material::shader()` 通过 `SHADER_MANAGER` 解析 Handle。Material 不持有 `ShaderAsset`，也不持有 `shared_ptr<Shader>`。

数值属性以 `UniformBlockLayout + uniformData` 作为唯一运行时数据源；Texture2D 暂存为独立字符串引用。类型安全接口包括 Float、Vec2、Vec3、Vec4、Bool 和 Texture 的 getter/setter。属性不存在或类型不匹配时只记录 warn，不修改数据。

成功修改会设置 dirty 并增加 version；GPU 上传完成后调用 `markClean()`。Bool 按 32 位 `0/1` 编码，Vec3 的 std140 padding 会清零。

## 创建流程

```text
Material 虚拟路径
  -> MATERIAL_MANAGER.load
  -> ASSET_MANAGER.loadAsset<MaterialAsset>
  -> SHADER_MANAGER.load(materialAsset.shader)
  -> MaterialAsset::instantiate(ShaderHandle)
  -> InstanceManager::insert(Material)
  -> MaterialHandle(index, generation)
```

MaterialManager 继承 InstanceManager，后者统一管理 HandlePool、虚拟路径索引、Slot、generation 和 free list。销毁后旧 Handle 失效，Slot 可安全复用。它不创建 VkPipeline、DescriptorSet 或 GPU Buffer。

## Shader 切换

`MaterialManager::setShader(handle, path)` 加载或复用目标 ShaderHandle，再让 Material 重建布局：

1. 保存旧 Shader 中的当前属性值。
2. 使用新 Shader 默认值创建新的 uniform/texture 数据。
3. 恢复同名且类型兼容的旧值。
4. Float/Range、Vec4/Color 分别视为兼容类型。
5. 丢弃新 Shader 未声明的 Keyword。
6. 保留 Material 的 RenderQueue override，否则采用新 Shader 默认队列。
7. dirty 置位，version 增加一次。

Shader 热重载使用相同 Handle、增加 Shader revision；MaterialManager 会刷新所有引用该 Handle 的材质，因此布局变化不要求游戏对象更换 MaterialHandle。

## Renderer 与 RHI

Renderer 每帧解析 MeshHandle 和 MaterialHandle，从 Material 选择 `Shader -> SubShader -> ShaderPass`，生成 DrawItem 并按 RenderQueue 排序。VulkanBackend 根据 Material version 更新 `MaterialGpuCache`，并通过 PipelineCache 获取 Pass 对应的 GPU Pipeline。

完整资产导入和热重载流程见 [AssetPipeline.md](AssetPipeline.md)。
