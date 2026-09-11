# RenderPipeline 可编程渲染管线

本文记录 MiniSRP 第一阶段的实现：把原本硬编码在 `Renderer::renderFrame` 中的渲染流程迁移到可插拔的 `IRenderPipeline`，并以 `MiniForwardPipeline` 作为第一条管线保持行为等价。

## 目标

- 让 `Renderer` 只负责帧基础设施（swapchain、begin/end、resize、frameSerial）。
- 引入 `IRenderPipeline` 作为可编程渲染管线的抽象入口。
- 引入 `RenderContext` 作为每帧 `ScriptableRenderContext` 式的上下文门面。
- 通过 `RenderPipelineRegistry` 按名称注册/创建管线，`engine.json` 配置使用哪条管线。
- 保持现有 RHI、RenderGraph v1、ShaderLab、GPU Manager 分层不变。

## 架构分层

```text
Engine::loop
  → Scene::buildRenderScene → RenderScene 快照
  → Renderer::renderFrame
       beginFrame
       RenderContext 构造
       pipeline_->render(context)     ← 可编程入口
       endFrame / resize 处理
  → IRenderPipeline::render(RenderContext&)
       MiniForwardPipeline（行为等价）
  → RenderGraph v1 → RHI encoder → Vulkan
```

## 新增源码

```text
src/render/pipeline/
├── RenderPipeline.h/.cpp       IRenderPipeline 接口 + RenderPipelineRegistry
├── RenderContext.h/.cpp        帧上下文（device/encoder/swapchain/forward target/scene 门面）
└── MiniForwardPipeline.h/.cpp  第一条管线，把原 Renderer 渲染逻辑迁入
```

## 关键接口

### IRenderPipeline

```cpp
class IRenderPipeline {
public:
    virtual ~IRenderPipeline() = default;
    virtual void render(RenderContext& context) = 0;
    virtual void onSwapchainChanged() {}
};
```

### RenderContext

栈上每帧构造，封装 pass 所需的帧基础设施：
- `device()` / `swapchain()` / `encoder()`
- `currentForwardTarget()` —— 当前 in-flight 帧的 depth target
- `frameIndex()` / `frameSerial()`
- `scene()` —— 本帧 `RenderScene` 快照

### RenderPipelineRegistry

```cpp
class RenderPipelineRegistry {
public:
    void registerPipeline(std::string name, RenderPipelineFactory factory);
    std::unique_ptr<IRenderPipeline> create(const std::string& name) const;
};
```

工厂签名不接收 `Renderer&`，保证单测可以独立构造 stub pipeline。

## 配置

`config/engine.json` 新增 `render` 段：

```json
{
  "render": {
    "pipeline": "MiniForward"
  }
}
```

`EngineConfig` 新增 `RenderConfig` 结构解析该字段；空名称校验失败会 fatal。

## 改造点

### Renderer

- 移除 `recordDrawCommands`、`submitDrawList` 和 DrawList 构建逻辑。
- 新增 `setPipeline()`、`swapchain()`、`currentForwardTarget()`、`frameSerial()` 访问器。
- `renderFrame` 新流程：`refreshShaders → beginFrame → collect → context → pipeline_->render → endFrame → resize 检查`。
- `forwardTargets_` 仍由 `Renderer` 持有，后续阶段会视需要下放到管线。

### MiniForwardPipeline

- 把原 `Renderer::renderFrame` 中 L74-245 的 DrawList 构建、材质 resolve、`RenderGraph` 录制逻辑原样迁入。
- 通过 `RenderContext` 访问 swapchain、forward target、encoder，不再直接依赖 `Renderer` 私有状态。
- `onSwapchainChanged` 当前为空；forward target 的 resize 仍由 `Renderer::recreateSwapchain` 处理。

## 验证

- `RenderPipelineTest`：验证注册表可以注册、按名称创建 pipeline，且工厂被调用。
- 构建并运行 `MiniVulkanEngine`，确认 showcase 场景画面与改造前一致。
- `ctest --test-dir build/clang-debug --output-on-failure` 全绿。

## 后续阶段

- 阶段 B：把 `RenderGraph` 升级到 v2（临时纹理、编译期、池化）。
- 阶段 C：拆分 `MiniForwardPipeline` 为多个 `IRenderPass`，并引入 `RenderQueue` 子系统。
