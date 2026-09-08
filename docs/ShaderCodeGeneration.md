# ShaderLab Properties GLSL 代码生成

> 编译入口已统一为 `ShaderCompilePipeline`，当前运行时、离线、打包和 include 搜索规则参见 [ShaderCompilePipeline.md](ShaderCompilePipeline.md)。本文其余内容主要保留代码生成规则与历史实现背景。

## 目标

这一阶段把 ShaderLab `properties` 转换为确定性的 GLSL 声明，让 CPU 端 `Material` 与 GPU 端 uniform block 使用同一份 `UniformBlockLayout`，避免手写两套字段、类型和 offset。

代码生成同时供运行时按需编译和离线验证工具使用，不依赖 Vulkan Device：

```text
*.shader.json
      │ ShaderAssetLoader
      ▼
ShaderAsset + UniformBlockLayout
      │ ShaderGenerator
      ▼
*.material.glsl
```

相关代码：

```text
src/render/shader/ShaderGenerator.h
src/render/shader/ShaderGenerator.cpp
tools/shader_compiler/main.cpp
```

## 第一版绑定约定

- 数值属性放入 `layout(std140, set = 1, binding = 0)` 的 `MaterialProperties` uniform block；
- block 实例名为 `Material`，Shader 代码通过 `Material.BaseColor` 访问字段；
- `Texture2D` 不进入 uniform block，从 `set = 1, binding = 1` 开始按属性声明顺序分配独立 `sampler2D`；
- Scene/Object 数据保留给其他 descriptor set，材质资源统一放在 set 1；
- 每个数值成员输出显式 `layout(offset = N)`，offset 直接来自 `UniformBlockLayout`。

ShaderLab 类型映射如下：

| ShaderLab | GLSL |
|---|---|
| Float、Range | `float` |
| Bool | `uint` |
| Vec2 | `vec2` |
| Vec3 | `vec3` |
| Vec4、Color | `vec4` |
| Texture2D | `sampler2D` |

例如 `assets/shaders/vertex_color.shader.json` 会生成：

```glsl
// Generated from ShaderLab properties. Do not edit.
layout(std140, set = 1, binding = 0) uniform MaterialProperties
{
    layout(offset = 0) vec4 BaseColor;
    layout(offset = 16) vec2 UvScale;
    layout(offset = 32) vec3 EmissiveColor;
    layout(offset = 48) vec4 DebugParams;
} Material;
```

显式 offset 是这里的重要约束：生成器不会重新推导一套 GLSL 布局，而是消费 CPU Material 已经使用的布局并校验属性顺序和类型。

## 命令行与 CMake

工具使用方式：

```powershell
./build/clang-debug/MiniShaderCompiler.exe compile `
  assets/shaders/vertex_color.shader.json Forward `
  build/clang-debug/generated-shaders
```

生成某个 Pass 的两个完整 GLSL 阶段：

```powershell
cmake --build --preset clang-debug --target MiniShaderPackagedShaders
```

CMake 只保留 `MiniShaderPackagedShaders` 离线目标，并通过 `MiniShaderCompiler compile` 调用统一管线。开发运行时第一次绘制 Pass/Variant 时按需编译；打包运行时只加载该目标生成的 SPIR-V：

```text
build/<preset>/generated-shaders/runtime/<compile-hash>.vert.spv
build/<preset>/generated-shaders/runtime/<compile-hash>.frag.spv
build/<preset>/generated-shaders/compiled/<package-hash>.vert.spv
build/<preset>/generated-shaders/compiled/<package-hash>.frag.spv
```

输出目录属于构建产物，不应手工修改，也不作为源资产提交。

编译参数约定：

- 目标环境固定为 `--target-env=vulkan1.3`；
- Debug 使用 `-O0 -g`，保留调试信息；
- Release 及其他非 Debug 配置使用 `-O`；
- 顶点、片元阶段分别指定 `-fshader-stage=vert/frag`，不依赖文件的最后一个扩展名猜测阶段。

可以只构建当前离线 Shader 产物：

```powershell
cmake --build --preset clang-debug --target MiniShaderPackagedShaders
```

## 校验与测试

生成前会检查：

- 属性名称、block 名和实例名都是合法 GLSL 标识符；
- `UniformBlockLayout` 与 ShaderLab 数值属性的数量、顺序和类型一致；
- descriptor set/binding 没有溢出；
- 输出文件能够被创建和完整写入。

`ShaderGeneratorTest` 使用运行时 `Shader` API 和 golden file 对完整阶段输出做逐字节比较。材质 set/binding 是渲染约定，不再暴露未使用的生成配置接口。

## Pass 接口与入口包装

源程序格式的 Pass 会生成两个独立阶段。生成器把 JSON 接口转换为三个稳定结构体：

```glsl
struct MiniVertexInput { /* vertexInput */ };
struct MiniVaryings { /* varyings */ };
struct MiniFragmentOutput { /* fragmentOutputs */ };
```

两个用户源文件都不写 `#version` 和 `main`。`.vert` 固定实现 `VertexMain`：

```glsl
void VertexMain(MiniVertexInput inValue, out MiniVaryings outValue)
{
    gl_Position = vec4(inValue.position, 0.0, 1.0);
    outValue.color = inValue.color;
}
```

`.frag` 固定实现 `FragmentMain`：

```glsl
void FragmentMain(MiniVaryings inValue, out MiniFragmentOutput outValue)
{
    outValue.color = vec4(inValue.color, 1.0);
}
```

生成器分别读取两个源文件并生成对应阶段的 `main`：

1. 从带 `_MiniIn_` 前缀的全局输入组装入口结构体；
2. 调用固定入口 `VertexMain` 或 `FragmentMain`；
3. 把输出结构体拆到带 `_MiniOut_` 前缀的全局输出。

`location` 原样写入 GLSL；varying 的 `Smooth`、`Flat`、`NoPerspective` 分别映射为 `smooth`、`flat`、`noperspective`。顶点位置是 Vulkan 内建值，不进入 `varyings`，顶点入口必须写入 `gl_Position`。

当某个接口列表为空时，对应结构体包含内部 `_unused` 字段，从而保持入口函数签名固定。该字段只属于生成器实现，不需要用户读写。

## 编译错误定位

生成器启用 `GL_GOOGLE_cpp_style_line_directive`，并在用户源码前后插入文件名形式的 `#line`：

```glsl
#line 1 "vertex_color.Forward.vert"
// 用户源码
#line 1 "MiniShaderCompiler/Forward/vertex-wrapper"
// 生成的包装代码
```

因此用户入口中的语法或类型错误会显示原始 `program.vertex` 或 `program.frag` 文件名和行号；生成器自身产生的问题会显示 Pass 和 stage。运行时 `glslc` 返回非零退出码时会记录 Error，并让当前 Pass 返回无效 Pipeline，不会把失败输出当作可用 SPIR-V。

## 当前边界与下一步

当前已经完成 `.vert/.frag` → 生成阶段 GLSL → `glslc` → SPIR-V → SPIRV-Cross Reflection → 运行时 ShaderProgram 的完整构建链。运行时二进制路径由处理后源码哈希生成，不再保存在 `ShaderPassDesc` 中。

## 统一预处理流程

`ShaderGenerator` 由 `ShaderCompilePipeline` 调度。它只接收运行时 `Shader`、`ShaderPass`、当前阶段和用户源码，生成 Properties、接口与入口包装；随后预处理器展开 `#include`、注入变体宏并计算源码哈希。

Shader JSON、阶段源码、include 依赖、生成后的 SPIR-V 和反射输入均使用 `VirtualPath`。资产源码通常位于 `asset://`，按需生成的 GLSL/SPIR-V 位于 `shader://runtime/<source-hash>.*`；只有文件系统挂载层负责映射到物理路径。
