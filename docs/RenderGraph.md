# 持久化 RenderGraph

RenderGraph 保存逻辑纹理、pass 声明和编译计划，可以跨帧执行。正常帧只准备物理资源、录制命令；只有 pass、附件关系、格式、采样数或最终状态约定变化时重新编译。

## 生命周期

```text
声明 import/create/pass → compile(context) → 持久化逻辑计划
每帧 beginFrame → updateFrame → prepareFrame(context) → execute(context, frame)
                                                    → frame 析构释放纹理租约
结构变化 → reset → 重新声明 → compile
格式/采样数变化 → invalidate → compile
```

- `compile(RenderContext&)` 生成附件索引、subpass 依赖、preserve、store 策略和 barrier 模板。不分配临时纹理，不创建 framebuffer。已编译时再次调用不会重复编译。
- `prepareFrame(RenderContext&)` 解析当前导入纹理、获取当前帧池条目、求值尺寸和清屏值、查询 native 缓存，并将 barrier 模板绑定到实际 RID 和初始状态。
- `execute(context, frame, commandBuffer)` 刷新 trackedState 对应的首次访问 barrier，再录制当前帧命令并暂存资源状态，实际提交成功后确认。省略 commandBuffer 时使用 context 的命令缓冲。
- `RgFrameResources` 保存当前帧纹理/view、RenderingInfo、RenderPassBeginInfo 和实际 barriers。析构或 `reset()` 归还池条目，图仍保持已编译。
- `invalidate()` 清除编译计划，保留声明。`reset()` 同时清除声明。二者都不应每帧调用。`compileCount()` 累计实际编译次数。

## 使用示例

```cpp
// graph 是 Pipeline 的成员。只在初始化或结构变化时声明：
const auto output = graph.importSwapchainTexture(context.swapchain());
RgRasterPass pass;
pass.name = "Forward";
pass.renderAreaResolver = [](const RenderContext& frame) {
    return rhi::Rect{0, 0, frame.sceneWidth(), frame.sceneHeight()};
};
pass.colorAttachments.push_back({output, rhi::LoadOp::Clear, rhi::StoreOp::Store});
pass.execute = [this](const RgExecuteContext& execution) {
    // items_ 由 updateFrame 更新，禁止捕获构建时的局部 context/DrawList。
    // execution.frame 是当前 RenderContext。
    // execution.resources.textureView(handle) 获取当前帧 view。
    // 使用 execution.renderArea 设置 viewport/scissor，再录制 items_。
};
graph.addGraphicsPass(std::move(pass));
graph.compile(context);

// 每帧 acquisition 完成、绘制数据更新之后：
if (!graph.resourcesCompatible(context)) {
    graph.invalidate(); // 外部资源格式或采样数变化。
    graph.compile(context);
}
auto frame = graph.prepareFrame(context);
// 可在此通过 frame.textureView(shadowMap) 更新描述符。
graph.execute(context, frame);
```

单独使用时可构造 `RenderContext(pool, cache, scene)`；执行时显式提供 commandBuffer。依赖 Renderer 的方法需要由 Renderer 构造的 context。

## 可变资源和参数

`importSwapchainTexture(swapchain)` 注册稳定的 swapchain 来源，prepareFrame 每次重新读取 acquired image、view 和状态，并转换到 Present。

`importTexture(name, resolver, finalState)` 注册具名外部来源，resolver 返回 `RgTextureBinding`，其中包括 texture、view、初始 state/access 和可选状态槽。相同名称共享逻辑句柄，必须代表同一个来源及状态约定。不同逻辑句柄不能解析为同一物理 image。导入纹理的 aspect 在编译时根据 view 格式自动推导。固定纹理也通过 resolver 返回绑定；跨帧状态由外部资源所有者维护，提供 trackedState 时由提交批次在提交成功后回写。

RenderTarget 的 `colorBinding()/depthBinding()` 返回当前附件；`importColor()/importDepth()` 注册动态来源，resize 后无需修改图的逻辑句柄。Pipeline 的 ForwardDepth/ForwardColor 来源根据当前 context 选择帧槽目标。

- `createTexture(TextureDesc, extentResolver, finalState)` 使用通用引擎描述，第二个参数每帧决定临时纹理宽高；format 和 samples 保持固定。usage 由附件、采样访问及 finalState 汇总推导，tiling 固定 Optimal。
- 图外回读的临时纹理可指定 finalState 为 `CopySource`，自动获得 TransferSource 用途并在执行结束时转换状态；读取前必须保留内容。传统 renderpass 会为导出读取升级 Store，独立 dynamic rendering pass 需显式使用 Store。
- `RgRasterPass::renderAreaResolver` 每帧决定 render area。
- 颜色/深度附件的 `clearColorResolver/clearDepthResolver` 每帧决定清屏值。
- 尺寸、纹理/view RID、相机、绘制列表和清屏值变化不改变计划；外部资源 format/samples 变化通过 `resourcesCompatible()` 检测。
- 每个 raster pass 的附件 sample count 必须一致；group 中所有 subpass 的实际 render area 和附件 sample count 必须一致。

## Pipeline 和同步

`IRenderPass::buildGraph(context, graph)` 声明稳定结构，`updateFrame(context, drawList)` 每帧过滤并更新 pass 自己的数据。MiniForwardPipeline 持有图；addPass/setPasses 或输出模式变化使结构失效。自定义 pass 配置改变结构时调用 Pipeline 的 invalidateGraph()。普通 resize 不使结构失效。

内置 ShadowCaster、DepthOnly 和 Forward 始终存在。默认 Forward 加载 DepthOnly 的深度结果；独立 Forward 仍可清除深度。空绘制列表仍执行声明的 clear/load/store；灯光是否投影只改变当前绘制数据和 shader 阴影强度；shadow map 存在时始终绑定当前帧 view。回调通过 execution.frame 读取当前 frameIndex 和 uniform sets。

附件及 sampledTextures 自动推导 RgResourceAccess，用于初始化检查、依赖和同步。编译计划保存逻辑 barrier；每帧根据物理纹理的实际 layout/access 补齐首次访问的 before/sourceAccess。最终 layout 也会校验 usage；导出给读取/呈现的附件不能丢弃内容。提交成功后回写外部状态槽和池条目的 lastState/lastAccess；取消录制保持原状态。新一帧虚拟 transient 的内容仍视为未初始化，不能依赖上一帧内容。

## 所有权与提交

图不持有 pool、cache 或 RenderContext 指针。Renderer 持有全局 RgTexturePool 和
RgRenderPassCache；RHI device 负责 native 对象生命周期、RID 注册和图形队列提交完成跟踪。
更多 subpass 规则见 [RenderGraphSubpasses.md](RenderGraphSubpasses.md)，池的完整协议见
[RgTexturePool.md](RgTexturePool.md)。

池按实际纹理描述进行哈希分组，Entry 持有 Ref<Texture>，只保存租借、提交和同步状态。
所有帧共享可用纹理，CPU 租借释放且 GPU 提交完成后才能复用。纹理长期闲置时可回收，
先失效引用其 view 的 framebuffer。设备切换清理旧条目；Texture 的 UID 检查保护新设备。

Renderer 每帧调用 collect(completedSubmission)、trim 和 beginBatch(commandBuffer)。
RenderGraph::execute 要求活动批次；提交成功后 onSubmitted(submissionId) 确认状态。
present 返回 OutOfDate 不代表提交失败。回调异常时 Renderer 丢弃原命令，安全呈现清屏，
然后 cancelBatch 取消纹理使用及状态更新。其他按帧槽管理的 command buffer、uniform 和
外部 ForwardTarget 继续保留帧槽隔离。

单独执行图时，由调用方维护这个协议：

```cpp
pool.collect(device.completedSubmission());
pool.beginBatch(commandBuffer);
auto frame = graph.prepareFrame(context);
graph.execute(context, frame, commandBuffer);
frame.reset(); // 只释放 CPU 租借，已录制纹理仍不能复用
// 结束并提交 commandBuffer，获得图形队列的 SubmissionId 后：
pool.onSubmitted(submissionId);
// 后续查询 GPU 完成进度：
pool.collect(device.completedSubmission());
```

当前支持同一设备的单图形队列、每个批次一个 command buffer。一个图只能同时持有一个
CPU-prepared frame；frame 必须先于 graph 和 pool 销毁。外部纹理及 trackedState 的所有者
要存活到提交批次完成或取消；prepare 与 execute 之间不能替换绑定。多个图在同一批次
读取同一 trackedState 时会看到批次内的临时最终状态。允许提前 prepare 多个图再依次 execute：
execute 刷新首次访问 barrier、内容初始化检查和依赖初始状态的最终转换，不重新解析纹理或编译结构。
没有 trackedState 的导入资源仍由调用方提供正确的初始状态。整个批次必须一起提交或取消。
取消批次前必须确保对应命令已被丢弃且不会提交。

frame 释放不会销毁 GPU 资源。销毁 pool/cache 前等待相关 GPU 工作结束并释放租借；
外部资源所有者仍需在 GPU 完成后先失效 framebuffer，再销毁或替换 view。
