# Frustum Culling and Transparent Sorting

Stage D adds two visibility optimizations to the programmable pipeline: frustum culling of objects before draw-list generation, and back-to-front sorting of transparent geometry inside the forward pass.

## Frustum math

`core/math/Frustum.h` provides:

- `math::Plane` with a normal and signed distance.
- `math::Frustum` containing six inward-facing planes in the order Left, Right, Bottom, Top, Near, Far.
- `math::extractFrustum(viewProjection)` that extracts the six planes from a view-projection matrix and normalizes them.
- `math::intersects(frustum, center, radius)` for conservative sphere-frustum culling.

The sphere test rejects an object as soon as its center is more than `radius` units behind any frustum plane.

## Object bounds

`RenderObject` now carries a `boundsRadius` field. A value of `0` treats the object as a single point at its transform origin. Larger values allow coarse bounding-sphere culling for meshes whose origin is near their center.

## Integration in DrawListBuilder

When the scene has a camera, `DrawListBuilder` computes:

```cpp
math::Frustum frustum = math::extractFrustum(camera.projection * camera.view);
```

Each object is tested before its sub-meshes are expanded into draw items:

```cpp
const math::Vec3 center = math::transformPoint(object.transform, math::Vec3{0.0F});
if (!math::intersects(frustum, center, object.boundsRadius)) {
    continue;
}
```

Rejected objects do not consume draw items, object uniform slots, or GPU resources for the current frame.

## Transparent sorting

`ForwardPass` splits its filtered items into opaque and transparent buckets:

- Opaque items are sorted by `RenderQueue | Pipeline | Material | Mesh` to maximize state reuse.
- Transparent items are sorted by `BackToFront` so that farther transparent objects are drawn first.

The buckets are concatenated (opaque first, transparent second) before recording the draw commands.

## Future work

- AABB-frustum tests for tighter culling.
- Hierarchical culling (octree / BVH) for large scenes.
- Per-phase sorting policy configuration on `IRenderPass`.
