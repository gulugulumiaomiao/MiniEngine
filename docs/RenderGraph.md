# RenderGraph v2

本文记录 MiniSRP 第二阶段的实现：把 `RenderGraph` 从 v1 的纯运行时调度器升级为支持**声明式虚拟资源、编译期解析、临时纹理池化**的 v2。

## 目标

- 引入 `RgTextureHandle`，让 pass 在录制阶段引用虚拟纹理，而不是直接持有 `rhi::TextureHandle`。
- 支持 `createTexture()` 声明临时纹理，由 `RgTexturePool` 按描述复配并池化。
- 引入 `compile()` 阶段：把虚拟句柄解析为实际 RHI 纹理/视图，并构建可执行的 pass 列表。
- 保持 `execute()` 负责屏障生成、Rendering 开启和回调执行。
- `RgTexturePool` 独立管理临时纹理生命周期，不依赖 `TextureManager` / `TextureGpuManager`。

## 架构

```text
RenderGraph v2
  importTexture(external)   → RgTextureHandle
  createTexture(transient)  → RgTextureHandle
  addGraphicsPass(name, RgRenderingInfo, usages, callback)
  compile(RgTexturePool&)   → 解析 RgTextureHandle → rhi::TextureHandle/View
  execute(encoder)          → barriers + beginRendering + callbacks + final barriers（仅 imported 纹理）
  reset()                   → 释放临时纹理回池
```

`RgTexturePool` 按 `framesInFlight` 分桶（默认 2），每帧开始调用 `beginFrame(frameIndex)` 重置对应桶，保证临时纹理不会在 GPU 仍在读取时被复用。

## 新增源码

```text
src/render/render_graph/
├── RgTypes.h              RgTextureHandle / RgTextureDesc / RgResourceUsage / RgRenderingInfo
├── RgTexturePool.h/.cpp   跨帧纹理池
├── RenderGraph.h/.cpp     v2 API（import/create/compile/execute/reset）
```

## 关键接口

### 资源声明

```cpp
RenderGraph graph;
RgTextureHandle backBuffer = graph.importTexture({
    .texture = swapchain.currentTexture(),
    .view = swapchain.currentTextureView(),
    .initialState = swapchain.currentTextureState(),
    .finalState = ResourceState::Present,
    .aspect = TextureAspect::Color,
});

RgTextureHandle shadowMap = graph.createTexture({
    .format = TextureFormat::Depth32Float,
    .width = 1024,
    .height = 1024,
    .usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled,
    .aspect = TextureAspect::Depth,
    .debugName = "ShadowMap",
});
```

### Pass 声明

```cpp
RgRenderingInfo rendering;
rendering.renderArea = {0, 0, width, height};
rendering.depthAttachments.push_back({shadowMap, LoadOp::Clear, StoreOp::Store, 1.0F});

graph.addGraphicsPass("ShadowCaster",
                      std::move(rendering),
                      {{shadowMap, TextureAspect::Depth, ResourceState::DepthAttachment}},
                      [](IGraphicsCommandEncoder& encoder) { /* draw shadow casters */ });
```

### 编译与执行

```cpp
graph.compile(rgTexturePool);
graph.execute(encoder);
graph.reset();
```

## RgTexturePool

- 键值：`format + width + height + mipCount + usage`（不含 debugName）。
- 每个 `frameIndex % framesInFlight` 桶独立管理；`beginFrame` 重置当前桶。
- `acquire()` 优先复用同帧同描述的空闲条目，否则创建新的 `Texture + TextureView`。
- `release()` 标记条目空闲；池析构时统一销毁所有纹理与视图。

## Transient 纹理与 final barrier 语义

`execute()` 结束时只对 **imported** 纹理发 final barrier（转换到声明的 `finalState`，例如 backbuffer 的 `Present`）。transient 纹理的 final barrier 被跳过：它们随 `reset()` 回池，保持最后一次使用的状态；下一帧被 `acquire()` 复用时，首个 usage barrier 负责转换到所需状态。跳过的原因是 Vulkan 禁止把 barrier 的 newLayout 指定为 `VK_IMAGE_LAYOUT_UNDEFINED`（VUID-VkImageMemoryBarrier-newLayout-01198），而 transient 纹理没有有意义的「最终状态」。

编译后管线可通过 `resolvedTextureView(RgTextureHandle)` 拿到 transient 纹理解析出的实际 `TextureViewHandle`（例如把 shadow map 绑进 scene bind group）；该句柄仅在 `compile()` 之后、`reset()` 之前有效。

## 改造点

### RenderTarget

- `importColor()` / `importDepth()` 现在返回 `RgTextureHandle`。
- 移除 `writeUsages()`：调用方通过返回的 handle 自行构建 `RgResourceUsage`。
- 继续保留 `renderingInfo()`（返回 `rhi::RenderingInfo`），用于非 RG 的直接 RHI 使用。

### MiniForwardPipeline

- `recordDrawCommands` 改用 `RgRenderingInfo` 与 `RgResourceUsage`。
- 后缓冲与 forward depth 均通过 `importTexture` / `importDepth` 获得 handle。
- 在 `execute()` 前后分别调用 `compile(context.rgTexturePool())` 与 `graph.reset()`。

### Renderer / RenderContext

- `Renderer` 持有 `std::optional<RgTexturePool>`，在 RHI 上下文校验后构造。
- 每帧 `renderFrame()` 调用 `rgTexturePool_->beginFrame(swapchain_->frameIndex())`。
- `RenderContext` 暴露 `rgTexturePool()` 供管线访问。

## 验证

- `RenderGraphTest`：验证 import 路径的屏障序列与 transient 路径的池分配。
- `RenderGraphTest`（shadow map 场景）：`Depth32Float`（`DepthStencilAttachment | Sampled`）纹理经 `DepthAttachment` 写入后由后续 pass 以 `ShaderRead` 读取，断言 `Undefined -> DepthAttachment -> ShaderRead` 两道屏障、transient 纹理无 final barrier、纹理 usage 含 `Sampled`。
- `RenderTargetTest`：验证 `importColor` / `importDepth` 返回 handle 后图能正确执行。
- `RgTexturePoolTest`：验证同帧复用、不同描述新建、跨帧桶隔离、两帧后复用。
- 构建并运行 `MiniVulkanEngine`，确认 showcase 场景画面与阶段 A 一致。
- `ctest --test-dir build/clang-debug --output-on-failure` 全绿。

## 后续阶段

- 阶段 C：拆分 `MiniForwardPipeline` 为多个 `IRenderPass`，并引入 `RenderQueue` 子系统。
