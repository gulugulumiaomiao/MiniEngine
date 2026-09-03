# Shader 运行时结构

资产与运行时分为两层：AssetManager 缓存 `ShaderAsset`，运行时由它创建 `Shader`。`Shader` 会复制绘制所需的路径、Properties 和 SubShader/Pass，不持有 `ShaderAsset` 的指针或引用。

运行时结构：

```text
Shader
  └─ SubShader[]
       └─ ShaderPass[]
```

- `Shader` 持有复制后的资产路径、Properties 的 `UniformBlockLayout` 和所有 SubShader。
- `SubShader` 从 `SubShaderDesc` 复制渲染管线、RenderType、RenderQueue 和运行时 Pass，不保存 Desc。
- `ShaderPass` 是最终参与绘制的对象，从 `ShaderPassDesc` 复制程序、接口和 Feature，并从 `ShaderPassAsset` 单独复制 RenderState；它不保存任何 Desc。

当前 Renderer 使用 `MiniForward` 选择 SubShader，并根据 `RenderPhase::Forward` 选择 `ShaderPassType::Forward`。选中的 `ShaderPass` 作为帧内稳定引用写入 `DrawItem`；Material 持有运行时 Shader，且 DrawList 在 `renderFrame` 内同步消费，因此引用在本帧有效。

Shader 资产加载阶段只读取 JSON 和缓存 `ShaderAsset`，不会生成 GLSL、编译 SPIR-V 或创建 Vulkan 对象。第一次为实际绘制请求某个 Pass/Variant 的 Pipeline 时，`ShaderProgramCache` 才执行预处理、ShaderGenerator、Variant define 注入、`glslc` 编译、SPIRV-Cross Reflection 校验，随后创建 ShaderModule 和 Pipeline。生成结果按处理后源码哈希缓存于 `shader://runtime/`。

Vulkan 后端已经接入第一版 `PipelineCache`。Renderer 为每个 DrawItem 选出 ShaderPass 后，通过 `pipelineForPass` 获取缓存 Pipeline；缓存键包含 Pass 程序与 RenderState、当前 VertexLayout 和颜色 RenderTarget 格式。Swapchain 格式变化时会清空缓存并递增句柄 generation。

Keyword/Variant 尚未加入缓存键，Mesh 也仍限制为一个活动 VertexLayout；这两项将在 Variant 和多布局 Mesh 阶段扩展。
