# 合批与材质常驻（Stage E）

Stage E 落地两项渲染效率基础设施：

1. **材质常驻**：材质 GPU 资源跨帧驻留，只有内容变化时才上传，消除每帧的 bindGroup 重建与 uniform 重传。
2. **合批**：`DrawBatcher` 把排序后状态连续的 DrawItem 合并为一次实例化 `drawIndexed`，配合每帧实例表（Instance Table）完成对象寻址。

## 合批（DrawBatcher）

### 合并规则

`DrawBatcher::build(items)` 只合并**相邻**且 GPU 状态完全一致的 DrawItem，判定字段：

- `pipeline`
- `materialBindGroup`
- `vertexBuffers`（数量与每个 binding 的 buffer）
- `indexBuffer` 与 `indexFormat`
- 索引区间：`indexCount` / `firstIndex` / `vertexOffset`

相邻合并意味着调用方控制的排序顺序（RenderQueue、BackToFront 等）被完整保留。`A A B A A` 产生三个批，而不是把两段 A 拼成一个。

### 实例表（Instance Table）

批内每个实例对应一个 DrawItem；顶点着色器不再直接用 `gl_InstanceIndex` 索引对象数组，而是经过实例表间接寻址：

```glsl
// assets/shaders/include/objects.glsl
layout(std430, set = 0, binding = 1) readonly buffer ObjectBuffer
{
    mat4 transforms[];
} Objects;

layout(std430, set = 0, binding = 2) readonly buffer InstanceTableBuffer
{
    uint objectIndices[];
} InstanceIndices;

// 顶点着色器内
mat4 model = Objects.transforms[InstanceIndices.objectIndices[gl_InstanceIndex]];
```

间接寻址的原因：不同 pass（ShadowCaster / DepthOnly / Forward）对同一批对象有各自的过滤与排序，对象在 `ObjectBuffer` 中的行号在批内不连续。实例表让每个 pass 在自己保留的表区域内写入任意的对象行序列。

### 数据流

```text
MiniForwardPipeline::render
  -> DrawListBuilder.build            剔除 + 生成 DrawItem（firstInstance = 对象行）
  -> resolveMaterialBindGroups        材质常驻三级路径（见下文）
  -> FRAME_GPU_MANAGER.beginFrame     重置当前帧实例表计数
  -> FRAME_GPU_MANAGER.upload         上传 scene / objects 数据
  -> 各 pass execute（回调内）
      -> drawFilteredItems(frameIndex, items, ...)
          -> DrawBatcher.build        相邻合并
          -> reserveInstanceRegion    在当前帧实例表中保留不重叠区域
          -> uploadInstanceRegion     写入该 pass 的对象行表
          -> 每批一次 drawIndexed     firstInstance = baseSlot + 批内槽位
```

实例表由 `FrameGpuManager` 按 in-flight frame 分配（`kMaxInstances = 8192`），Upload 堆直写，`beginFrame` 时清零计数。各 pass 的区域互不重叠，且不同帧使用各自的表缓冲，因此回调录制阶段上传是安全的。

## 材质常驻

### 三级 resolve 路径

`MaterialGpuManager::resolve(handle)`：

1. **快路径**：槽位命中且 `Material::version()` 与已上传版本一致 → 直接返回 bindGroup，零 GPU 操作。
2. **脏更新**：仅 uniform 内容变化（纹理签名与绑定大小未变）→ `MaterialGpuFactory::updateUniforms` 只重传 uniform 字节，保留 bindGroup。
3. **全量重建**：槽位被驱逐、纹理引用变化或缓冲几何变化 → 重建 bindGroup 并记录簿记。

`Material` 的任何属性修改（含 `setTexture`、`setShader`、shader 热重载）都会递增 `version_`，因此三级路径自动覆盖所有失效场景。

### MaterialBindingCache 常驻策略

- `beginFrame` 不再清空查找表；槽位与其 GPU 资源跨帧驻留。
- 每个 in-flight frame 各持有一份槽位池（同帧内相同材质只上传一次，帧间写入互不冲突）。
- 容量上限 `kMaxResidentMaterials = 512`；超限时驱逐 LRU 槽位：查找表移除旧占用者，槽位标记 `pendingRelease`，下次被新材质全量重建前由 Factory 释放旧句柄。
- `MaterialGpuResource` 新增簿记字段：`uniformVersion`、`boundSize`、`ownerKey`、`lastUsed`、`textureBindings`、`pendingRelease`。

## 排序与合批的关系

`DrawSorter` 的 `Pipeline | Material | Mesh` 排序在合批前执行，作用是让相同状态的对象在序列中相邻，从而最大化合并率。透明物体按 `BackToFront` 排序，距离不同的对象通常各自成批——这是正确性要求，排序优先于合并。

## 相关文件

- `src/render/renderer/DrawBatcher.h/.cpp` 合批器
- `src/render/gpu/frame/FrameGpuManager.h/.cpp` 实例表缓冲与上传
- `src/render/gpu/material/*` 材质常驻三级路径
- `src/render/pipeline/passes/RenderPassUtils.cpp` 批次命令编码
- `assets/shaders/include/objects.glsl` 共享的对象/实例表声明
- `tests/DrawBatcherTest.cpp` 合并规则测试
- `tests/RenderCacheTest.cpp` 材质缓存常驻/LRU 测试
