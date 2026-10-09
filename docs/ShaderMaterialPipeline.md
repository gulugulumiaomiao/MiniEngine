# Shader 与 Material 运行时管线

编译及打包见 [ShaderCompilePipeline.md](ShaderCompilePipeline.md)，材质生命周期和反射映射见 [MaterialSystem.md](MaterialSystem.md)。

```text
ShaderAssetData -> ShaderAsset -> ShaderCreateInfo -> Shader
MaterialAssetData -> MaterialAsset -> MaterialCreateInfo -> Material
Material -> Shader -> SubShader -> ShaderPass.keyword
  -> uint64 keyword mask -> ShaderPass.programs[mask]
  -> GraphicsPipelineStorage -> ShaderCompilePipeline -> ShaderProgram
DrawListBuilder -> GraphicsPipelineStorage.resolve -> Pipeline
                -> Material.uniformSet(program, frame) -> set 1
                -> FrameGpuManager.retain -> 提交帧保活
```

资产单向持有运行时引用，Material 不经过领域 Manager。ShaderPass 保存 keyword 和直接的 unordered_map<uint64_t, Ref<ShaderProgram>>，编译源由 Shader 保存。Material 的 enableKeyword/disableKeyword 决定 Program，Pass 未声明的关键词忽略。

ShaderProgram 保存阶段模块、每个 set 的实际反射 binding、UBO 成员布局、共享 descriptor layout 和 pipeline layout。共享布局 hash 不含成员名称或 UBO 偏移，Material 按 ProgramId 和帧分别缓存，上传以该 Program 反射为准。

set 0 由 FrameGpuManager 管理场景/对象/实例表/阴影资源，set 1 由 Material 创建，set 2 由 GlobalUniformGpuManager 管理。反射没有材质资源时 set 1 合法为空，Forward 绑定失败回退到 Material::errorMaterial。

MaterialAsset 原实例重载完整重建，ShaderAsset 原实例重载增加 revision，Material 使用时迁移兼容属性。GraphicsPipelineStorage 负责程序和 Pipeline 退役，FrameGpuManager 保活通用提交资源，不持有材质索引。
