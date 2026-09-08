# Asset Importer

Importer 负责把源资产验证并写成 Artifact，不负责更新 AssetDatabase，也不创建运行时对象。数据库更新、依赖顺序和失败状态由 `AssetImportPipeline` 统一处理。

## 接口

```text
AssetImportContext
  ├─ AssetMeta
  ├─ 源文件 VirtualPath
  ├─ Meta VirtualPath
  ├─ Artifact VirtualPath

AssetImportResult
  ├─ success / AssetType
  ├─ Artifact VirtualPath
  ├─ VirtualPath 依赖列表
  └─ 错误信息
```

`AssetImportContext` 不携带数据库引用。Importer 必须查询已导入资产时，通过全局 `ASSET_DATABASE` 访问。

## 注册规则

`AssetImporterRegistry` 以 `AssetType` 为唯一键。Meta 不记录 Importer 名称；管线按照 `asset_type` 选取 Importer。当前内置：

- `ShaderAssetImporter`
- `MaterialAssetImporter`

重复注册同一种类型会记录 error 并拒绝覆盖。

## Shader 导入

Shader Importer 只解析并验证 ShaderLab JSON、生成 Shader Artifact，不扫描 include，
也不运行 glslc。阶段源码和递归 include 由 `ShaderPreprocessor` 在实际编译时读取并记录到
全局 `FileDependencyGraph`。

依赖收集器分别维护 `visiting` 和 `visited`：前者检测当前递归栈中的循环，后者用于去重。格式错误、文件缺失、循环 include 或写 Artifact 失败都会记录 `Log::error` 并返回失败，不会调用 fatal。

Shader 导入输出类型专用的二进制 Artifact，不运行 glslc。SPIR-V、Reflection、ShaderProgram 和 Pipeline 在 Shader 真正参与绘制时按需生成。Material 同样使用独立的二进制序列化格式；运行时不再从 Artifact 二次解析 JSON。

## Material 导入

管线先解析 Material 的 Shader 虚拟路径并保证 Shader 已成功导入。Material Importer 随后从 Shader Artifact 读取属性声明，校验 Material 的 Properties 和 Keywords，并输出 Material Artifact。Material 依赖列表包含其 Shader 的 `asset://` 路径，因此 Shader 变化会通过 AssetDatabase 的反向依赖触发 Material 重导入。

完整导入与热重载流程见 [AssetPipeline.md](AssetPipeline.md)。
