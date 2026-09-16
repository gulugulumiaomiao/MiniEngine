# 源格式模块（asset/format）

`src/asset/format/`（命名空间 `engine::format`）集中管理人类可读源格式的解析与
验证：`.shader.json`、`.material.json`、`.scene.json`。Importer 只负责编排
（读文件、调用 format 解析、写 Artifact）；[Exporter.md](Exporter.md) 的写回
体系同样复用这些单元（Material 写回走 `writeMaterialAssetJson`，Scene 写回
走 scene 模块的 `writeSceneAssetJson`）。
运行时类（`Shader.cpp` / `Material.cpp` / `SceneAsset.cpp`）只保留二进制 transfer
与运行时行为，不再包含源文件解析。

## AssetFormatJson

`asset/format/AssetFormatJson.h`

**作用**：三个格式单元共享的 JSON 基元，纯头文件实现。

**运转流程**：

- `readJson(category, file, source)`：宽容解析（不抛库异常），失败即报错终止。
- `required<T>(category, object, key, file, path)`：必填字段读取，缺失或类型
  错误即失败。
- `parseEnum<Enum>(category, value, table, ...)`：字符串与枚举的映射表查询。
- `vectorValue<Length, Vector>(...)`：定长数值数组解析为 `Vec2/3/4`。
- `fail(category, file, path, message)`：以调用方的日志频道记录"文件: JSON 路径:
  原因"后抛出 `AssetParseFailure`；各格式单元在顶层 `catch` 并返回空结果。

**设计意图**：异常式失败让解析代码可以顺序书写（读到哪错到哪），失败点集中在
顶层处理，不需要层层传递错误码；`category` 参数保留各格式单元的历史日志频道
（如 `"ShaderAsset"`），日志行为与迁移前完全一致。基元把"字段读取 + 日志 + JSON
路径标注"的样板压缩掉，新格式单元只需描述 schema。

## ShaderAssetFormat

`asset/format/ShaderAssetFormat.h` / `ShaderAssetFormat.cpp`

**作用**：`.shader.json` → `ShaderAsset`。

**运转流程**：`parseShaderAsset(path, source)` 解析 ShaderLab JSON：名称、
properties（类型化默认值）、subShader/passes（lightMode、program 的 vertex /
fragment 阶段、接口声明、RenderState）。任何 schema 违规返回 `nullptr` 并以
`"ShaderAsset"` 频道记录错误。

**设计意图**：导入阶段只做"声明解析"，不扫描 GLSL include、不运行 glslc——
阶段源码与递归 include 由 `ShaderPreprocessor` 在真正编译时读取并记录到全局
`FileDependencyGraph`（见
[ShaderCompilePipeline.md](../ShaderCompilePipeline.md)）；SPIR-V、Reflection 与
Pipeline 也都在 Shader 参与绘制时按需生成。语法细节见
[ShaderLabJson.md](../ShaderLabJson.md)。

## MaterialAssetFormat

`asset/format/MaterialAssetFormat.h` / `MaterialAssetFormat.cpp`

**作用**：`.material.json` ↔ `MaterialAsset`，以及跨资产校验。

**运转流程**：

- `parseMaterialAsset(path, source)` / `parseMaterialAsset(path, source, resolver)`：
  解析名称、shader 引用、properties 属性值、keywords 与可选 renderQueue。shader
  与 texture 引用经 `AssetReference` 处理：`guid://` 形态需要 resolver 定位，
  路径形态按所属挂载解析。
- `writeMaterialAssetJson(material, resolver)`：写回编码（与 parse 同 TU 对称）。
  `ordered_json` 保字段顺序，`dump(2) + "\n"`；shader 与 texture 属性值经
  `resolver.findGuid(path)` 命中写 `guid://...`，未命中回退绝对路径
  （parse 双形态均支持，SceneExport 同模式）；JSON 值形态：Float/Range→number、
  Boolean→bool、Vec2/3/4 与 Color→数组、Texture2D→string。字段省略：
  renderQueue 无 override 省略、keywords 空省略、properties 空省略。
- `validateMaterialAsset(material, shader, materialPath)`：跨资产校验——每个
  property 与 keyword 必须由引用的 Shader 声明，属性值类型必须匹配声明。

**设计意图**：解析与校验拆分：解析产出 `MaterialAsset`（不依赖 Shader 已导入），
校验需要 Shader 的属性声明，由 importer 在依赖（Shader Artifact）就绪后调用
（见 [Importer.md](Importer.md) 的 MaterialAssetImporter）。属性值类型系统
（`ShaderValue`：标量/布尔/字符串/VecN）与 Shader 声明共享。写侧的省略语义与
属性按名排序保证：等值的 MaterialAsset 序列化出等值字节，字段只在有真实语义
时出现（空纹理槽不写、override 缺省不写），详见 [Exporter.md](Exporter.md)。

## SceneAssetFormat

`asset/format/SceneAssetFormat.h` / `SceneAssetFormat.cpp`

**作用**：`.scene.json` → `SceneAsset`，以及结构校验。

**运转流程**：

- `parseSceneAsset(path, source)` / `parseSceneAsset(path, source, resolver)`：
  解析节点树与组件（Transform / Mesh / Material 组件）。Mesh 与 Material 组件的
  引用同样走 `AssetReference`（GUID 需 resolver），并校验引用指向预期的资产类型
  与扩展名。
- `validateSceneAsset(asset, scenePath)`：结构校验——节点 id 唯一、层级无环且
  父节点存在、每节点一个 Transform、无重复组件类型、组件取值范围。解析、二进制
  transfer 与导出（`SceneExport`）三方共用这一校验。

**设计意图**：场景格式细节见 [SceneAsset.md](../SceneAsset.md)。组件扩展名的
硬校验（`.mesh.json` / `.material.json`）同时天然阻止了内置类型间的资产级循环
依赖，管线的循环防护主要面向 ScriptedImporter 声明的依赖（见
[Pipeline.md](Pipeline.md)）。

## 模块级设计意图

- **解析是纯函数**：输入 = 路径 + 文本 + 可选 `GuidResolver`，输出 = Asset 或
  `nullptr`。不写数据库、不落 Artifact，便于单测与 Importer / Exporter 双向复用。
- **依赖注入**：GUID 解析经 `GuidResolver` 接口，格式层不绑定 `AssetDatabase`
  单例；编辑器、离线 cooker 与测试可以注入不同的解析环境。写侧同样只依赖
  `GuidResolver`（路径 → GUID 反查），保持双向对称。
- **失败语义统一**：任何违反 schema 的输入返回空结果并记录 `Log::error`，绝不
  fatal、绝不部分产出。
