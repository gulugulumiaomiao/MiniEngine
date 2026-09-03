# Shader 与材质运行时管线

本文记录 Shader/Material 重构后的第一版完整框架。目标是把“资产描述、离线编译、运行时程序、RHI 对象、Pipeline、材质 GPU 数据”分层，避免上层直接操作 Vulkan 或以文件路径作为运行时缓存键。

## 总体数据流

```text
ShaderLab JSON / GLSL
        │
        ▼
ShaderAsset ── ShaderCompileRequest ── PreprocessedShader
        │                                  │
        │                                  ▼
        │                           CompiledShader + SPIRV-Cross Reflection
        │                                  │ CompileID
        ▼                                  ▼
Shader → SubShader → ShaderPass ───── ShaderProgram / ProgramLayout
                                               │ ProgramID + LayoutID
                                               ▼
                                      RHIShaderHandle / PipelineCache
                                               │
Material → MaterialGpuCache → set 1 ───────────┤
Scene/Object buffer → set 0 ───────────────────┘
```

## 十个步骤的实现结果

### 1. 资产和运行时对象分离

- `ShaderAsset`、`MaterialAsset` 只表示磁盘资产和导入结果，由 `AssetManager` 按路径加载与缓存。
- `Shader`、`SubShader`、`ShaderPass` 是运行时对象。最终参与绘制的是 `ShaderPass`。
- `Material` 持有 `shared_ptr<Shader>`，不持有 `ShaderAsset`，可以在运行时调用 `setShader` 切换 Shader 并迁移同名兼容属性。
- `SubShader` 和 `ShaderPass` 不保留对应的 Desc。

### 2. 明确编译输入、预处理结果和编译产物

`ShaderCompiler.h` 定义了：

- `ShaderVariantKey`：关键字位、Mesh 特征位、平台特征位。
- `ShaderCompileRequest`：源文件、阶段、入口、宏、include 路径、目标 API、编译器版本和选项。
- `PreprocessedShader`：展开 include 和 define 后的源码、依赖文件及源码 Hash。
- `CompiledShader`：SPIR-V 字节码、入口、阶段、反射结果和依赖列表。

预处理器支持递归 `#include "..."`、循环依赖检查，并把宏插入 `#version` 之后。

### 3. Reflection 归属于 CompiledShader

SPIRV-Cross 的反射结果保存在 `CompiledShader::reflection`，内容包括 descriptor、uniform member offset、阶段输入和阶段输出。JSON 声明校验仍在离线构建阶段执行，运行时直接消费已验证的布局。

### 4. CompiledShaderCache 使用 CompileID

缓存键由 SPIR-V 内容、阶段、入口和 `ShaderVariantKey` 共同计算，不再使用文件路径作为对象身份。路径只负责加载和依赖追踪。同一内容可以复用，内容变化会产生新的 CompileID，旧 handle 通过 generation 失效。

### 5. ShaderProgram 与 ProgramCache

`ShaderProgram` 组合一个 Pass 的顶点和片元 `CompiledShader`，并合并两个阶段的 descriptor/interface：

- `ProgramID` 标识阶段组合和 Variant。
- `ShaderProgramLayout::id` 标识合并后的资源布局。
- 同 set/binding 类型不一致时记录 `error`，Program 创建返回无效 Handle；Pipeline 创建随之失败并由 Renderer 跳过该 Pass。

### 6. RHI Shader 对象

`RhiShaderCache` 把 `CompiledShaderHandle` 转成 `rhi::ShaderHandle`，内部创建并缓存 `VkShaderModule`。`ShaderModule` 只接收 SPIR-V 字节，不再打开文件，因此 RHI 不依赖资产路径和文件系统。

### 7. 结构化 PipelineCache

Pipeline key 包含：

- ProgramID 和 LayoutID；
- VertexLayout；
- RenderState；
- RenderTarget color format。

`GraphicsPipeline` 接收 RHI Shader module 和两个 descriptor set layout，不再临时读取 `.spv`。Swapchain 重建时只清理受格式影响的 Pipeline，编译产物和 Shader module 仍可复用。

### 8. MaterialGpuCache

固定的 descriptor 约定为：

- `set = 0, binding = 0`：对象 transform storage buffer；
- `set = 1, binding = 0`：材质 uniform buffer；
- `set = 1, binding = 1..16`：按 Shader properties 声明顺序排列的 Texture2D。

每个 in-flight frame 有独立的材质 uniform buffer 和 descriptor set。`Material::version()` 负责标识 CPU 数据版本；同一帧内相同 `MaterialHandle` 只准备一次。纹理通过 `MaterialGpuCache::TextureResolver` 解耦 TextureAsset/GPU image 的加载，未安装 resolver 或纹理未就绪时只输出 warn。

### 9. Keyword、Variant 与多 Pass

`ShaderKeywordSchema` 为每个 Pass 建立稳定的关键字位表，材质的 keyword 列表转换为 `ShaderVariantKey` 后参与 CompiledShader、Program 和 Pipeline 缓存。

Renderer 按以下顺序提交存在的 Pass：

```text
ShadowCaster → DepthOnly → Forward
```

某个 SubShader 没有对应 Pass 时直接跳过。当前示例只声明 Forward，因此行为与原三角形示例一致；添加另外两个 Pass 后不需要改变场景提交接口。

### 10. 依赖、热重载、延迟销毁和 Cooked 元数据

- `ShaderDependencyGraph` 保存 include/SPIR-V 文件到 CompileID 的反向依赖。
- 每帧首次请求 Pipeline 时轮询已加载依赖的时间戳。
- 变化按 `CompiledShader → ShaderProgram → RHI Shader → Pipeline` 传播失效。
- 已提交给 GPU 的旧 `VkPipeline` 和 `VkShaderModule` 进入 retirement 队列，至少经过 frames-in-flight 后才析构。
- `ShaderCookedAsset` 保存 properties、`UniformBlockLayout`、Pass render state、Variant、CompileID 和 LayoutID，作为以后打包工具输出二进制资产的稳定运行时模型。

## 关键缓存键

| 缓存 | 键 | 值 |
|---|---|---|
| AssetManager | 规范化资产路径 + 资产类型 | ShaderAsset / MaterialAsset / MeshAsset |
| CompiledShaderCache | SPIR-V 内容 + stage + entry + Variant | CompiledShader |
| ShaderProgramCache | vertex CompileID + fragment CompileID + Variant | ShaderProgram |
| RhiShaderCache | CompileID | VkShaderModule |
| PipelineCache | ProgramID + LayoutID + VertexLayout + RenderState + RT format | VkPipeline |
| MaterialGpuCache | frame + MaterialHandle + material version | uniform buffer + descriptor set |

## 生命周期和修改规则

1. 编辑 JSON/GLSL 后先由 CMake 的 ShaderLab target 重新生成和编译 SPIR-V。
2. Debug 运行时检测 SPIR-V 时间戳变化，并在下一次 Pipeline 请求时切换到新对象。
3. 材质属性变化只使材质 GPU 数据变脏，不重新创建 Pipeline。
4. Keyword、VertexLayout、RenderState 或 RenderTarget 格式变化会选择或创建另一条 Pipeline。
5. Shader 切换时材质保留名称和类型兼容的属性，布局变化由新的 Material GPU buffer 承接。

## 主要源码位置

- `src/renderer/Shader.h/.cpp`：资产描述、运行时 Shader/SubShader/Pass、Reflection。
- `src/renderer/ShaderCompiler.h/.cpp`：预处理、CompiledShader、Program、依赖图和 Cooked 模型。
- `src/rhi/vulkan/RhiShaderCache.h/.cpp`：CompileID 到 Vulkan Shader module。
- `src/rhi/vulkan/PipelineCache.h/.cpp`：结构化 Pipeline 缓存。
- `src/rhi/vulkan/MaterialGpuCache.h/.cpp`：每帧材质 GPU 数据与 descriptor。
- `src/renderer/Renderer.cpp`：Variant 选择与多 Pass 场景提交。
- `src/rhi/vulkan/VulkanBackend.cpp`：缓存组装、绑定和热重载边界。
