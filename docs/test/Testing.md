# 测试体系

引擎测试由 CTest 编排，每个测试编译为独立可执行文件并链接 `MiniEngine` 静态库。断言框架分两种模式并存：传统的手写 `main()` + 返回码断言，与 Google Test（新测试一律采用）。

## 目录结构

```text
tests/
├── assets/                      测试固件（shader、material、texture 等 fixture）
├── data/                        二进制与序列化测试数据
├── CMakeLists.txt               测试注册与测试专用编译定义
├── TestAssetEnvironment.h       共享的资产环境初始化/关闭
├── RenderRhiBoundaryTest.cmake  架构守卫脚本（cmake -P 执行）
└── *Test.cpp                    测试源码，一个文件对应一个可执行目标
```

Google Test v1.18.0 vendor 在 `third_party/googletest/`（含尚未编译的 googlemock 源码），由 `tests/CMakeLists.txt` 把 `gtest-all.cc` + `gtest_main.cc` 编成 `MiniGTest` 静态库，不引入上游 CMake 的安装规则与包导出。

## 注册方式

| 函数 | 链接库 | 用途 |
|---|---|---|
| `mini_add_test` | `MiniEngine` | 传统手写 `main()` 测试 |
| `mini_add_editor_test` | `MiniEngineEditor` | 需要 `MINI_EDITOR` 的编辑器测试，可追加 `tools/editor` 源文件 |
| `mini_add_gtest` | `MiniEngine` + `MiniGTest` | Google Test 测试，入口由 `gtest_main` 提供 |

`mini_add_gtest` 通过 `gtest_discover_tests` 把每个 `TEST()` 注册为独立 CTest 条目（名为 `Suite.Case`），失败可定位到单个用例而不是一个裸退出码。部分测试在注册后还需追加链接库或编译定义（如 `HierarchyPanelTest`、`InspectorPanelTest` 链 `MiniImGui`，资产类测试定义 `MINI_TEST_ASSET_DIR`），少数完全手动注册（如 `ImGuiRendererTest`；`SelectionSetTest` 是纯头文件逻辑测试，不链接引擎库）。`RenderRhiBoundaryTest` 是 `cmake -P` 脚本测试，守卫已废弃的渲染层旧目录（`render/backend`、`render/cache` 等）不再复活。

## 验证流程

代码改动后必须验证 Debug 与 Release 两个配置（Publish 为 `BUILD_TESTING=OFF`，不生成测试目标）：

```powershell
# 构建：Invoke-CMake.ps1 自动配置 WinLibs clang++、ninja、cmake 与 Vulkan SDK 的 PATH
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dev\Invoke-CMake.ps1 --build build\clang-debug
powershell -NoProfile -ExecutionPolicy Bypass -File tools\dev\Invoke-CMake.ps1 --build build\clang-release

# 全量测试
ctest --test-dir build\clang-debug --output-on-failure
ctest --test-dir build\clang-release --output-on-failure
```

环境已配置好时也可直接用 preset：`cmake --build --preset clang-debug`。

判定标准：

- 构建输出无 `error:`、`FAILED`、`ninja: build stopped`。WinLibs 的 `duplicate section ... has different size` 链接警告是工具链既有噪音，与改动无关。
- ctest 100% 通过。测试总数随功能增长，以当前注册条目为准，不依赖历史数字基线。
- 涉及渲染行为的改动还需实跑引擎确认，两个配置的差异来源见 `ShaderCompiler` 的在线编译（`-O0` 与 `-O` 会产生不同的 SPIR-V）。

选择性运行：

```powershell
ctest --test-dir build\clang-debug -R "TextureTest|AssetImporterTest"  # 按 CTest 条目名过滤
build\clang-debug\HashTest.exe --gtest_filter="HashFileTest.*"         # gtest 用例级过滤
```

## Google Test 用法约定

新测试一律用 gtest 编写并以 `mini_add_gtest` 注册。

- **断言分级**：`EXPECT_*` 失败后继续执行，一次运行暴露全部问题；`ASSERT_*` 失败即终止当前用例，仅用于后续断言依赖其结果的前置条件。
- **用例组织**：逻辑独立的检查拆成多个 `TEST(Suite, Case)`，不再写成线性 `return N` 链条；需要共享 setup/teardown 时用 `TEST_F` fixture（参考 `HashTest.cpp` 的 `HashFileTest`），资源获取与释放在 `SetUp`/`TearDown` 中自动配对。
- **死亡测试**：断言"进程应当退出"的行为用 `EXPECT_EXIT`，所在套件名以 `DeathTest` 结尾（参考 `LogTest.cpp`）。它替代旧的 `add_test` + `WILL_FAIL TRUE` 模式，能同时断言退出码与 stderr 内容：

  ```cpp
  TEST(LogDeathTest, FatalExitsWithFailureCode) {
      EXPECT_EXIT(engine::Log::fatal("LogTest", "Fatal exits the process"),
                  ::testing::ExitedWithCode(EXIT_FAILURE),
                  "Fatal exits the process");
  }
  ```

- **命名**：文件 `XxxTest.cpp`；套件名与主题对应；用例名描述被验证的行为。

## 旧测试迁移约定

- 渐进迁移：改到哪个旧测试就顺手迁移哪个，两种注册模式长期共存，不做一次性重写。
- 迁移映射：`if (...) return N;` → `EXPECT_*`/`ASSERT_*`；手写环境初始化 → fixture 或 `::testing::Environment`；`WILL_FAIL` 条目 → 死亡测试。
- 迁移完成后把 `mini_add_test` 注册改为 `mini_add_gtest`，并删除手写的 `add_test`/`WILL_FAIL` 附加条目。

## 已知陷阱

- **单例宏不加命名空间前缀**：`FILE_SYSTEM` 等宏展开为 `(::engine::FileSystem::instance())`，写成 `engine::FILE_SYSTEM` 会触发 `expected unqualified-id` 编译错误。只有普通函数与类型才用 `engine::` 限定。
- **纯 gtest 没有 gmock matcher**：`::testing::HasSubstr` 属于 gmock，当前 `MiniGTest` 只编译了 gtest；死亡测试的 matcher 直接传字符串字面量，gtest 按正则匹配 stderr。需要 gmock 时把 `gmock-all.cc` 加入 `MiniGTest` 即可，源码已 vendor。
- **断言宏里避免 braced initializer list**：`EXPECT_EQ(v, math::Vec2{1.0F, 2.0F})` 的逗号会被预处理器拆成多个宏参数；先赋给具名变量再断言。
- **清理路径的 `[[nodiscard]]`**：`TearDown` 中的尽力清理用 `(void)` 显式丢弃返回值（如 `(void)FILE_SYSTEM.unmount(...)`，与 `TestAssetEnvironment.h` 一致），因为 `SetUp` 提前失败时 `TearDown` 仍会执行。

## 测试环境

- 需要资产环境的测试通过 `TestAssetEnvironment.h` 的 `engine::test::initializeAssetEnvironment(assetRoot)` 初始化：挂载 `assets://` 与 `library://` 并启动 `AssetManager`，结束时调用 `shutdownAssetEnvironment()`。
- fixture 路径由编译定义注入：`MINI_TEST_ASSET_DIR`（构建目录资产根）、`MINI_TEST_BUILTIN_DIR`、`MINI_TEST_SHADER_FIXTURE_DIR` 等；依赖内置资产的目标用 `add_dependencies(... MiniCopyBuiltin)` 保证拷贝已完成。
- 编辑器测试（`mini_add_editor_test`）链接 `MiniEngineEditor`，能看到 `MINI_EDITOR` 保护的接口。

## 相关文档

本目录专题：

- [GoogleTest.md](GoogleTest.md)：MiniGTest 构建、CTest 集成、用例组织与死亡测试细节
- [Migration.md](Migration.md)：旧测试迁移步骤、模式映射与待迁移清单
- [AssetEnvironment.md](AssetEnvironment.md)：资产测试环境、`MINI_TEST_*` 定义与 fixture 组织

引擎文档：

- [SourceLayout.md](../SourceLayout.md)：目录结构与 CMake 分工
- [AssetPipeline.md](../AssetPipeline.md)：构建/发布流程与资产测试覆盖
- [Editor.md](../Editor.md)：编辑器测试与 `MiniEngineEditor` 变体
- [Logging.md](../Logging.md)：日志级别与 `fatal` 退出行为（死亡测试的断言依据）
