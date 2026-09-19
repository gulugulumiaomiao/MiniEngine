# 渲染管线分阶段数据流

当前渲染管线把场景数据、批处理和 GPU 命令分成三个明确阶段：

```text
RenderScene
  -> DrawListBuilder::extract       视锥剔除，生成 SourceDrawItem
  -> DrawListBuilder::prepare       选择 SubShader/Pass，按 RenderQueue 归类
  -> StaticBatcher::process         烘焙静态几何并缓存合并后的 VB/IB
  -> IRRenderPass                   按 RenderGraph 编排执行
  -> DrawBatcher::build             相邻且兼容的项目合成 GPU instance draw
  -> RenderItem / drawIndexed
```

`SourceDrawItem` 保留 Mesh、SubMesh 索引范围、Material 和世界矩阵，供 CPU
批处理使用。`DrawItem` 是选定 shader pass 和 GPU 资源后的中间数据。
`RenderItem` 对应一次 `drawIndexed`，包含 pipeline、动态绘制状态、VB/IB、材质
bind group 和 instance 数量。

## RenderQueue 与 GPU 调试

Pass 先按 RenderQueue 读取 `SourceDrawGroups`。同一 queue 内再按 pipeline、动态状态、
顶点布局、材质和距离排序。GPU instancing 不跨 RenderQueue 合并。提交命令时，每段
RenderQueue 使用 `RenderQueue <值>` 的 GPU debug label，因此 RenderDoc、Nsight 或
验证层标记可以直接对应到材质的 queue。

## Static Batching

Material 的 batching mode 为 `Static` 时才参与静态合批。兼容项必须具有相同 pipeline、
动态绘制状态、顶点布局、材质和 RenderQueue。合批器把 position 乘世界矩阵，把
normal/tangent 乘 normal matrix，并把索引统一重写为 UInt32。

缓存键包含 Mesh handle/version、Material version、SubMesh 范围和世界矩阵。资源或变换
变化会产生新缓存项；swapchain 重建和 pipeline 销毁会在 device idle 后释放缓存。
单批默认最多 128 个 source item、`1 << 20` 个索引。

## GPU Instancing

Material 的 batching mode 为 `GpuInstancing` 时，相邻且 GPU 状态、几何与索引范围相同的
项目可以合成一次 draw。每个实例的 object row 写入当前 in-flight frame 的 instance
table。单次 draw 默认最多 1024 个实例。Static 与 GpuInstancing 是互斥选项。

## RenderGraph 与 RenderTarget

Pass 声明输入、输出和资源状态，RenderGraph 编译依赖并缓存稳定拓扑的执行计划。
Graph 负责 pass 边界的纹理布局转换。持久相机 target 由 `RenderTargetPool` 管理，Graph
临时纹理由 `RgTexturePool` 管理。Camera 未指定 target 时输出到 swapchain；编辑器
Scene View 使用 Renderer 的离屏 target。

## Statistics 窗口

`Renderer::frameStats()` 保存最近一帧的统计：source/prepared/submitted item 数量、
render item 与 draw call 数量、静态合批命中、GPU instance draw、RenderGraph pass 数和
临时 RT 数。编辑器 `StatisticsPanel` 直接显示该快照，数值包括所有实际执行的 pass。

## 验证

新增测试一律使用 GoogleTest。批处理边界、缓存失效、动态状态、RenderGraph 编排和帧统计
分别由 `GpuInstancingBatcherTest`、`StaticBatcherTest`、`DynamicDrawStateTest`、
`RenderGraphSchedulingTest` 与 `RenderDiagnosticsTest` 覆盖。
