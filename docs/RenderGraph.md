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
- `execute(context, frame, commandBuffer)` 录制当前帧命令并更新资源状态。省略 commandBuffer 时使用 context 的命令缓冲。
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

`importTexture(name, resolver, finalState, aspect)` 注册具名外部来源，resolver 返回 `RgTextureBinding`，其中包括 texture、view、初始 state/access 和可选状态槽。相同名称共享逻辑句柄，必须代表同一个来源及状态约定。不同逻辑句柄不能解析为同一物理 image。固定 RID 的 import 重载仍可用。

RenderTarget 的 `colorBinding()/depthBinding()` 返回当前附件；`importColor()/importDepth()` 注册动态来源，resize 后无需修改图的逻辑句柄。Pipeline 的 ForwardDepth/ForwardColor 来源根据当前 context 选择帧槽目标。

- `RgTextureDesc::extentResolver` 每帧决定临时纹理宽高；format、usage 和 samples 保持固定。
- `RgRasterPass::renderAreaResolver` 每帧决定 render area。
- 颜色/深度附件的 `clearColorResolver/clearDepthResolver` 每帧决定清屏值。
- 尺寸、纹理/view RID、相机、绘制列表和清屏值变化不改变计划；外部资源 format/samples 变化通过 `resourcesCompatible()` 检测。
- 每个 raster pass 的附件 sample count 必须一致；group 中所有 subpass 的实际 render area 和附件 sample count 必须一致。

## Pipeline 和同步

`IRenderPass::buildGraph(context, graph)` 声明稳定结构，`updateFrame(context, drawList)` 每帧过滤并更新 pass 自己的数据。MiniForwardPipeline 持有图；addPass/setPasses 或输出模式变化使结构失效。自定义 pass 配置改变结构时调用 Pipeline 的 invalidateGraph()。普通 resize 不使结构失效。

内置 ShadowCaster、DepthOnly 和 Forward 始终存在。默认 Forward 加载 DepthOnly 的深度结果；独立 Forward 仍可清除深度。空绘制列表仍执行声明的 clear/load/store；灯光是否投影只改变当前绘制数据和 shadow 绑定。回调通过 execution.frame 读取当前 frameIndex 和 uniform sets。

附件及 sampledTextures 自动推导 RgResourceAccess，用于初始化检查、依赖和同步。编译计划保存逻辑 barrier；每帧根据物理纹理的实际 layout/access 补齐首次访问的 before/sourceAccess。最终 layout 也会校验 usage；导出给读取/呈现的附件不能丢弃内容。执行后回写外部状态槽和池条目的 lastState/lastAccess。新一帧虚拟 transient 的内容仍视为未初始化，不能依赖上一帧内容。

## 所有权

图不持有 pool、cache 或 RenderContext 指针。Renderer 持有 RgTexturePool 和 RgRenderPassCache；RHI device 只负责 native 对象生命周期与 RID 注册。更多 subpass 规则见 [RenderGraphSubpasses.md](RenderGraphSubpasses.md)。

每个图同时只能存在一个准备好的 CPU 帧。frame 必须先于 graph 和 pool 销毁；释放 CPU 租约后，多个 GPU 帧仍可通过帧槽池并行在途。调用 beginFrame 前需要等待对应帧槽的 fence，并释放该槽所有 CPU 租约。纹理池按 device UID 隔离条目，不能将旧设备 RID 用于新设备。prepare 到 execute 之间不得替换绑定的纹理或状态槽。

frame 释放只归还租约，不销毁 native 纹理/framebuffer。缓存及资源所有者需等待相关 GPU 工作结束，再失效旧 framebuffer 并替换 view。外部来源和回调捕获的 pass 对象必须在图使用期间保持有效。
