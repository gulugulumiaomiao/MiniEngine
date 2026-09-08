# SPIR-V Reflection

Shader 模块使用 Khronos SPIRV-Cross 的 core 库进行反射。`reflectSpirv()` 读取 SPIR-V
并转换成引擎自己的 `SpirvReflection`，上层不依赖第三方类型。结果包含：

- Shader stage；
- stage input/output 的 location 和数据类型；
- descriptor set、binding 和 descriptor 类型；
- uniform/storage block 成员名称和 offset。

`validateSpirvReflection()` 将顶点和片元 SPIR-V 与运行时 `Shader` 声明进行校验：

- `vertexInput` 对应顶点阶段输入；
- `varyings` 同时对应顶点输出和片元输入；
- `fragmentOutputs` 对应片元输出；
- Material uniform block 固定为 set 1、binding 0，成员 offset 必须匹配
  `UniformBlockLayout`；
- Texture2D 从 set 1、binding 1 开始连续分配 combined image sampler；
- 顶点和片元阶段的同一 descriptor 不能出现类型冲突。

## 编译时校验

`MiniShaderCompiler compile` 和开发运行时都通过 `ShaderCompilePipeline` 编译，并在创建
`ShaderProgram` 前执行同一套反射校验。校验失败时不会产生有效 Program。

旧的 `reflect` 命令、专用 `.reflection.spv` 和 `MiniShaderLabReflection` 构建目标已经删除，
避免维护第二条 Shader 编译流程。

## 第三方依赖

项目固定使用 SPIRV-Cross `vulkan-sdk-1.4.309.0`，只编译反射需要的 core 源码。依赖位于
`third_party/spirv-cross`，许可证位于同目录的 `LICENSE`。
