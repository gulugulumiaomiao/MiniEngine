# Shader Material Uniform 布局

## 目的

`UniformBlockLayout` 把 ShaderLab 的数值 Properties 转换为稳定的 uniform block 内存布局。该类型属于 Renderer 层，不依赖 Vulkan；后续 GLSL 生成器、Material 字节缓冲和 SPIR-V Reflection 都应使用或校验同一份布局。

相关代码：

```text
src/renderer/Shader.h
src/renderer/Shader.cpp
```

## 第一版布局规则

第一版采用 std140 兼容的保守布局：

| ShaderLab 类型 | Uniform 类型 | 对齐 | 占用大小 |
|---|---|---:|---:|
| Float | float | 4 | 4 |
| Range | float | 4 | 4 |
| Bool | uint | 4 | 4 |
| Vec2 | vec2 | 8 | 8 |
| Vec3 | vec3 | 16 | 16 |
| Vec4 | vec4 | 16 | 16 |
| Color | vec4 | 16 | 16 |
| Texture2D | descriptor | 不适用 | 不进入 UBO |

Vec3 强制占用 16 字节，第 4 个分量作为 padding。虽然部分 std140 实现允许后续 scalar 利用剩余位置，但固定 padding 可以避免驱动差异，也让 CPU 写入、代码生成与 Reflection 校验更简单。

每个成员的 offset 向上对齐到该类型的 alignment；整个 uniform block 的 `byteSize` 最终向上对齐到 16 字节。只有 Texture2D、没有数值属性时，布局大小为 0。

## 数据结构

```cpp
struct UniformMemberLayout {
    std::string name;
    ShaderPropertyType type;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t alignment;
};

struct UniformBlockLayout {
    std::uint32_t byteSize;
    std::vector<UniformMemberLayout> members;
};
```

通过以下函数生成：

```cpp
UniformBlockLayout buildUniformBlockLayout(
    std::span<const ShaderPropertyDesc> properties);
```

`findMember` 用于可选查询，`requireMember` 在属性不存在时记录 fatal 日志并退出程序。

## 当前示例

`vertex_color.shader.json` 的 Properties：

```text
BaseColor     Color
UvScale       Vec2
EmissiveColor Vec3
DebugParams   Vec4
```

生成结果：

| 属性 | Offset | Size | Alignment |
|---|---:|---:|---:|
| BaseColor | 0 | 16 | 16 |
| UvScale | 16 | 8 | 8 |
| EmissiveColor | 32 | 16 | 16 |
| DebugParams | 48 | 16 | 16 |

最终 `byteSize = 64`。`UvScale` 后面存在 8 字节 padding，使 `EmissiveColor` 从 32 开始。

## 当前阶段边界

布局计算本身不负责 GPU 对象：

- Material 系统已经使用该布局保存和修改 uniform 字节缓冲；
- 离线 Shader Generator 已使用该布局生成带显式 offset 的 GLSL uniform block；
- 尚未创建 Vulkan uniform buffer 或 descriptor；
- SPIR-V Reflection 已反向校验 Material descriptor 和每个 uniform 成员的 offset。

生成器的绑定约定、命令行与测试说明见 [`ShaderCodeGeneration.md`](ShaderCodeGeneration.md)。下一步将生成每个 Pass 的 stage 接口与入口包装代码，再把完整 GLSL 编译成 SPIR-V。
