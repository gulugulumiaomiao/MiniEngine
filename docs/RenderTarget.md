# RenderTarget 设计

`RenderTarget` 是 Render 层的离屏附件所有者。它创建并销毁一组 RHI Texture 与
TextureView，但不录制命令、不决定 Pass 顺序，也不暴露 Vulkan `VkImage`、`VkImageView` 或
Framebuffer。

```text
Renderer / Render Feature
    │ 描述尺寸、格式、clear/load/store
    ▼
RenderTarget                    拥有 Texture + TextureView，处理 resize
    │ RenderingInfo / ResourceUsage / ImportedTexture
    ▼
RenderGraph                     决定 Pass 顺序与资源状态转换
    │
    ▼
RHI Dynamic Rendering          Vulkan 后端映射 image usage、aspect 与 layout
```

## 职责边界

`RenderTarget` 负责：

- 一个或多个颜色附件（MRT）与最多一个深度附件；
- 每个附件的格式、额外 usage、load/store op 和 clear value；
- Texture/View 的创建、先销毁 View 再销毁 Texture 的释放顺序；
- 保持尺寸一致，并在 `resize()` 时以新资源替换旧资源；
- 保存跨 RenderGraph 实例的最后资源状态。

`RenderTarget` 不负责：

- Pass 排序、读写依赖分析和 barrier 录制，这些属于 `RenderGraph`；
- swapchain 图片的所有权，swapchain 仍由 `ISwapchain` 管理；
- GPU 同步。调用 `resize()` 或 `release()` 前，调用方必须确认旧附件已不再被 GPU 使用；
- 资产缓存。RenderTarget 是运行时渲染基础设施，不是可导入的 Texture 资产。

## 描述结构

```cpp
RenderTargetDesc desc;
desc.width = width;
desc.height = height;
desc.debugName = "GBuffer";
desc.colorAttachments = {
    {.format = rhi::TextureFormat::Rgba8Unorm,
     .additionalUsage = rhi::TextureUsage::Sampled},
    {.format = rhi::TextureFormat::Rgba8Unorm,
     .additionalUsage = rhi::TextureUsage::Sampled},
};
desc.depthAttachment = RenderTargetDepthAttachmentDesc{
    .format = rhi::TextureFormat::Depth32Float,
};
```

`ColorAttachment` 和 `DepthStencilAttachment` usage 由 RenderTarget 自动添加。调用方只在
`additionalUsage` 中声明后续采样或复制所需的 `Sampled`、`TransferSource`、
`TransferDestination`。

## 接入 RenderGraph

创建 Pass 时分三步使用：

```cpp
RenderGraph graph;
target.import(graph, rhi::ResourceState::ShaderRead,
              rhi::ResourceState::DepthAttachment);
graph.addGraphicsPass("GBuffer",
                      target.renderingInfo(),
                      target.writeUsages(),
                      recordDraws);
graph.execute(encoder);
```

- `import()` 声明附件的初始状态和整张图执行后的目标状态；
- `renderingInfo()` 生成 Dynamic Rendering 所需的 view、范围和清除参数；
- `writeUsages()` 声明颜色写入与深度写入状态；
- RenderGraph 执行后把 final state 回写给 RenderTarget，下一帧不会错误地再次使用
  `Undefined` 作为旧 layout。

从 `import()` 到 `execute()` 完成之前，不得 resize、release 或销毁 RenderTarget，因为
RenderGraph 会暂时引用其状态槽。

## Forward 路径

当前 Forward Pass 的颜色附件仍是 swapchain back buffer，深度附件来自 depth-only
RenderTarget。Renderer 为每个 in-flight frame 各持有一个深度目标；帧槽 fence 保证某个深度
目标再次使用前，上一轮使用已经完成。swapchain resize 在 `waitIdle()` 后同步重建这些深度附件。

GraphicsPipeline 的兼容键同时包含 color format 和 depth format，Vulkan Dynamic Rendering
通过 `VkPipelineRenderingCreateInfo` 声明二者，避免把无深度格式的 Pipeline 绑定到带深度附件的
Pass。

## 当前范围

第一版只支持 2D、单 mip、单层、单采样附件和 `Depth32Float`。后续增加 MSAA 时，应在 RHI
TextureDesc 中加入 sample count，并在 RenderTarget 描述中显式区分 multisample attachment 与
resolve attachment；不要在 RenderTarget 内隐式执行 resolve。
