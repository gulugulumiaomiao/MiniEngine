# 单例基类

全局服务统一继承 `core/base/Singleton.h` 中的 CRTP 基类：

```cpp
class AssetManager final : public Singleton<AssetManager> {
    friend class Singleton<AssetManager>;

private:
    AssetManager() = default;
};
```

`Singleton<T>::instance()` 使用函数内静态对象，保持延迟初始化和 C++11 起保证的线程安全初始化。基类统一删除拷贝、移动构造和赋值；派生类通过私有构造函数及 friend 声明，禁止外部直接实例化。

这是静态多态的生命周期工具，不使用虚函数，也不允许通过基类指针销毁对象。各服务仍负责自己的 `initialize()`、`shutdown()` 或 `clear()`，单例基类只保证唯一实例。

当前继承该基类的服务：

- `FileSystem`
- `AssetDatabase`
- `AssetImportPipeline`
- `FileWatcher`
- `AssetManager`
- `ShaderManager`
- `MaterialManager`

现有调用方式和宏保持不变，例如 `AssetManager::instance()` 与 `ASSET_MANAGER`。
