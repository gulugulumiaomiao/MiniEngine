# 文件系统

文件系统位于 `src/core/io`，负责虚拟路径、挂载点和原始文件读写。它不解析 Shader、Material 或 Mesh，也不缓存 Asset。

```text
MaterialManager → AssetManager → FileSystem → DirectoryMount → 操作系统文件
```

## 虚拟路径

`VirtualPath` 使用 `scheme://relative/path`：

```text
asset://shaders/vertex_color.shader.json
engine://textures/white.png
cache://shaders/vertex_color.vert.spv
user://settings.json
```

Scheme 会转成小写，分隔符统一为 `/`，`.` 和可安全消解的 `..` 会被规范化。试图通过 `..` 逃出挂载根目录的路径无效。`file://` 由 `VirtualPath::fromNative()` 生成，用来兼容工具和旧接口传入的绝对路径。

## 挂载

`IFileMount` 是存储后端接口，第一版实现了 `DirectoryMount`。挂载点可以只读，也可以被同 Scheme 的新挂载替换：

```cpp
FILE_SYSTEM.mountDirectory("asset", assetRoot, true);
FILE_SYSTEM.mountDirectory("cache", cacheRoot, false);
```

以后增加 Pak 时只需要实现新的 `IFileMount`，`AssetManager` 和业务层不需要改变。

## 同步接口

第一版提供同步的 `exists`、`isFile`、`isDirectory`、`readText`、`readBinary`、`writeText`、`writeBinary`、`listFiles` 和 `resolvePhysicalPath`。普通读取失败返回 `std::nullopt` 并记录日志；写入失败返回 `false`。是否因关键资产缺失而退出由 AssetManager 等调用方决定。

## AssetManager 集成

`AssetManager::setAssetRoot()` 将根目录挂载为只读的 `asset://`。相对资产路径先转换为虚拟路径，再由 FileSystem 读取 JSON；解析函数只接收已经读取的文本。绝对路径转换成 `file://`，保留 Shader 编译工具和测试夹具的兼容性。

缓存键仍使用解析后的规范化物理路径，因此 `shaders/../shaders/a.json` 和 `shaders/a.json` 会命中同一个 Asset。
