# RenderGraph render-pass groups

Implementation plan (executed in order):

- [x] Resource access declarations and automatic attachment usages.
- [x] Explicit raster groups, compiled attachment tables/subpasses/dependencies, validation.
- [x] RHI and Vulkan render-pass/framebuffer caching and command recording.
- [x] Subpass pipeline selection, input-attachment descriptors and shader reflection.
- [x] Compiler regression tests, real Vulkan subpass test, Debug/Release verification.
- [x] Move render-pass/framebuffer caching to the render-graph layer, with RHI lifecycle only.

Default graphics passes continue to use dynamic rendering. An explicit raster group
compiles to one traditional Vulkan render pass, with one subpass per logical pass.
Registration order is execution order in this first version. Automatic merging,
resource aliasing, async compute, resolves and attachment feedback loops are deferred.

## Declaring a group

Create a persistent `RgRenderPassCache` alongside the texture pool. `Renderer` owns
both and exposes the cache through `RenderContext::rgRenderPassCache()`. A frame-local
graph shares this cache with subsequent graphs:

```cpp
// Persistent rendering resources; they outlive recorded/submitted frame graphs.
RgTexturePool pool;
RgRenderPassCache cache;
```

```cpp
auto gbuffer = graph.createTexture({
    .format = rhi::PixelFormat::Rgba8Unorm,
    .width = width,
    .height = height,
    .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::InputAttachment,
    .debugName = "GBuffer",
});

RgRasterPass geometry;
geometry.name = "GBuffer";
geometry.rendering.renderArea = {0, 0, width, height};
geometry.rendering.colorAttachments.push_back(
    {gbuffer, rhi::LoadOp::Clear, rhi::StoreOp::DontCare});
geometry.execute = [&](const RgExecuteContext& context) {
    // Resolve a pipeline using context.renderPass and context.subpass.
    // Bind the resulting pipeline, descriptors, geometry, and draw.
};

RgRasterPass lighting;
lighting.name = "Lighting";
lighting.rendering.renderArea = geometry.rendering.renderArea;
lighting.rendering.colorAttachments.push_back(
    {output, rhi::LoadOp::Clear, rhi::StoreOp::Store});
lighting.rendering.inputAttachments = {gbuffer};
lighting.execute = [&](const RgExecuteContext& context) {
    // The shader uses input_attachment_index = 0 and subpassLoad().
    // Bind gbuffer's resolved view as ResourceType::InputAttachment.
};

graph.addRasterGroup("Deferred", {std::move(geometry), std::move(lighting)});
graph.compile(pool, cache);
// Views can now be resolved and descriptors populated.
graph.execute(commandBuffer);
graph.reset();
```

`compile(pool)` remains available for independent dynamic-rendering passes. Explicit
raster groups require `compile(pool, cache)` so a frame-local graph cannot accidentally
destroy resources before its command buffer is submitted.

`output` is another graph texture, commonly an imported swapchain/offscreen image.
The callback receives a command-buffer RID, a cached render-pass RID and the actual
subpass index. `GraphicsPipelineStorage::resolve` has a subpass-aware overload:

```cpp
auto pipeline = GRAPHICS_PIPELINE_STORAGE.resolve(
    program, mesh, colorFormats, depthFormat,
    context.renderPass, context.subpass, rhi::SampleCount::One);
```

The cache includes the ordered color formats, depth format, samples, render-pass RID,
subpass index, shader program and vertex layout. Resolve this pipeline after graph
compilation, rather than reusing a pipeline prepared for dynamic rendering.

## Compilation and synchronization

The compiler retains registration order and one logical pass per subpass. It emits:

- One `CompiledGroup` for each independent pass or explicit group.
- A group-wide deduplicated attachment table and per-subpass references.
- Dependencies for RAW, WAR, WAW and attachment layout changes inside a group.
- Preserve references when an attachment spans an unused intermediate subpass.
- Barriers before groups, including same-layout hazards, and imported final transitions.

`RgResourceAccess` declares a pass's texture, aspect, state and read/write mode. The
compiler derives synchronization and dependencies, and validates resource accesses.
Color/depth/input bindings automatically produce these access declarations. Additional
`resources` declarations describe sampled texture reads. `AccessMode::Auto` preserves
the old declarations; explicit access is available for depth read-only use.
The compiler validates all handles and rejects conflicting declarations and feedback.

An attachment's first use selects load/clear. Later output uses must request `Load`;
a later clear/discard requires a separate group. Intermediate `DontCare` store hints
do not discard contents between subpasses. A later group consumer or an imported
read/present final state upgrades the group's final store to `Store` when necessary.
Independent passes preserve their explicitly declared store operation: reading their
discarded contents is a compilation error.

Implicit stores, including `DontCare`, participate in synchronization. When an
attachment finishes as input, dependencies include its color/depth store stage;
following barriers retain those writes while tracking its input-attachment layout.
See the [Vulkan store-operation rules](https://docs.vulkan.org/spec/latest/chapters/renderpass.html#renderpass-store-operations).

## Lifetime

`RgRenderPassCache` owns the render-pass and framebuffer caches in `render_graph/`.
The RHI only exposes `render_pass_create/destroy`, `framebuffer_create/destroy` and
handle resolution. Each RHI create call creates a new object, even for identical
descriptions. The device's handle pools register resources; they have no render-pass
or framebuffer reuse policy and do not evict objects on view destruction.

The graph cache deduplicates render passes by their complete descriptions and
framebuffers by render pass, ordered view RIDs (including generations), dimensions
and layers. Both survive graph reset and destruction. The cache must outlive submitted
graphs, and the rendering layer must retire the affected GPU work before invalidation
or destruction. These are the ordinary Vulkan [framebuffer destruction rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyFramebuffer.html).

- `cache.invalidateFramebuffers(view)` destroys only entries referencing this view.
  Call it before replacing an imported render-target view, after the affected frame's
  fence. `Renderer::prepareForwardTarget()` does this for the current frame slot.
- `cache.clearFramebuffers()` drops all framebuffers while keeping render-pass handles
  stable for pipeline reuse. `Renderer` calls it after device idle, before swapchain
  recreation and render-target resizing.
- `cache.clear()` and its destructor destroy framebuffers before render passes.
  `Renderer` destroys the cache while idle, before pooled textures, targets and device.

The cache tracks its creating device UID; teardown never sends old RIDs to a different
device that might have reused the same numeric handle values. A new device starts a
fresh cache. Owners of imported views outside `Renderer` perform the same explicit
framebuffer invalidation before destroying/replacing those views.

Transient textures return to the frame-indexed pool on reset. Their physical layout
and last access are updated after command recording and used by the next graph's
barriers. A newly declared virtual transient texture has undefined contents even when
the physical allocation is reused. Reset clears all passes, virtual resources and
compiled plans; it does not destroy the shared graph cache.
Recording and execution must follow the same submission order on the graphics queue.

## First-version constraints

- Graph attachments use 2D views of mip 0, layer 0, with one mip/layer.
- All passes in a group use the same render area; attachments have matching samples.
- Input order matches shader `input_attachment_index`; descriptors are fragment-only.
- Group attachments are read locally through input attachments. Sampling such an
  attachment requires another group/pass; shader semantics are never rewritten.
- Resolves, multiview, feedback loops, automatic merging/reordering, buffers and
  asynchronous queues are outside this implementation.

## Verification

`RenderGraphTest` covers independent/mixed groups, attachment/dependency compilation,
preserve references, same-layout writes, read-before-write, import dedup/reset,
input-attachment reflection, pipeline cache discrimination and invalid declarations.
It also verifies reuse across frame-local graphs, targeted framebuffer invalidation,
view generations/extent keys, destruction order and device UID safety.
`RhiDeviceTest.RenderGraphSubpassesReadInputAndReuseCaches` draws two color attachments,
reads one in a second subpass, verifies all output pixels by GPU readback, repeats the
graph using cached resources and checks validation output.
`RhiDeviceTest.RenderGraphDepthReadOnlyAndInputStoresAreSynchronized` verifies depth
clear/read-only/input transitions and the following sampled-read barrier.
`RhiDeviceTest.RenderObjectCreationDoesNotCacheAtDeviceLevel` confirms the raw RHI
creates distinct render passes and framebuffers for identical descriptions.

Verified after the cache refactor: complete Debug and Release builds; 219/219 CTest
cases passed in each configuration. All three focused real-device tests passed with
synchronization validation enabled, including distinct raw RHI object creation.
The existing scene-view integration test also passed, covering viewport resize,
hidden/empty scenes and project reattachment.

```powershell
ctest --test-dir build/clang-debug -R "RenderGraph|RhiDevice" --output-on-failure
$env:VK_LAYER_ENABLES = 'VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT'
build/clang-debug/RhiDeviceTest.exe --gtest_filter=RhiDeviceTest.RenderGraph*:RhiDeviceTest.RenderObjectCreationDoesNotCacheAtDeviceLevel
```
