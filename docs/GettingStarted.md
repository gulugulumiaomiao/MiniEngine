# 在新电脑上构建和调试

项目的第三方 C++ 库已经放在 `third_party/`，克隆时不需要 Git Submodule，也不在
CMake 配置阶段下载依赖。系统仍需要 Windows 10/11、支持 Vulkan 1.3 的显卡驱动、
CMake、Ninja、WinLibs Clang 和 Vulkan SDK。

## 首次准备

安装 Git 后克隆仓库，用 PowerShell 在项目根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/dev/Setup-Windows.ps1
```

脚本通过 WinGet 安装经过项目验证的工具包，并安装 VS Code 推荐扩展。安装完成后
重启 VS Code，让它读取新的系统环境。

## VS Code 调试

1. 用 VS Code 打开仓库根目录。
2. 接受扩展推荐。
3. 打开“运行和调试”。
4. 选择 `Debug MiniVulkanEngine (CodeLLDB)`。
5. 按 F5。

F5 会依次调用仓库中的 `tools/dev/Invoke-CMake.ps1`、配置 `clang-debug` Preset、
构建 `MiniVulkanEngine.exe`，最后由 CodeLLDB 启动程序。工具发现脚本不会保存用户名、
SDK 版本或 WinGet 缓存绝对路径，因此仓库可以在不同 Windows 用户目录中工作。

调试编辑器选 `Debug MiniEditor (CodeLLDB)`（Release 选 `Run MiniEditor Release`）：
任务会先构建 `MiniEditor` 目标及其内嵌的 UI Shader，然后启动
`build/clang-debug/MiniEditor.exe`。

## 命令行验证

不修改当前 PowerShell 的 PATH 也可以直接执行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/dev/Invoke-CMake.ps1 --preset clang-debug
powershell -ExecutionPolicy Bypass -File tools/dev/Invoke-CMake.ps1 --build --preset clang-debug --parallel
./build/clang-debug/MiniVulkanEngine.exe
```

Release 将 `clang-debug` 改为 `clang-release`：行为与 Debug 完全一致（含验证层和热重载），仅开启编译优化。发布版使用 `clang-publish`，它以 Release 级优化编译并启用发布行为：只读取打包后的 AssetDatabase、Artifact 和预编译 SPV，不启动验证层、FileWatcher 和运行时 Shader 编译。

`MiniEditor` 目标在三个配置下都会构建，但只有 Debug 与 Release 可用：Publish 的
资产系统是 Packaged 模式，只读烘焙后的 AssetDatabase 与 Artifact，而项目 `assets/`
里放的是源 JSON：

```powershell
powershell -ExecutionPolicy Bypass -File tools/dev/Invoke-CMake.ps1 --build --preset clang-debug --target MiniEditor --parallel
./build/clang-debug/MiniEditor.exe
```

首次启动会在 `build/clang-debug/editor/config/` 下生成 `editor.json` 与 `imgui.ini`，
并弹出项目选择框；新建或选定的项目目录会自己持有 `assets/`、`library/` 和
`generated-shaders/`。详见[编辑器文档](Editor.md)。

## 仓库内容约定

会提交：

- `src/`、`builtin/`、`config/`、`docs/`、`tests/`、`tools/`
- `third_party/` 固定版本依赖
- `.vscode/`、`CMakeLists.txt`、`CMakePresets.json`

不会提交：

- `build*` 构建目录
- `library/` 导入缓存和 Artifact
- `generated-shaders/` 运行时与打包 Shader 产物
- 日志、IDE 用户配置和临时 Shader 产物
