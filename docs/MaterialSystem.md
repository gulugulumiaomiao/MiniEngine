# 运行时 Material 系统

Material 是纯运行时对象，不直接依赖 RHI。它保存属性、Keyword、RenderQueue 和 `ShaderHandle`；Vulkan Buffer、Descriptor 和 Pipeline 由 GPU 资源层管理。

## 数据结构

```cpp
class Material {
    ShaderHandle shaderHandle_;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    std::unordered_map<std::string, std::string> textures;
};
```

`Material::shader()` 通过 `SHADER_MANAGER` 解析 Handle。数值属性以 `UniformBlockLayout + uniformData` 作为唯一运行时数据源，Texture2D 保存规范化的虚拟路径。成功修改属性会设置 dirty 并增加 version，GPU 上传完成后调用 `markClean()`。

## 加载流程

```text
Material 资产路径
  -> MATERIAL_MANAGER.load
  -> ASSET_MANAGER.loadAsset<MaterialAsset>
  -> SHADER_MANAGER.load(materialAsset.shader)
  -> MaterialAsset::instantiate(ShaderHandle)
  -> KeyedHandleRegistry::insert(Material)
  -> MaterialHandle(index, generation)
```

MaterialManager 继承 KeyedHandleRegistry，由其统一管理 HandlePool、路径到 Handle 的索引、generation 和空闲槽位。MaterialManager 不创建 Pipeline、DescriptorSet 或 GPU Buffer。

## Shader 切换

`MaterialManager::setShader(handle, path)` 加载目标 Shader 后重建 Material：

1. 保存旧 Shader 中的当前属性值。
2. 使用新 Shader 默认值创建 uniform 和 texture 数据。
3. 恢复同名且类型兼容的旧值。
4. Float/Range、Vec4/Color 分别视为兼容类型。
5. 丢弃新 Shader 未声明的 Keyword。
6. 保留 Material 的 RenderQueue override，否则采用新 Shader 默认队列。
7. 设置 dirty 并增加 version。

Shader 热重载使用相同 Handle 并增加 Shader revision。MaterialManager 会刷新所有引用该 Handle 的材质，因此布局变化不要求游戏对象更新 MaterialHandle。

## Renderer 与 GPU 资源

Renderer 每帧从 Material 选择 `Shader -> SubShader -> ShaderPass`，生成 DrawItem 并按 RenderQueue 排序。`MaterialGpuManager` 管理每帧 Material BindGroup，`GraphicsPipelineManager` 提供对应的 RHI Pipeline；Render 层不接触 Vulkan 类型。

## 内置错误材质

`MiniEngine/BuiltinColor` 是最小兜底 Shader，只声明一个 `Color` uniform。顶点阶段仅执行对象与相机变换，片元阶段直接输出该颜色。

`asset://materials/error.material.json` 使用该 Shader，并把 `Color` 设置为洋红色 `(1, 0, 1, 1)`。`MaterialManager::errorMaterial()` 延迟加载并复用这个材质。

回退规则：

- Material 资产或其 Shader 加载失败时，`MaterialManager::load()` 返回 Error Material Handle。
- Shader 编译或 Graphics Pipeline 创建失败时，Renderer 的 Forward Pass 改用 Error Material。
- Material BindGroup 准备失败时，提交前切换为 Error Material 的 Pipeline 和 BindGroup。
- Error Material 自身不可用时才跳过 DrawItem，避免递归回退。

完整资产导入和热重载流程见 [AssetPipeline.md](AssetPipeline.md)。
