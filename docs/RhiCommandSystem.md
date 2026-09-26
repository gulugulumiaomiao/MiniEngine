# RHI 命令系统与最小 RenderGraph

本文记录第一版 RHI 命令抽象的设计和实现。目标是让 Renderer 不再直接调用 Vulkan 绘制命令，同时保留 Vulkan 显式 API 的可控性，为后续材质多 Pass、阴影、后处理和其他图形后端留下稳定边界。

## 最终分层

```text
Scene / Node / Component
    │ Scene 从 Root 提取
    ▼
RenderScene
    │ Renderer 解析、排序
    ▼
DrawList（后端无关的绘制包）
    │
    ▼
RenderGraph（Pass、附件、资源状态）
    │ 调用 rhi:: 命令自由函数（首参为 command buffer 的 RID）
    ▼
VulkanCommandBuffer（设备单例按 RID 解析）
    │ 解析带 generation 的 RHI handle
    ▼
VkCommandBuffer / Vulkan 资源
```

上层只能看到 `rhi::RID` 句柄，不能看到 `VkBuffer`、`VkPipeline` 或 `VkDescriptorSet`，也看不到任何 RHI 资源接口类——`IBuffer`/`IRHITexture`/`IRHITextureView`/`IRHISampler`/`IShaderModule`/`ICommandBuffer` 均已删除，只保留 `Vulkan*` 实现类。`rhi/api/` 只剩三个头：`Device.h`（`IDevice` 单例 + `ISwapchain`）、`Command.h`（命令自由函数）、`ResourceDesc.h`（全部描述符/枚举/POD）。`VulkanDevice` 按对象类型分开持有存实例的 `HandlePool`，以 `<type>_<op>`（`_create`/`_destroy`/`_upload`/`_allocate_rid`/`_release_rid`）操作族管理，各自定位自己的池。原生对象只允许在 Vulkan 后端出现。

## 七步实施记录

### 1. 定义 RHI 基础类型

`src/rhi/api/RhiTypes.h` 定义：

- 带 `index + generation` 的 Buffer、Texture、TextureView、GraphicsPipeline 和 BindGroup 句柄；
- `Viewport`、`Rect`、`RenderingInfo` 和颜色/深度附件；
- `DrawArguments`、`DrawIndexedArguments` 和 `BufferCopy`；
- `ResourceState` 与 `TextureBarrier`。

句柄的 generation 在资源销毁或复用时变化，后端解析句柄时会拒绝过期资源，避免静默访问已经释放的 GPU 对象。

### 2. 定义命令自由函数

`src/rhi/api/Command.h` 定义一组命令自由函数（不再有 `ICommandBuffer` 抽象接口），每个函数首参是 command buffer 的 `RID`，覆盖：

- 屏障、动态渲染、viewport、scissor、pipeline、VB/IB、bind group、draw 和 debug label；
- 传输命令：`copyBuffer`、`copyImage`、`copyBufferToImage`、`copyImageToBuffer`、`updateBuffer`、`updateImage`。

这些函数只在 Vulkan 后端（`src/rhi/vulkan/VulkanCommand.cpp`）定义：函数体经 `IDevice::active()` 拿到设备单例，按 RID 解析出 `VulkanCommandBuffer` 实例后转发。`Command.h` 不含任何 Vulkan 类型，因此渲染层可以包含它而不违反“渲染层禁止依赖 Vulkan”的边界（`SubmitSync`/`CommandState` 含 Vk 类型，已下沉到 Vulkan 层；提交与命令缓冲分配是后端职责，不在 `IDevice` 上）。

`copy*` 系列直接映射 Vulkan 的对应命令，且与 `vkCmdCopy*` 一样**自身不做布局转换**：执行 copy 时纹理必须已处于 `CopySource`/`CopyDestination` 状态。布局转换由调用方通过 `resourceBarriers` 显式完成，`TextureBarrier` 携带 `baseMipLevel/mipCount/baseArrayLayer/layerCount` 子资源范围（`kRemainingMipLevels`/`kRemainingArrayLayers` 哨兵对应 Vulkan 的 REMAINING 语义），因此可以精确转换被 copy 的那个 mip/layer。`updateBuffer`/`updateImage` 在 Vulkan 中没有任意尺寸的原生等价命令，由后端先把数据写入 host-visible 的 scratch buffer，再录制一次 copy。因此 scratch buffer 必须活到命令缓冲执行完毕，其生命周期由 `VulkanDevice` 管理：`endFrame` 提交前把待回收的 scratch buffer 打上本帧 fence，下一次 `beginFrame` 等待 fence 后销毁；设备析构时 `waitIdle` 兜底。

接口描述渲染意图，不复制 Vulkan 的创建流程。`ISwapchain` 负责帧 acquire/present，
其 Vulkan 实现内部管理命令缓冲、队列提交、fence 和 semaphore。

### 3. 实现 Vulkan command buffer 和资源解析

`src/rhi/vulkan/VulkanCommandBuffer.*` 完成 RHI 到 Vulkan 的映射：

- `ResourceState` 转换为 stage、access mask 和 image layout；
- `RenderingInfo` 转换为 Vulkan 1.3 Dynamic Rendering；
- RHI 句柄通过 `VulkanDevice::resolve*`（resolveBuffer/resolveTexture/resolveTextureView/resolveSampler/resolveShader/resolvePipeline/resolveBindGroup）转换为原生对象；
- Debug 构建使用 `VK_EXT_debug_utils` 标记 Pass，Release 构建不编译这些调用；
- command buffer 记住当前 pipeline layout，从而安全绑定 descriptor set。

资源解析按职责拆分：`VulkanDevice` 创建并拥有 Vulkan instance、surface、physical/logical
device、queue、allocator、descriptor pool 和 command pool，同时管理并校验 Buffer、Shader、
GraphicsPipeline、BindGroupLayout 与 BindGroup 句柄；`VulkanSwapchain` 管理交换链图片、
命令缓冲、逐帧同步与提交，并为 command buffer 解析当前 back buffer。

### 4. 迁移绘制命令

`Renderer::recordDrawCommands` 不直接调用以下操作：

```text
vkCmdBeginRendering / vkCmdEndRendering
vkCmdSetViewport / vkCmdSetScissor
vkCmdBindPipeline
vkCmdBindVertexBuffers / vkCmdBindIndexBuffer
vkCmdBindDescriptorSets
vkCmdDrawIndexed
vkCmdPipelineBarrier
```

这些操作全部经过 `rhi::` 命令自由函数（首参为 command buffer 的 RID）。begin/end 生命周期、命令缓冲分配与提交都是后端职责：`VulkanDevice::command_buffer_create`/`submit` 负责分配与提交，交换链每帧命令缓冲也注册进设备命令缓冲池并以 RID 暴露（`ISwapchain::commandBuffer()` 返回 RID）。

### 5. Renderer 构建 DrawList

`src/render/renderer/DrawList.h` 定义 `DrawItem`。每项包含 pipeline、VB、IB、索引格式、indexed draw 参数和 render queue。

`Renderer::renderFrame` 遍历 `RenderScene`，通过 `MeshGpuCache` 获取 Mesh 绘制信息，并按照 SubMesh 的 material slot 从 RenderObject 材质列表选择材质。Renderer 为每个 SubMesh 生成 DrawItem，并按 render queue 稳定排序。`firstInstance` 保留原 RenderObject 下标，因此排序后 shader 仍会读取正确的对象数据。Scene UBO 保存相机和方向光，Object SSBO 保存模型矩阵。

当前只按 render queue 排序。下一版可增加 pipeline、material 和 mesh 排序键，以减少状态切换，同时必须保持透明物体的深度排序规则。

### 6. 迁移 GPU buffer copy

Mesh 上传的 staging buffer 和 device-local buffer 都进入带 generation 的 buffer 资源表。`buffer_upload` 内部通过 `VulkanDevice::command_buffer_create` 创建一次性 command buffer，
录制 `rhi::copyBuffer`（Vulkan 后端实现），不再直接调用 `vkCmdCopyBuffer`。

第一版仍会 `vkQueueWaitIdle`，实现简单且资源生命周期明确。资源批量加载后应改为 upload context：持久 command pool、批量 copy、timeline semaphore 和延迟释放 staging buffer。

### 7. 接入最小 RenderGraph

`src/render/render_graph/RenderGraph.*` 当前负责：

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
  → 选择逐帧场景/材质 BindGroup
  → 上传 Renderer 已提取到 DrawList 的对象数据快照
  → Renderer 已构建好的 DrawList 交给后端
  → begin command buffer
  → RenderGraph 生成附件屏障并执行 Forward Pass
  → RHI command buffer 录制绑定和 drawIndexed
  → RenderGraph 转换到 Present
  → end、submit、present
```

## 扩展规则

新增上层绘制能力时，按以下顺序判断：

1. 如果是通用图形动作，例如 indirect draw、push constants，加入 RHI command buffer；
2. 如果是资源创建或生命周期，加入后端资源接口和句柄表，不加入 command buffer；
3. 如果是 Pass 依赖、附件或资源状态，加入 RenderGraph；
4. 如果是“画哪些对象以及顺序”，加入 DrawList 构建阶段；
5. 如果只是 Vulkan 特有优化，留在 Vulkan 实现内部，不泄漏到 Renderer。

## 当前限制与下一步

- RenderGraph 只处理导入纹理和图形 Pass，尚未创建临时纹理，也没有 buffer barrier；
- 每个 Pass 当前最多一个深度附件，RenderTarget 已支持 `Depth32Float`；
- Mesh 当前仍只有一个活动 VertexLayout；
- BindGroup 已拆分为 Scene/Object（set 0）与 Material（set 1），Object 目前仍与 Scene 共用 set；
- 未实现 compute encoder、indirect draw、push constants 和 secondary command buffer；
- 资源销毁会等待 device idle，后续应加入按 frame/timeline 回收的 deferred deletion queue。

下一阶段可继续拆分 Object 更新频率，并扩展 RenderGraph 临时纹理；这样即可自然接入
ShaderLab 的 `ShadowCaster → Forward → 后处理` 多 Pass 链路。

## 验证

命令录制只能在真实设备上验证（命令自由函数只转发到 Vulkan 后端，无法被 mock 拦截）。`tests/RhiDeviceTest.cpp` 使用真实 headless `VulkanDevice`（无 Vulkan 运行时则跳过）验证：

- 各类型资源的 create/destroy 与 RID 有效性、sampler/view 的设备级去重；
- 命令缓冲的 begin → barrier → beginRendering/endRendering → end → submit → waitIdle 全链路无致命错误、无校验层报错。

`RenderTarget` 的 CPU 侧结构由 `tests/RenderTargetTest.cpp` 覆盖；端到端帧录制（含 RenderGraph 执行与 ImGui overlay）由 `tests/SceneViewIntegrationTest.cpp` 在真实设备上覆盖。

运行：

```powershell
cmake --build --preset clang-debug
ctest --test-dir build/clang-debug --output-on-failure
cmake --build --preset clang-release
ctest --test-dir build/clang-release --output-on-failure
```
