# Render Pass System

The render pass system splits the monolithic forward renderer into small, composable passes similar to Unity's Scriptable Render Passes.

## Core abstractions

- `IRenderPass` declares persistent resources/callbacks in `buildGraph(context, graph)`. `updateFrame(context, drawList)` updates pass-owned filtered draw data every frame. Callbacks capture stable pass objects and read the current `RgExecuteContext::frame`, bindings and render area.
- `MiniForwardPipeline` owns its pass list and RenderGraph. It builds/compiles initially, then prepares bindings and executes every frame. Changes to passes or output mode rebuild structure; format/sample changes invalidate the plan; ordinary resizing and empty draw lists do not recompile.
- `DrawListBuilder` (`render/renderer/DrawListBuilder.h`) builds the `DrawList` from the `RenderScene`. It resolves shader passes for the built-in phases `ShadowCaster`, `DepthOnly` and `Forward` and leaves GPU material bind group resolution to the pipeline.

## Render queue subsystem

`render/queue/RenderQueue.h` provides Unity-compatible constants and helpers:

| Constant | Value | Alias |
|---|---|---|
| `kRenderQueueBackground` | 1000 | |
| `kRenderQueueGeometry` | 2000 | `kRenderQueueOpaque` |
| `kRenderQueueAlphaTest` | 2450 | |
| `kRenderQueueGeometryLast` | 2500 | opaque/transparent boundary |
| `kRenderQueueTransparent` | 3000 | |
| `kRenderQueueOverlay` | 4000 | |

- `RenderQueueRange` is an inclusive range with factory methods `all()`, `opaque()` and `transparent()`.
- `DrawFilter` filters items by `RenderQueueRange`, layer mask and (future) render type.
- `SortingCriteria` provides bit flags for `RenderQueue`, `Pipeline`, `Material`, `Mesh` and `BackToFront` sorting.
- `DrawSorter` compares the requested criteria lexicographically using complete resource IDs. `BackToFront` uses an inverted camera distance so farther transparent objects draw first. Draw items carry their original scene index separately from the compact GPU object-table index, so culling does not change layer filtering or distance lookup.

## Built-in passes

### ForwardPass

Draws the `Forward` phase into the swapchain back-buffer and the forward depth target. It clears color and stores it for presentation. The default chain uses `Load` for depth to retain DepthOnly results; standalone ForwardPass defaults to `Clear`. Its constructor accepts the depth load operation.

### DepthOnlyPass

Draws the `DepthOnly` phase into the forward depth target only. It is used as a z-prepass for opaque geometry. The depth clear remains active when no item matches the filter.

### ShadowCasterPass

Draws the `ShadowCaster` phase into a transient depth shadow map (default 1024x1024, `Depth32Float`, `DepthStencilAttachment | Sampled`). Its logical texture and pass always exist, so enabling/disabling shadow-casting directional lights updates draw data without rebuilding the graph. The pass clears the map even when there are no casters.

`ForwardPass` declares a stable sampled dependency. Between frame preparation and execution, the pipeline binds the current `frame.textureView(handle)` whenever the shadow-map handle is valid. The map remains cleared when there are no casters, and `Scene.shadowParams.x` controls shader shadow strength. Global pooling preserves physical state/access at submission commit and prevents reuse until GPU completion and CPU lease release.

## Composing a pipeline

The default `MiniForwardPipeline` constructor already installs the `ShadowCaster -> DepthOnly -> Forward` chain wired to its internal `ShadowCasterOutput`. Custom chains must keep the producer/consumer pair connected to one shared struct:

```cpp
ShadowCasterOutput shadowOutput;
auto pipeline = std::make_unique<MiniForwardPipeline>();
std::vector<std::unique_ptr<IRenderPass>> passes;
passes.push_back(std::make_unique<ShadowCasterPass>(&shadowOutput, 1024));
passes.push_back(std::make_unique<DepthOnlyPass>());
passes.push_back(std::make_unique<ForwardPass>(
    &shadowOutput, DrawFilter{RenderQueueRange::all()}, rhi::LoadOp::Load));
pipeline->setPasses(std::move(passes));
renderer.setPipeline(std::move(pipeline));
```

## Shared draw logic

`RenderPassUtils::drawFilteredItems` performs the actual draw call emission: it binds the pipeline and scene bind group at slot 0, the material bind group at slot 1, vertex/index buffers and issues `drawIndexed`. Passes are expected to have already filtered and sorted their items before calling it.
