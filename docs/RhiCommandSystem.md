# RHI 命令系统与最小 RenderGraph

本文记录第一版 RHI 命令抽象的设计和实现。目标是让 Renderer 不再直接调用 Vulkan 绘制命令，同时保留 Vulkan 显式 API 的可控性，为后续材质多 Pass、阴影、后处理和其他图形后端留下稳定边界。

## 最终分层

```text
RenderScene
    │ Renderer 提取、排序
    ▼
DrawList（后端无关的绘制包）
    │
    ▼
RenderGraph（Pass、附件、资源状态）
    │ 调用 IGraphicsCommandEncoder
    ▼
VulkanGraphicsCommandEncoder
    │ 解析带 generation 的 RHI handle
    ▼
VkCommandBuffer / Vulkan 资源
```

上层只能看到 `rhi::BufferHandle`、`GraphicsPipelineHandle` 等句柄，不能看到 `VkBuffer`、`VkPipeline` 或 `VkDescriptorSet`。原生对象只允许在 Vulkan 后端和 Vulkan encoder 中出现。

## 七步实施记录

### 1. 定义 RHI 基础类型

`src/rhi/RhiTypes.h` 定义：

- 带 `index + generation` 的 Buffer、Texture、TextureView、GraphicsPipeline 和 BindGroup 句柄；
- `Viewport`、`Rect`、`RenderingInfo` 和颜色/深度附件；
- `DrawArguments`、`DrawIndexedArguments` 和 `BufferCopy`；
- `ResourceState` 与 `TextureBarrier`。

句柄的 generation 在资源销毁或复用时变化，后端解析句柄时会拒绝过期资源，避免静默访问已经释放的 GPU 对象。

### 2. 定义命令编码接口

`src/rhi/CommandEncoder.h` 将接口拆为两类：

- `IGraphicsCommandEncoder`：屏障、动态渲染、viewport、scissor、pipeline、VB/IB、bind group、draw 和 debug label；
- `ITransferCommandEncoder`：第一版只提供 `copyBuffer`。

接口描述渲染意图，不复制 Vulkan 的创建流程。队列提交、fence、semaphore、swapchain acquire/present 仍由后端负责。

### 3. 实现 Vulkan encoder 和资源解析

`src/rhi/vulkan/VulkanCommandEncoder.*` 完成 RHI 到 Vulkan 的映射：

- `ResourceState` 转换为 stage、access mask 和 image layout；
- `RenderingInfo` 转换为 Vulkan 1.3 Dynamic Rendering；
- RHI 句柄通过 `IVulkanResourceResolver` 转换为原生对象；
- Debug 构建使用 `VK_EXT_debug_utils` 标记 Pass，Release 构建不编译这些调用；
- encoder 记住当前 pipeline layout，从而安全绑定 descriptor set。

资源解析集中在 `VulkanBackend`。当前 swapchain 图片、单 graphics pipeline、逐帧 descriptor set 和 buffer 资源表都具备句柄校验。

### 4. 迁移绘制命令

`VulkanBackend::recordDrawCommands` 不再直接调用以下操作：

```text
vkCmdBeginRendering / vkCmdEndRendering
vkCmdSetViewport / vkCmdSetScissor
vkCmdBindPipeline
vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
vkCmdBindDescriptorSets
vkCmdDrawIndexed
vkCmdPipelineBarrier
```

这些操作全部经过 `IGraphicsCommandEncoder`。后端仍直接 begin/end `VkCommandBuffer`，因为 command buffer 分配、提交与同步属于后端队列职责。

### 5. Renderer 构建 DrawList

`src/renderer/DrawList.h` 定义 `DrawItem`。每项包含 pipeline、VB、IB、索引格式、indexed draw 参数和 render queue。

`Renderer::renderFrame` 遍历 `RenderScene`，通过 `IRenderBackend::meshDrawInfo` 获取 Mesh 绘制信息，并通过引擎层 `MaterialManager` 获取材质属性和 render queue。Renderer 为每个 SubMesh 生成 DrawItem，并按 render queue 稳定排序。`firstInstance` 保留原 RenderObject 下标，因此排序后 shader 仍会读取正确的对象数据。

当前只按 render queue 排序。下一版可增加 pipeline、material 和 mesh 排序键，以减少状态切换，同时必须保持透明物体的深度排序规则。

### 6. 迁移 GPU buffer copy

Mesh 上传的 staging buffer 和 device-local buffer 都进入带 generation 的 buffer 资源表。`uploadBuffer` 通过 `VulkanTransferCommandEncoder::copyBuffer` 录制，不再直接调用 `vkCmdCopyBuffer`。

第一版仍会 `vkQueueWaitIdle`，实现简单且资源生命周期明确。资源批量加载后应改为 upload context：持久 command pool、批量 copy、timeline semaphore 和延迟释放 staging buffer。

### 7. 接入最小 RenderGraph

`src/renderer/RenderGraph.*` 当前负责：

- 导入外部 Texture，并声明 initial/final state；
- 声明 Graphics Pass 的 RenderingInfo 和资源用途；
- 在 Pass 前生成需要的状态转换；
- 在所有 Pass 后把导入资源转换到 final state；
- 包围 begin/end rendering 和 Debug Label。

当前 Forward Pass 把 swapchain 图片从 `Undefined`（首次使用）或 `Present` 转为 `ColorAttachment`，执行 DrawList 后再转回 `Present`。swapchain resize 会递增 generation，使旧 Texture/View handle 立即失效。

## 一帧执行顺序

```text
等待当前 FrameContext fence
  → acquire swapchain image
  → reset/更新逐帧 descriptor
  → 上传 Renderer 已提取到 DrawList 的对象数据快照
  → Renderer 已构建好的 DrawList 交给后端
  → begin VkCommandBuffer
  → RenderGraph 生成附件屏障并执行 Forward Pass
  → RHI encoder 录制绑定和 drawIndexed
  → RenderGraph 转换到 Present
  → end、submit、present
```

## 扩展规则

新增上层绘制能力时，按以下顺序判断：

1. 如果是通用图形动作，例如 indirect draw、push constants，加入 RHI encoder；
2. 如果是资源创建或生命周期，加入后端资源接口和句柄表，不加入 command encoder；
3. 如果是 Pass 依赖、附件或资源状态，加入 RenderGraph；
4. 如果是“画哪些对象以及顺序”，加入 DrawList 构建阶段；
5. 如果只是 Vulkan 特有优化，留在 Vulkan 实现内部，不泄漏到 Renderer。

## 当前限制与下一步

- RenderGraph 只处理导入纹理和图形 Pass，尚未创建临时纹理，也没有 buffer barrier；
- 每个 Pass 当前最多一个深度附件；
- pipeline 仍只有一个活动 VertexLayout，后续需要 PipelineCache；
- BindGroup 第一版对应当前帧的场景 descriptor，尚未拆分 Scene/Material/Object 频率；
- 未实现 compute encoder、indirect draw、push constants 和 secondary command buffer；
- 资源销毁会等待 device idle，后续应加入按 frame/timeline 回收的 deferred deletion queue。

建议下一阶段先实现 PipelineCache 与 Scene/Material/Object 三层 BindGroup，再扩展 RenderGraph 的深度附件和临时纹理；这样即可自然接入 ShaderLab 的 `ShadowCaster → Forward → 后处理` 多 Pass 链路。

## 验证

`tests/RenderGraphTest.cpp` 使用 Mock encoder 验证：

- Pass 执行顺序为 label、barrier、begin rendering、callback、end rendering、end label；
- 首次屏障为 `Undefined → ColorAttachment`；
- 最终屏障为 `ColorAttachment → Present`。

运行：

```powershell
cmake --build --preset clang-debug
ctest --test-dir build/clang-debug --output-on-failure
cmake --build --preset clang-release
ctest --test-dir build/clang-release --output-on-failure
```
