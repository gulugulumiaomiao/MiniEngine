# RenderGraph render-pass groups

Implementation plan (executed in order):

- [x] Resource access declarations and automatic attachment usages.
- [x] Explicit raster groups, compiled attachment tables/subpasses/dependencies, validation.
- [x] RHI and Vulkan render-pass/framebuffer caching and command recording.
- [x] Subpass pipeline selection, input-attachment descriptors and shader reflection.
- [x] Compiler regression tests, real Vulkan subpass test, Debug/Release verification.
- [x] Move render-pass/framebuffer caching to the render-graph layer, with RHI lifecycle only.
- [x] Compile through RenderContext without retaining pool/cache/context pointers in the graph.
- [x] Persistent plans with per-frame bindings, dynamic extents and current draw data.

Default graphics passes continue to use dynamic rendering. An explicit raster group
compiles to one traditional Vulkan render pass, with one subpass per logical pass.
`RgRasterPass` directly contains its render area, color/depth/input attachments,
sampled texture handles and execution callback. Independent passes use
`addGraphicsPass(pass)`; grouped subpasses use `addRasterGroup(name, passes)`.
Registration order is execution order in this first version. Automatic merging,
resource aliasing, async compute, resolves and attachment feedback loops are deferred.

## Declaring a group

Register the stable swapchain source with `graph.importSwapchainTexture(swapchain)`.
`prepareFrame()` resolves its acquired texture, view and state after `beginFrame()`
and requests a final transition to `Present`. Other external textures use
`importTexture(name, resolver, finalState)`; the resolver supplies a
`RgTextureBinding` with the current texture, view, state/access and optional state slot.
There is no `ImportedTexture` description type. Imported resources remain externally
owned. Imported and transient textures need no aspect parameter: compile derives
color or depth from the view format or transient pixel format.

`Renderer` owns a persistent texture pool and `RgRenderPassCache` and exposes both
through `RenderContext`. Pass the frame context to graph compilation:

```cpp
RenderContext context(renderer, scene);
```

```cpp
RenderGraph graph;
auto gbuffer = graph.createTexture({
    .format = rhi::PixelFormat::Rgba8Unorm,
    .width = width,
    .height = height,
    .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::InputAttachment,
    .debugName = "GBuffer",
});

RgRasterPass geometry;
geometry.name = "GBuffer";
geometry.renderArea = {0, 0, width, height};
geometry.colorAttachments.push_back(
    {gbuffer, rhi::LoadOp::Clear, rhi::StoreOp::DontCare});
geometry.execute = [&](const RgExecuteContext& context) {
    // Resolve a pipeline using context.renderPass and context.subpass.
    // Bind the resulting pipeline, descriptors, geometry, and draw.
};

RgRasterPass lighting;
lighting.name = "Lighting";
lighting.renderArea = geometry.renderArea;
lighting.colorAttachments.push_back(
    {output, rhi::LoadOp::Clear, rhi::StoreOp::Store});
lighting.inputAttachments = {gbuffer};
lighting.execute = [&](const RgExecuteContext& context) {
    // The shader uses input_attachment_index = 0 and subpassLoad().
    // Bind gbuffer's resolved view as ResourceType::InputAttachment.
};

graph.addRasterGroup("Deferred", {std::move(geometry), std::move(lighting)});
graph.compile(context); // Once per structure/format/sample-count version.
// Every frame, after acquisition and dynamic draw-data updates:
auto frame = graph.prepareFrame(context);
// Populate input descriptors using frame.textureView(gbuffer).
graph.execute(context, frame, commandBuffer);
// frame's destructor releases leases; graph remains compiled.
```

Both independent dynamic-rendering passes and raster groups use `compile(context)`.
Compilation produces attachment/dependency tables and barrier templates without
allocating textures or creating framebuffers. `prepareFrame(context)` borrows the
pool/cache, resolves physical bindings and patches barriers using their actual states.
`RgFrameResources` owns the current leases and releases them on destruction/reset.
It retains no context pointer; the pool and graph must outlive the frame, and cached
objects must outlive GPU work. Only one CPU-prepared frame may be live per graph;
multiple GPU submissions can remain in flight through the pool's submission retirement queue.

For standalone graph compilation, construct `RenderContext(pool, cache, scene)`
with externally owned resources. Renderer-dependent methods require a context
constructed with a renderer.

`output` is another graph texture, commonly an imported swapchain/offscreen image.
All callbacks receive `RgExecuteContext`: the command-buffer RID, render-pass RID,
subpass index, current RenderContext, frame bindings and resolved render area.
Independent passes receive a null render-pass RID and index zero.
`GraphicsPipelineStorage::resolve` has a subpass-aware overload:

```cpp
auto pipeline = GRAPHICS_PIPELINE_STORAGE.resolve(
    program, mesh, colorFormats, depthFormat,
    context.renderPass, context.subpass, rhi::SampleCount::One);
```

The cache includes the ordered color formats, depth format, samples, render-pass RID,
subpass index, shader program and vertex layout. Resolve this pipeline after graph
frame preparation, rather than reusing a pipeline prepared for dynamic rendering.

## Compilation and synchronization

The compiler retains registration order and one logical pass per subpass. It emits:

- One `CompiledGroup` for each independent pass or explicit group.
- A group-wide deduplicated attachment table and per-subpass references.
- Dependencies for RAW, WAR, WAW and attachment layout changes inside a group.
- Preserve references when an attachment spans an unused intermediate subpass.
- Barriers before groups, including same-layout hazards, and imported final transitions.

Passes declare attachments and a `sampledTextures` list of graph texture handles:

```cpp
pass.sampledTextures = {shadowMap};
```

The compiler derives the complete `RgResourceAccess` list. Sampled textures get the
texture node's aspect, `ShaderRead` state and `Read` access. Color/depth/input
attachments generate their own accesses; they do not need duplicate declarations.
The depth attachment's explicit access selects read-only or read/write use.
Compiled accesses drive synchronization, dependencies and initialization validation.
The compiler validates all handles and rejects conflicting declarations and feedback.

An attachment's first use selects load/clear. Later output uses must request `Load`;
a later clear/discard requires a separate group. Intermediate `DontCare` store hints
do not discard contents between subpasses. A later group consumer or an imported
read/present final state upgrades the group's final store to `Store` when necessary.
Independent passes preserve their explicitly declared store operation: reading their
discarded contents, including through an exported read/present final state, is a compilation error.
Final layouts must match the texture usage; shader-read layout supports sampled or input usage.
Every raster pass also validates attachment sample-count compatibility.

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

The texture pool and cache track their creating device UID; teardown never sends old RIDs to a different
device that might have reused the same numeric handle values. A new device starts a
fresh cache. Owners of imported views outside `Renderer` perform the same explicit
framebuffer invalidation before destroying/replacing those views.

Transient textures release their CPU leases when frame resources expire. They return to the global
pool only after their graphics submission completes. Physical layout and last access are committed
after successful submission; cancelled batches preserve their original states. A newly declared virtual transient texture has undefined contents even when
the physical allocation is reused. `invalidate()` preserves declarations and clears
the plan; `reset()` also clears passes and virtual resources. Neither is per-frame work.
Dynamic external imports can use a named resolver returning `RgTextureBinding`.
Size-only changes use `extentResolver`/`renderAreaResolver`; check
`resourcesCompatible(context)` and invalidate/recompile for format/sample changes.
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

Standalone graph execution must begin a pool submission batch, commit it with the actual graphics
SubmissionId, and collect device completion before reuse. See [RgTexturePool.md](RgTexturePool.md).
