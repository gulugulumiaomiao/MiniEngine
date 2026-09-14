# 旧测试迁移指南

当前仍有 41 个旧模式测试目标（40 份源码，`ProjectConfigTest.cpp` 编译两遍）使用手写 `main()` + 返回码断言，按渐进原则迁移：改到哪个迁哪个，两种注册模式长期共存。已迁移的 `HashTest`、`LogTest` 可作参照。总览见 [Testing.md](Testing.md)，gtest 用法见 [GoogleTest.md](GoogleTest.md)。

## 迁移步骤

1. 把 `XxxTest.cpp` 的 `main()` 拆成多个 `TEST(Suite, Case)`：按原代码中 `return N` 的逻辑分组切分，每组一个用例。
2. 断言替换：见下方映射表。优先用值断言（`EXPECT_EQ` 等），失败时自动打印期望与实际。
3. 删除手写的 `main()`、`fail()` 辅助函数和 `argc/argv` 分支。
4. `tests/CMakeLists.txt` 中把 `mini_add_test(XxxTest)` 改为 `mini_add_gtest(XxxTest)`；删除围绕它的附加 `add_test`/`WILL_FAIL` 条目。
5. 双配置验证：Debug 与 Release 各自构建 + 全量 ctest 通过。核对原测试的**每个** `return N` 分支都有对应断言覆盖——迁移是换表达形式，不是删检查。

## 模式映射

| 旧模式 | gtest 对应 |
|---|---|
| `if (cond) return N;` | `EXPECT_TRUE`/`EXPECT_FALSE`，能用值断言就用 `EXPECT_EQ/NE` |
| `return fail("msg")` | `EXPECT_*(...) << "msg"`（流式附加上下文） |
| 长条件链 `a != x \|\| b != y \|\| ...` | 拆成多条 `EXPECT_EQ(a, x); EXPECT_EQ(b, y);`，失败精确定位到单项 |
| 线性 `main()` 中前段失败跳过后续 | 拆成独立 `TEST`，互不传染 |
| 手动初始化/清理（临时目录、mount） | `TEST_F` fixture 的 `SetUp`/`TearDown` |
| 跨用例共享环境（`TestAssetEnvironment.h`） | fixture 内调用，或 `::testing::Environment` 全局注册 |
| 命令行参数 + `WILL_FAIL TRUE` | `EXPECT_EXIT` 死亡测试（套件名以 `DeathTest` 结尾） |
| 手写浮点比较 | `EXPECT_FLOAT_EQ`/`EXPECT_NEAR` |

## 特殊案例

- **同一源码双变体**：`ProjectConfigTest.cpp` 同时被 `ProjectConfigTest`（链 `MiniEngine`）和手动注册的 `ProjectConfigEditorTest`（链 `MiniEngineEditor`）编译。迁移时源码只改一份，但两个 target 都要换成 gtest 注册。
- **不链接引擎库的测试**：`SelectionSetTest` 是纯头文件逻辑测试（手动 `add_executable`，只加 include 路径，不链 `MiniEngine`）。这类测试迁移时同样不需要引擎库，手动注册并只链 `MiniGTest` 即可。
- **带额外依赖的测试**：资产类测试的 `MINI_TEST_*` 编译定义、`add_dependencies(... MiniCopyBuiltin)`、`POST_BUILD` cook 步骤（`AssetReleaseTest`）都是 target 级机制，与断言框架无关，迁移时原样保留（见 [AssetEnvironment.md](AssetEnvironment.md)）。
- **编辑器测试**：`mini_add_editor_test` 目前没有 gtest 版本；首个编辑器测试迁移时，先仿照 `mini_add_gtest` 增加 `mini_add_editor_gtest`（链 `MiniEngineEditor` + `MiniGTest`，保留 `${ARGN}` 追加源文件能力）。
- **不适用迁移**：`RenderRhiBoundaryTest` 是 `cmake -P` 架构守卫脚本，保持原样。

## 待迁移清单

按子系统分组：

- **核心**（5）：BuildConfigTest、FileSystemTest、HandlePoolTest、FrustumTest、TransferTest
- **资产**（8）：AssetFoundationTest、AssetImporterTest、AssetPipelineTest、AssetReleaseTest、TextureTest、MeshTest、SceneAssetTest、SceneExportTest
- **Shader/材质**（5）：ShaderCompilerTest、ShaderAssetTest、ShaderLayoutTest、ShaderGeneratorTest、MaterialTest
- **渲染**（10）：RenderCacheTest、RenderGraphTest、RenderTargetTest、RenderPipelineTest、RenderQueueTest、DrawBatcherTest、RenderPassTest、RgTexturePoolTest、SwapchainLayoutTest、VulkanPipelineCacheTest
- **场景**（1）：SceneTest
- **项目配置**（2）：ProjectConfigTest、ProjectConfigEditorTest（同源双变体）
- **编辑器**（9）：BuildConfigEditorTest、SceneHierarchyEditorTest、HierarchyPanelTest、InspectorPanelTest、ProjectSceneTest、ProjectTemplateTest、ProjectRegistryTest、ProjectLifecycleTest、ImGuiRendererTest
- **独立逻辑**（1）：SelectionSetTest

清单以 `tests/CMakeLists.txt` 当前内容为准，迁移一个划掉一个。
