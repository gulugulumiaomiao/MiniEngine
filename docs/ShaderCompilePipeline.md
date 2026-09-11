# Shader 加载与编译流程

Shader 保留资产与运行时两层：`ShaderAsset` 负责 JSON、Artifact 和资产身份，
`ShaderAsset::instantiate()` 生成供渲染使用的 `Shader`。编译链从 `Shader` 开始，
不持有也不接收 `ShaderAsset`。

```text
shader.json -> ShaderAsset -> Shader -> ShaderCompilePipeline
                                      |- DevelopmentRuntime: generate -> preprocess -> glslc
                                      |- OfflineTool:       generate -> preprocess -> glslc -> packaged SPV
                                      `- PackagedRuntime:   resolve path -> load packaged SPV
```

## 阶段边界

四个阶段类分别位于同名的 `.h/.cpp` 文件中；阶段相关的配置、请求和结果类型与其
所有者放在同一个头文件中。

- `ShaderGenerator` 根据运行时 `Shader` 和 `ShaderPass` 生成完整阶段 GLSL。
- `ShaderPreprocessor` 注入排序后的 define、展开递归 include、检测循环并输出
  `PreprocessedShader`。它不接触 `ShaderAsset`，也不调用编译器。
- `ShaderCompiler` 只接收 `PreprocessedShader` 和 `ShaderCompilerOptions`，调用 glslc
  并输出 SPIR-V。
- `ShaderCompilePipeline` 选择运行模式，并负责源码生成、阶段编排、SPIR-V
  加载与反射、Program 布局合并、离线产物写入以及失效传播。
- `CompiledShaderCache` 和 `ShaderProgramCache` 只负责查找、插入、句柄解析和
  失效。它们不读取文件，也不执行生成、预处理、编译、反射或 Program 组装。

源码生成、预处理和 SPIR-V 编译分别有内容缓存；编译后 Shader 与 Program 使用
专用对象缓存。命中时直接复用缓存对象，未命中后的业务流程由
`ShaderCompilePipeline` 继续执行。依赖关系只保存在 `FileDependencyGraph` 中。

## Include 搜索路径

`ShaderPreprocessorConfig::includeSearchPaths` 接受有序的 `VirtualPath` 目录。目录的
scheme 必须已经挂载到 `FileSystem`，并且目录必须存在。预处理器不会直接访问物理
路径，也不会自行创建 mount。

```cpp
ShaderCompilePipelineConfig config;
config.preprocessorConfig.includeSearchPaths = {
    VirtualPath{"asset://shaders/include"},
    VirtualPath{"engine-shader://include"},
};
```

搜索规则：

1. `#include "file.glsl"` 先搜索当前文件目录，再按配置顺序搜索 include 目录。
2. `#include <file.glsl>` 只搜索配置的 include 目录。
3. `#include "scheme://path/file.glsl"` 只访问指定虚拟路径。

预处理结果记录根阶段文件和所有递归 include。搜索目录及顺序参与缓存键。

## 唯一依赖图

`FileDependencyGraph` 是进程内唯一的文件依赖图。`AssetDatabase` 不再维护反向图，
原来的 `ShaderDependencyGraph` 已删除。资产导入和 Shader 编译都将边写入同一个图。

```text
shader.json/source -> generated node -> preprocessed node -> intermediate SPV -> packaged SPV
include -----------------------------> preprocessed node
```

`FileWatcher` 把变化通知依赖图。`ShaderCompilePipeline::invalidateChanged()` 查询传递
依赖并逐级清除预处理、SPV 和 Program 缓存。RHI Shader 与 Graphics Pipeline 根据
失效的 `CompiledShaderId` 延迟销毁。

## 三种模式

### DevelopmentRuntime

首次请求 Pass/Variant 时按需生成、预处理和编译，SPV 保存在
`shader-cache://<compile-hash>.*.spv`。源文件变化后在下一次请求时重新编译。

### OfflineTool

`MiniShaderCompiler compile <shader.json> <pass> <output-directory>` 加载 Shader JSON，
实例化运行时 `Shader`，然后调用同一个 `ShaderCompilePipeline`。中间结果写入
`shader-cache://`，最终包内二进制写入 `shader-bin://`。

`MiniShaderCompiler` 是显式调用的离线工具。CMake 构建 `MiniVulkanEngine` 时不会运行它，也不会把 GLSL 编译为 SPIR-V。

需要生成仓库内置 Shader 的发布 SPV 时，显式执行：

```powershell
cmake --build build/clang-publish --target MiniShaderPackagedShaders
```

### PackagedRuntime

根据 Shader 路径、Pass、Variant、Target 和 Stage 计算固定路径，只加载
`shader-bin://*.spv`。任何阶段缺失都会记录预期路径、返回无效 Program，Renderer
使用内置洋红 Error Material。打包模式不会调用 glslc，也不会回退到运行时编译。

## GPU 创建

```text
ShaderCompilePipeline
  -> ShaderProgram + CompiledShader handles
  -> ShaderGpuManager -> ShaderModuleCache
  -> IDevice::createShader
  -> GraphicsPipelineCache
  -> IDevice::createGraphicsPipeline
  -> draw
```

单例 `ShaderGpuManager` 持有一个 `ShaderCompilePipeline`，Renderer 不拥有编译器、CompiledShaderCache 或 ShaderProgramCache。
