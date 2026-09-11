# Render Pass System

The render pass system splits the monolithic forward renderer into small, composable passes similar to Unity's Scriptable Render Passes.

## Core abstractions

- `IRenderPass` (`render/pipeline/RenderPass.h`) is the base interface. A pass receives the frame `RenderContext`, the active `RenderGraph` and the prepared `DrawList`. It decides which items to draw and which resources to declare. Pass callbacks run during `RenderGraph::execute` and query the frame scene bind group from `FRAME_GPU_MANAGER` at execution time, so the pipeline may rebuild that bind group between `compile()` and `execute()` (e.g. to bind the resolved shadow map view).
- `MiniForwardPipeline` (`render/pipeline/MiniForwardPipeline.h`) owns an ordered list of `IRenderPass` instances and executes them every frame after resolving material bind groups and uploading scene/object uniforms.
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
- `DrawSorter` packs the requested criteria into a 64-bit sort key. `BackToFront` uses an inverted camera distance so farther transparent objects draw first.

## Built-in passes

### ForwardPass

Draws the `Forward` phase into the swapchain back-buffer and the forward depth target. It clears color and depth and stores the color attachment for presentation.

### DepthOnlyPass

Draws the `DepthOnly` phase into the forward depth target only. It is used as a z-prepass for opaque geometry. If no item matches the filter the pass does not record anything.

### ShadowCasterPass

Draws the `ShadowCaster` phase into a transient depth shadow map allocated from the `RenderGraph` texture pool (default 1024x1024, `Depth32Float`, usage `DepthStencilAttachment | Sampled`, depth aspect). When the scene contains a shadow-casting directional light the pass always creates and clears the shadow map - a fully cleared map decodes as "no shadow" - and writes the handle plus a `castShadows` flag into the `ShadowCasterOutput` struct owned by the pipeline.

`ForwardPass` receives the same `ShadowCasterOutput` pointer, declares the shadow map as a `ShaderRead` input, and the pipeline calls `FRAME_GPU_MANAGER.bindShadowMap(frameIndex, graph.resolvedTextureView(handle))` between `compile()` and `execute()` so set 0 binding 3 samples the resolved view. Because the pool buckets textures per frame index, the resolved view is stable across frames and the bind group is not rebuilt in steady state.

## Composing a pipeline

The default `MiniForwardPipeline` constructor already installs the `ShadowCaster -> DepthOnly -> Forward` chain wired to its internal `ShadowCasterOutput`. Custom chains must keep the producer/consumer pair connected to one shared struct:

```cpp
ShadowCasterOutput shadowOutput;
auto pipeline = std::make_unique<MiniForwardPipeline>();
pipeline->setPasses({
    std::make_unique<ShadowCasterPass>(&shadowOutput, 1024),
    std::make_unique<DepthOnlyPass>(),
    std::make_unique<ForwardPass>(&shadowOutput),
});
renderer.setPipeline(std::move(pipeline));
```

## Shared draw logic

`RenderPassUtils::drawFilteredItems` performs the actual draw call emission: it binds the pipeline and scene bind group at slot 0, the material bind group at slot 1, vertex/index buffers and issues `drawIndexed`. Passes are expected to have already filtered and sorted their items before calling it.
