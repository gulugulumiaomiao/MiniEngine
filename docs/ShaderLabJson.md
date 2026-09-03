# JSON ShaderLab v1

## RenderState 默认值与失败语义

Pass 的 `state` 整体可省略，也可以只覆盖需要修改的字段。未声明字段采用下列默认值：

| 字段 | 默认值 |
|---|---|
| `cull` | `Back` |
| `frontFace` | `CW` |
| `fill` | `Solid` |
| `topology` | `TriangleList` |
| `depthWrite` | `true` |
| `depthTest` | `LessEqual` |
| `blend` | `Off` |
| `colorMask` | `RGBA` |

Shader/Material JSON 解析、材质校验、GLSL 生成、SPIR-V 读取与反射校验失败时只记录 `error`。对象接口返回 `nullptr`，Handle 接口返回无效 Handle；失败对象不会进入缓存，Renderer 会跳过无法创建 Pipeline 的 Pass。

## Pass 源程序与 Shader 接口声明

`program` 只声明顶点和片元源码：

```json
"program": {
  "vertex": "vertex_color.Forward.vert",
  "frag": "vertex_color.Forward.frag"
}
```

`vertex` 和 `frag` 都是必填字段，扩展名必须分别为 `.vert` 和 `.frag`。JSON 不声明 SPIR-V 路径和入口名称；离线生成器固定调用 `VertexMain` 与 `FragmentMain`，并把源码文件名追加 `.spv` 作为运行时二进制名称。

Pass 可以声明以下接口：

```json
{
  "vertexInput": [
    {
      "name": "position",
      "semantic": "POSITION",
      "type": "Vec3",
      "location": 0
    },
    {
      "name": "uv",
      "semantic": "TEXCOORD0",
      "type": "Vec2",
      "location": 1
    }
  ],
  "varyings": [
    {
      "name": "uv",
      "type": "Vec2",
      "location": 0,
      "interpolation": "Smooth"
    },
    {
      "name": "objectId",
      "type": "Float",
      "location": 1,
      "interpolation": "Flat"
    }
  ],
  "fragmentOutputs": [
    {
      "name": "color",
      "type": "Vec4",
      "location": 0
    }
  ]
}
```

字段含义：

- `vertexInput`：Mesh 提供给顶点着色器的输入；`semantic` 必填，用来与 `VertexLayout` 匹配。
- `varyings`：顶点着色器输出和片元着色器输入共用的接口。
- `fragmentOutputs`：片元着色器写入 RenderTarget 的输出。
- `name`：生成 GLSL 变量时使用的名称，必须是合法标识符。
- `type`：第一版支持 `Float`、`Vec2`、`Vec3`、`Vec4`。
- `location`：非负 32 位整数；同一接口列表中不可重复。
- `interpolation`：仅 `varyings` 支持，可选值为 `Smooth`、`Flat`、`NoPerspective`，默认为 `Smooth`。

每个接口列表内的 `name` 和 `location` 必须唯一；`vertexInput` 的 `semantic` 也必须唯一。离线生成器会据此生成 `MiniVertexInput`、`MiniVaryings`、`MiniFragmentOutput`、阶段 `layout(location)` 声明和 `main` 包装函数。入口函数约定见 [`ShaderCodeGeneration.md`](ShaderCodeGeneration.md)。

JSON ShaderLab 将 Shader 声明与材质参数值分开保存：

- `*.shader.json` 声明属性、标签、Pass、渲染状态、SPIR-V 路径和功能开关。
- `*.material.json` 引用一个 Shader，并覆盖属性值、Keyword 和渲染队列。

两种资产都必须包含 `"$schemaVersion": 1`。如果缺少必填字段、值类型错误、枚举值未知或名称重复，加载将失败，并在错误信息中指出对应的文件和 JSON 路径。

## Shader 资产

根对象支持以下字段：

- `name`：稳定的 Shader 名称。
- `tags`：旧版兼容的根标签；新资产应把标签写入对应的 SubShader。
- `properties`：材质属性声明。
- `subShaders`：一个或多个 SubShader；每项包含 `tags` 和 `passes`。

推荐结构：

```json
{
  "name": "Mini/Lit",
  "properties": [],
  "subShaders": [
    {
      "tags": {
        "renderPipeline": "MiniForward",
        "renderType": "Opaque",
        "queue": "Opaque"
      },
      "passes": [
        { "name": "Forward", "lightMode": "Forward", "program": {} }
      ]
    }
  ]
}
```

旧的单对象 `subShader` 字段仍可加载，但只作为迁移兼容格式。

支持的属性类型包括 `Float`、`Range`、`Vec2`、`Vec3`、`Vec4`、`Color`、`Texture2D` 和 `Bool`。
`Vec2`、`Vec3`、`Vec4` 的默认值必须分别包含两个、三个、四个数字，`Color` 必须包含四个数字；`Range` 还必须提供 `[min, max]` 范围。旧类型名 `Vector` 会按 `Vec4` 解析，以兼容已有资产。

支持的 Pass 光照模式包括 `Forward`、`DepthOnly` 和 `ShadowCaster`。每个 Pass 必须具有唯一名称、一个包含顶点和片元 SPIR-V 路径的 `program`、可选的功能 Keyword，以及可选的渲染状态对象。

支持以下渲染状态：

- `cull`：`Off`、`Front`、`Back`
- `frontFace`：`CW`、`CCW`
- `fill`：`Solid`、`Wireframe`
- `topology`：`TriangleList`、`LineList`
- `depthWrite`：布尔值
- `depthTest`：`Never`、`Less`、`LessEqual`、`Equal`、`Greater`、`GreaterEqual`、`Always`
- `blend`：`Off`、`Alpha`、`Additive`、`PremultipliedAlpha`
- `colorMask`：`RGBA` 的任意组合

命名渲染队列包括 `Background`、`Opaque`、`AlphaTest`、`Transparent` 和 `Overlay`。可以使用 `"Opaque+10"` 表示正向偏移，也可以直接填写整数队列值。

## Material 资产

根对象支持以下字段：

- `name`：材质显示名称。
- `shader`：相对于构建后资产根目录的 Shader 路径。
- `properties`：可选的属性覆盖值。
- `keywords`：可选功能开关，必须由 Shader 的某个 Pass 声明。
- `renderQueue`：可选的整数队列覆盖值。

未知属性、属性类型不匹配、重复 Keyword 和未声明的 Keyword 都会被视为错误。材质中没有填写的属性将使用 Shader 声明的默认值。

## 当前运行时范围

资产与运行时采用 `Shader → SubShader → ShaderPass`。Renderer 先按当前 `MiniForward` 渲染管线选择 SubShader，再按 Forward 渲染阶段选择 `Forward` Pass；DrawItem 保存本帧选中的 ShaderPass，Vulkan 初始 Pipeline 也直接由该 Pass 的 SPIR-V 和 RenderState 创建。两个示例材质分别覆盖了 `BaseColor`，该属性会随每对象 GPU 数据一起上传。

加入对应的 DrawList 和渲染目标后，`DepthOnly` 与 `ShadowCaster` 才会真正进入执行流程。

当前已经可以把 `Texture2D` 属性解析并校验为字符串；图像加载、GPU 上传和材质纹理 Descriptor 是材质系统的下一阶段。
