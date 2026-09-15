# 资产测试环境

资产、Shader、材质、场景类测试都依赖引擎的虚拟文件系统与 `AssetManager` 单例。本文说明环境搭建、fixture 组织与构建期依赖。总览见 [Testing.md](Testing.md)，资产系统文档见 [../asset/README.md](../asset/README.md)。

## TestAssetEnvironment.h

共享头 `tests/TestAssetEnvironment.h` 提供一对函数（`engine::test` 命名空间）：

```cpp
[[nodiscard]] bool initializeAssetEnvironment(
    const std::filesystem::path& assetRoot,
    bool assetReadOnly = false,
    AssetManagerMode mode = AssetManagerMode::Development);
void shutdownAssetEnvironment();
```

`initializeAssetEnvironment` 先关闭旧环境，再挂载 `assets://`（指向 `assetRoot`）与 `library://`（指向 `assetRoot` 的兄弟目录 `library/`），最后初始化 `AssetManager`。只挂载这两个 scheme：Error Material、内置 Shader 等引擎自带内容随 `MiniCopyBuiltin` 位于资产根内，与真实项目 `assets/` 的布局一致。

测试结束必须调用 `shutdownAssetEnvironment()`（`AssetManager.shutdown` + unmount），否则单例状态会泄漏进同进程的后续检查——gtest 迁移时把它放进 fixture 的 `SetUp`/`TearDown`，或注册为 `::testing::Environment`。

## MINI_TEST_* 编译定义

fixture 路径全部由 CMake 注入，测试源码不写死路径：

| 定义 | 指向 | 用途 |
|---|---|---|
| `MINI_TEST_BUILTIN_DIR` | `builtin/`（源码树） | 全部测试自动获得（注册 helper 注入） |
| `MINI_TEST_ASSET_DIR` | `<build>/assets` | 构建目录资产根，配合 `MiniCopyBuiltin` |
| `MINI_TEST_SHADER_FIXTURE_DIR` | `tests/assets` | shader 测试固件 |
| `MINI_TEST_MATERIAL_FIXTURE_DIR` | `tests/assets` | 材质测试固件 |
| `MINI_TEST_GENERATED_SHADER_DIR` | `generated-shaders/` | ShaderCompilerTest 的输出目录 |
| `MINI_TEST_SOURCE_DIR` | 项目根 | AssetImporterTest 定位 `tests/data` 等源树文件 |
| `MINI_TEST_SCRATCH_DIR` | `<build>/template-test` | ProjectTemplateTest 在构建树内建临时项目 |
| `MINI_TEST_COOKED_ASSET_DIR` | `<build>/assets` | AssetReleaseTest 的 cook 输入 |

`MINI_TEST_SCRATCH_DIR` 必须与 `MINI_TEST_BUILTIN_DIR` 同卷：`copy_file` 只在源与目标同卷时才拒绝覆盖已存在文件，放 `temp_directory_path()` 复现不了该缺陷（背景见 `tests/CMakeLists.txt` 注释与跨平台复制缺陷记录）。

## MiniCopyBuiltin

`tools/CMakeLists.txt` 定义的 always-stale custom target：每次构建把源码树 `builtin/` 拷到构建目录（可执行文件旁），**排除 `*.meta`**——构建产物从不携带 Meta 身份，与项目创建时拷入 `assets/` 的内容一致。

需要构建目录资产根的测试必须声明依赖，保证拷贝先于测试运行：

```cmake
mini_add_test(MaterialTest)
target_compile_definitions(MaterialTest PRIVATE
    MINI_TEST_ASSET_DIR="${CMAKE_BINARY_DIR}/assets"
)
add_dependencies(MaterialTest MiniCopyBuiltin)
```

## fixture 目录组织

```text
tests/
├── assets/                        资产测试固件
│   ├── shaders/                   builtin_color.shader.json（最小可用 shader）
│   ├── materials/                 error.material.json
│   ├── textures/                  black.png / white.png（含 .meta）
│   ├── material_*.json            材质值 / shader 切换 / 非法导入 fixture
│   ├── shader_interface_*.json    shader 接口正反例（valid、duplicate_location、
│   │                              duplicate_name、missing_entry）
│   ├── *.expected.*.glsl          生成结果的逐字节比对基线
│   ├── preprocess_*.glsl          include 预处理 fixture
│   └── generated_interface.*      代码生成输入样例
└── data/                          texture-test.png / .jpg（导入器二进制输入）
```

新增固件的原则：

- 正反例成对（如 `shader_interface_valid.shader.json` 与 `shader_interface_duplicate_location.shader.json`），expected 输出直接入库供逐字节比对。
- 固件保持最小化，只包含被测行为必需的内容。

## 写保护与隔离

源树 fixture 不能被测试写脏（FileWatcher 会自愈 `.meta`，导入器会写回源文件）：

- `MaterialTest` 把 `MINI_TEST_MATERIAL_FIXTURE_DIR` 拷贝到 `temp_directory_path()` 下的独立目录再挂载，写操作全部落在副本上。
- `AssetReleaseTest` 反向操作：从构建目录的 builtin 副本 cook 到构建目录 `library/`（`POST_BUILD` 先清空 library 再 cook），避免对源树 cook 导致 importer 把 `.meta` 自愈回 `builtin/`。

新测试涉及写资产时，一律先拷贝到临时目录或构建目录再挂载，不直接挂载 `tests/assets`。
