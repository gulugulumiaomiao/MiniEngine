# Asset Importer

本阶段实现资产计划的第 20～25 步：Importer 公共接口、按 `AssetType` 注册、
Shader/Material 内置 Importer，以及 Shader 源码依赖收集。导入管线和文件监听尚未
接入。

## 接口

`IAssetImporter` 输入 `AssetImportContext`，返回 `AssetImportResult`：

```text
AssetImportContext
  ├─ Meta与AssetId
  ├─ 源文件VirtualPath
  ├─ Meta VirtualPath
  ├─ Artifact VirtualPath
  └─ include搜索目录

AssetImportResult
  ├─ 是否成功
  ├─ AssetType
  ├─ Artifact VirtualPath
  ├─ VirtualPath依赖列表
  └─ 错误信息
```

Importer 不更新 AssetDatabase。后续 `AssetImportPipeline` 负责根据 Result 原子地
更新 AssetRecord，避免数据库进入只完成一半的状态。

`AssetDatabase` 已经是全局单例，因此不放进 `AssetImportContext`。Importer 后续
确实需要查询资产记录时直接使用 `ASSET_DATABASE`；上下文只携带每次导入特有的
输入数据。

## 注册表

`AssetImporterRegistry` 以 `AssetType` 为唯一键，不读取 Meta 中的 Importer 名称：

```cpp
AssetImporterRegistry registry;
registerBuiltinAssetImporters(registry);

const IAssetImporter* importer = registry.find(AssetType::Shader);
```

同一种 AssetType 重复注册会记录错误并拒绝覆盖。当前内置注册项只有：

- `ShaderAssetImporter`
- `MaterialAssetImporter`

Material Importer 在这一阶段只完成 JSON 解析验证和 Artifact 输出；Shader 引用依赖
及属性验证在后续 Material 导入步骤完善。

## Shader 导入

Shader Importer 执行：

```text
读取.shader.json
  → 使用现有ShaderLab解析器验证
  → 遍历SubShader和Pass
  → 收集vertex/fragment源码
  → 递归解析源码中的#include
  → 写入JSON Artifact
  → 返回依赖VirtualPath列表
```

Shader Artifact 的 payload 是经过解析验证并规范化排版的 ShaderLab JSON。导入过程
不会运行 glslc，不生成 SPIR-V，不创建 `VkShaderModule` 或 Pipeline。Shader 真正
参与绘制时仍由 `ShaderProcessor` 完成这些工作。

## 依赖规则

每个 Pass 的顶点和片元源码本身都是依赖，源码中的双引号 include 会被递归收集：

```glsl
#include "include/common.glsl"
```

解析顺序为：

1. 相对当前源码所在目录查找。
2. 按 `AssetImportContext::includePaths` 顺序查找。

依赖会去重；循环 include、格式错误、文件缺失或读取失败都会使本次导入失败，记录
`Log::error` 并返回 `success == false`。所有依赖始终保存为 `VirtualPath`。
