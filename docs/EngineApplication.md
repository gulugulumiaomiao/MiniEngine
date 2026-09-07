# Engine 与 Application

## 职责边界

`Engine` 是唯一的 Runtime 与生命周期控制者。它负责：

- 初始化和关闭 FileSystem、AssetManager、Window、Renderer。
- 维护主循环、帧时间、退出状态、主 Scene 和当前 RenderScene 快照。
- 按固定顺序调用 Application 生命周期。

`Application` 是项目逻辑入口。它负责：

- 提供窗口名称、尺寸和 VSync 等 `AppConfig`。
- 在 `onStart()` 创建项目资源并提交初始场景。
- 在 `onUpdate(deltaTime)` 执行项目级更新。
- 在 `onStop()` 释放项目创建的资源。

Application 不拥有 Engine，也不实现主循环。Engine 只依赖抽象的 `Application`，不知道具体的 `GameApplication`。

## 生命周期

```text
main()
  -> 创建 GameApplication
  -> Engine::run(Application&)
       -> Engine::initialize(AppConfig)
       -> Application::onStart()
       -> while 未退出
            -> Window::pollEvents()
            -> AssetImportPipeline::processFileEvents()
            -> 计算 deltaTime
            -> Application::onUpdate(deltaTime)
            -> Scene::update(deltaTime)
            -> Scene::buildRenderScene(RenderScene, aspectRatio)
            -> Renderer::renderFrame(RenderScene)
       -> Application::onStop()
       -> Engine::shutdown()
```

生命周期回调是 `protected`，`Engine` 通过 friendship 调用它们。这保证外部代码不能绕过 Engine 随意触发启动或关闭。

## 新增另一种 Application

```cpp
class EditorApplication final : public Application {
public:
    AppConfig getConfig() const override {
        return {.name = "Mini Editor", .width = 1600, .height = 900};
    }

protected:
    void onStart() override;
    void onUpdate(float deltaTime) override;
    void onStop() override;
};

int main() {
    EditorApplication application;
    return ENGINE.run(application);
}
```

Game、Editor 和未来的其他 Application 可以共享同一个 Engine Runtime。Scene 是 Engine 承载的世界数据，不等同于 Application；一个 Application 以后可以通过 SceneManager 切换多个 Scene。

## AppConfig

当前第一版包含：

```cpp
struct AppConfig {
    std::string name{"Application"};
    std::uint32_t width{1280};
    std::uint32_t height{720};
    bool vsync{true};
};
```

`vsync=true` 使用 Vulkan FIFO Present Mode；关闭 VSync 时优先使用 Mailbox，不支持 Mailbox 的设备会回退到 FIFO。
