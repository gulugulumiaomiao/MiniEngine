# SPIR-V Reflection

Shader 模块使用 Khronos 官方 SPIRV-Cross 的 core 库进行反射。`reflectSpirv()` 封装 SPIRV-Cross C++ API，读取 SPIR-V 二进制并转换成引擎自己的 `SpirvReflection`，因此上层不依赖第三方类型。反射结果包含：

- Shader stage；
- stage input/output 的 location 和数据类型；
- descriptor set、binding 和 descriptor 类型；
- uniform/storage block 成员名称与 offset。

`validateSpirvReflection()` 将顶点和片元 SPIR-V 与 ShaderLab JSON 自动校验：

- `vertexInput` 对应顶点阶段输入；
- `varyings` 同时对应顶点输出和片元输入；
- `fragmentOutputs` 对应片元输出；
- Material uniform block 固定在 set 1、binding 0，成员 offset 必须匹配 `UniformBlockLayout`；
- Texture2D 从 set 1、binding 1 开始连续分配 combined image sampler；
- 顶点和片元阶段的同一 descriptor 不能出现类型冲突。

## 离线命令

```powershell
MiniShaderCompiler.exe reflect `
  assets/shaders/vertex_color.shader.json `
  Forward `
  build/clang-debug/generated-shaders/vertex_color.Forward.vert.reflection.spv `
  build/clang-debug/generated-shaders/vertex_color.Forward.frag.reflection.spv
```

CMake 目标 `MiniShaderLabReflection` 会在 GLSL 编译成 SPIR-V 后自动执行该校验。校验失败会立即中止构建，不会把接口不匹配的 Shader 交给运行时。

Release 构建中的 `-O` 可能删除未使用的 descriptor 或 uniform 成员，甚至重新排列只剩下的成员，因此不能用优化后的运行产物检查完整 ShaderLab 声明。构建系统会额外生成带 `.reflection.spv` 后缀的 `-O0 -g` 反射产物用于校验；正常的 `.spv` 仍保持 Release 优化并交给运行时使用。

## 第三方依赖

项目固定使用 SPIRV-Cross `vulkan-sdk-1.4.309.0`，只编译反射所需的 core 源码。依赖位于 `third_party/spirv-cross`，许可证保存在同目录的 `LICENSE` 中。
