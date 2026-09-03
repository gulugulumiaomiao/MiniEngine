# 日志系统

引擎统一通过 `core/Log.h` 输出日志，不再由引擎代码主动抛出 C++ 异常。

## 日志级别

| 级别 | 颜色 | 输出流 | 行为 |
|---|---|---|---|
| `info` | 绿色 | 标准输出 | 普通运行信息 |
| `debug` | 青色 | 标准输出 | 调试信息 |
| `warn` | 黄色 | 标准错误 | 警告，程序继续运行 |
| `error` | 红色 | 标准错误 | 错误，程序继续运行 |
| `fatal` | 粗体紫色 | 标准错误 | 输出后刷新流并以失败状态退出程序 |

所有级别采用相同格式：

```text
[日志所在类]: "日志信息"
```

例如：

```cpp
#include "core/Log.h"

engine::Log::info("Renderer", "renderer initialized");
engine::Log::debug("Material", "uniform data uploaded");
engine::Log::warn("Shader", "optional pass was not found");
engine::Log::error("Texture", "texture upload failed");
engine::Log::fatal("VulkanBackend", "failed to create Vulkan device");
```

消息支持 `printf` 风格的格式化参数：

```cpp
engine::Log::info("Renderer", "draw count: %u", drawCount);
engine::Log::debug("Material", "name: %s, version: %llu",
                   name.c_str(), static_cast<unsigned long long>(version));
engine::Log::error("VulkanBackend", "VkResult: %d", static_cast<int>(result));
```

格式参数遵循 C `printf` 规则，参数类型必须与 `%d`、`%u`、`%f`、`%s` 等占位符匹配；`std::string` 需要使用 `.c_str()` 传给 `%s`。不带格式参数的原有字符串接口仍然保留。

`fatal` 调用 `std::exit(EXIT_FAILURE)`，不会进行 C++ 异常的栈展开，因此不要依赖局部对象析构完成关键持久化操作。它只应处理无法恢复、程序不能继续运行的错误；可恢复错误应使用返回值配合 `warn` 或 `error`。

日志写入由互斥锁保护，避免多线程输出互相穿插。在 Windows 控制台中会自动开启 ANSI 颜色支持。

## 异常边界

引擎公开加载与编译接口不会向调用方抛出异常。JSON 解析依赖的 `nlohmann/json`、反射依赖的 SPIRV-Cross 可能在库边界内部报告异常；边界会将错误转换为 `error` 日志和 `nullptr`/无效 Handle，不会传播，也不会调用 `fatal`。
