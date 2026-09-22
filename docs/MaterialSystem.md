# 运行时 Material 系统

Material 是纯运行时对象，不直接依赖 RHI。它保存属性、Keyword、RenderQueue 和 `ShaderHandle`；Vulkan Buffer、Descriptor 和 Pipeline 由 GPU 资源层管理。

## 数据结构

```cpp
class Material {
    ShaderHandle shaderHandle_;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    // Asset/reference representation (serialized): property -> VirtualPath string.
    std::unordered_map<std::string, std::string> textures;
    // Runtime override (not serialized): property -> TextureView + Sampler.
    std::unordered_map<std::string, TextureBinding> textureBindings_;
};
```

`Material::shader()` 通过 `SHADER_MANAGER` 解析 Handle。数值属性以 `UniformBlockLayout + uniformData` 作为唯一运行时数据源。Texture2D 的资产表示保存规范化虚拟路径；运行时可以覆盖为 `TextureBinding { TextureView, Sampler }`，但不会把 GPU handle 写回资产。成功修改属性会设置 dirty 并增加 version。

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

## TextureView + Sampler 绑定

Shader 采样槽的规范输入是 `TextureView + Sampler`，不是 Texture。Material 提供：

```cpp
setTexture(name, const TextureView& view, const Sampler& sampler); // 主接口
setTexture(name, const Texture& texture, const Sampler& sampler); // 转发到 defaultView
```

同一个 TextureView 可以搭配不同 Sampler；Texture 和 TextureView 都不拥有 sampler。指定 mip、layer、format 或 swizzle 时传自定义 TextureView，普通路径使用 Texture 的默认 view。

`MaterialGpuManager` 每次 resolve 都收集最终 binding：优先使用运行时 override，否则由 VirtualPath 加载 GPU-backed Texture，并采用资产的默认 sampler 建议。绑定签名变化（包括 texture 热重载后 default view handle 变化）会重建 BindGroup。`MaterialGpuFactory` 只向 RHI 提交 `TextureViewHandle + SamplerHandle`，TextureHandle 从不直接写入 sampled descriptor。

## Renderer 与 GPU 资源

Renderer 每帧从 Material 选择 `Shader -> SubShader -> ShaderPass`，生成 DrawItem 并按 RenderQueue 排序。`MaterialGpuManager` 管理每帧 Material BindGroup，`GraphicsPipelineManager` 提供对应的 RHI Pipeline；Render 层不接触 Vulkan 类型。

## 内置错误材质

`MiniEngine/BuiltinColor` 是最小兜底 Shader，只声明一个 `Color` uniform。顶点阶段仅执行对象与相机变换，片元阶段直接输出该颜色。

`assets://materials/error.material.json` 使用该 Shader，并把 `Color` 设置为洋红色 `(1, 0, 1, 1)`。`MaterialManager::errorMaterial()` 延迟加载并复用这个材质。

回退规则：

- Material 资产或其 Shader 加载失败时，`MaterialManager::load()` 返回 Error Material Handle。
- Shader 编译或 Graphics Pipeline 创建失败时，Renderer 的 Forward Pass 改用 Error Material。
- Material BindGroup 准备失败时，提交前切换为 Error Material 的 Pipeline 和 BindGroup。
- Error Material 自身不可用时才跳过 DrawItem，避免递归回退。

完整资产导入和热重载流程见 [asset/Pipeline.md](asset/Pipeline.md)。
