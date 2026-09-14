# Google Test 集成

Google Test v1.18.0 vendor 在 `third_party/googletest/`，是测试断言框架的目标形态：新测试一律采用，旧测试按 [Migration.md](Migration.md) 渐进迁移。测试体系总览见 [Testing.md](Testing.md)。

## Vendor 结构

```text
third_party/googletest/
├── LICENSE
├── googletest/            gtest 核心（已编译）
│   ├── include/gtest/     公共头文件
│   └── src/               gtest-all.cc（聚合 TU）、gtest_main.cc、分体 .cc
└── googlemock/            gmock（已 vendor，未编译）
```

精简原则与 spirv-cross、imgui 一致：只保留编译所需源码，上游的 test/、samples/、docs/、CI 配置不入库。升级版本时重新克隆上游 tag，覆盖 `googletest/` 与 `googlemock/` 两个子目录即可。

## MiniGTest 静态库

`tests/CMakeLists.txt` 把两个聚合翻译单元编成 `MiniGTest`，不使用上游自带的 CMake 工程（避开安装规则、包导出和 gtest 自己的测试套件）：

- `gtest-all.cc` 以 `#include "src/gtest-*.cc"` 聚合全部实现，编译一个 TU 即可；这些相对包含要求 `googletest/` 根目录位于 **PRIVATE** include 路径。
- `gtest_main.cc` 提供 `main()`（`RUN_ALL_TESTS`），测试文件不再手写入口。
- 公共头经 **SYSTEM PUBLIC** 暴露：测试目标链接 `MiniGTest` 即获得 include 路径，且 gtest 头不参与本项目的 `-Wall -Wextra -Wpedantic` 告警。
- WinLibs/MinGW 下无需额外线程库：gtest 检测到 Windows 后走 Win32 线程原语（`GTEST_HAS_PTHREAD=0`），`std::thread`/`std::mutex` 依赖的 winpthread 已由 `mini_link_mingw_runtime` 静态提供。

需要 gmock 时，把 `googlemock/src/gmock-all.cc` 加入 `MiniGTest` 源列表并将 `googlemock/include` 加进 SYSTEM PUBLIC 即可，源码已在库内。

## CTest 集成

`mini_add_gtest` 用 `gtest_discover_tests` 注册：构建后运行 `<target> --gtest_list_tests` 枚举用例，每个 `TEST()` 成为独立 CTest 条目，命名 `Suite.Case`（如 `HashTest.FnvVectorsMatch`、`HashFileTest.FileHashMatchesMemoryHash`）。

由此产生的要求与收益：

- 测试二进制必须能在构建机上直接启动（discover 阶段会执行它）；枚举阶段只列出用例名、不运行测试体，因此不依赖资产环境。
- `ctest -R` 可精确到用例级：`ctest -R "HashFileTest"`。
- 二进制支持全部 gtest 命令行参数，调试时直接运行更灵活：`HashTest.exe --gtest_filter="HashTest.*" --gtest_repeat=10`。

## 用例组织

- 逻辑独立的检查各写一个 `TEST(Suite, Case)`，替代旧模式的线性 `return N` 链条；用例间失败互不传染，一次运行暴露全部问题。
- 共享 setup/teardown 用 `TEST_F` fixture：`SetUp`/`TearDown` 自动配对；`SetUp` 中 `ASSERT_*` 失败会跳过测试体但**仍执行 `TearDown`**，清理路径因此必须容忍未初始化状态（见 [Testing.md](Testing.md) 已知陷阱）。
- 断言分级：`EXPECT_*` 记录失败后继续；`ASSERT_*` 立即终止当前用例，仅用于后续断言依赖的前置条件（如指针非空）。
- 浮点用 `EXPECT_FLOAT_EQ`/`EXPECT_NEAR`；容器与字符串可直接 `EXPECT_EQ`，失败时打印内容差异。

参考实例：`tests/HashTest.cpp`（TEST + TEST_F fixture + 文件系统环境）。

## 死亡测试

断言"进程应当退出"的行为（如 `Log::fatal`）用 `EXPECT_EXIT`，所在套件名以 `DeathTest` 结尾：

```cpp
TEST(LogDeathTest, FatalExitsWithFailureCode) {
    EXPECT_EXIT(engine::Log::fatal("LogTest", "Fatal exits the process"),
                ::testing::ExitedWithCode(EXIT_FAILURE),
                "Fatal exits the process");
}
```

机制与注意点：

- gtest 启动子进程重跑同一个二进制（带内部过滤参数只执行该语句），父进程断言退出码与 stderr。Windows 上经 `CreateProcess` 实现，无 fork 语义问题。
- 第三个参数是 matcher：当前只编译了 gtest，**没有 gmock 的 `HasSubstr`**；直接传字符串字面量，gtest 按正则（`ContainsRegex`）匹配 stderr 全部内容。消息含正则元字符时需转义，或引入 gmock 后改用 `HasSubstr`。
- 死亡测试替代旧的"命令行参数 + `add_test` + `WILL_FAIL TRUE`"模式：旧模式只能断言"非零退出"，无法区分退出原因，也不能检查输出。

参考实例：`tests/LogTest.cpp`。
