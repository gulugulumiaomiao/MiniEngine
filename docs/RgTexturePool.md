# 全局纹理池

RgTexturePool 对同一设备的单图形队列提供按描述匹配的物理纹理缓存。
它不再按帧槽分池。纹理由 engine::Texture 持有，状态由池条目管理。

## 创建与复用

acquire 接收 engine::TextureDesc，使用已解析的尺寸、类型、格式、颜色空间、层数、mip、
实际 usage、数值 samples 和 sampler 设置生成哈希 key。debugName 不影响兼容性。
`acquire(desc, usage)` 接收图编译推导的实际用途，不同用途分别缓存；tiling 固定 Optimal，无需进入 key。
动态尺寸 resolver 单独保存在 RenderGraph 中，解析完成后才向池请求资源。
匹配组的空闲栈非空时直接取出；否则由池通过 Texture 私有创建入口传入描述和实际 usage，创建新纹理。
拥有条目的 unique_ptr 保证 Entry 地址稳定，unordered_map 扩容也不移动分组对象。
新纹理状态为 Undefined；复用纹理保留最后一次实际提交的 state/access。

## 状态与提交批次

```text
Available -> Leased -> PendingSubmit -> InFlight -> Available
                         |                |
                     取消批次          GPU 完成
```

- Leased：CPU 正在使用条目。未录制使用的租借释放后直接回到 Available。
- PendingSubmit：批次已录制该纹理的使用，CPU 释放也不能让它再次可用。
- InFlight：实际提交成功，等待 GPU 完成。GPU 完成时仍有 CPU 租借则回到 Leased；
  最后一个必要条件满足时才进入空闲栈。
- 当前批次内不重新租借 PendingSubmit 纹理，也不进行资源内存 aliasing。

beginBatch 开始一个 command buffer 的录制事务。RenderGraph::execute 将池纹理登记到
事务，并暂存外部 trackedState。后续图查询同一状态槽时看到暂存状态，但实际槽尚未修改。
onSubmitted 接收实际图形队列提交编号，确认池纹理和外部状态；此步骤不分配内存。
GPU retirement 记录在 beginBatch 时预先分配，防止提交成功后因内存分配失败丢失跟踪。

取消必须先丢弃原命令，保证不会再提交，然后 cancelBatch。它保持所有原始同步状态，
释放已结束租借的条目，仍有 CPU 租借的条目由后续 release 归还。外部 trackedState 的
所有者必须存活到事务提交或取消。release 检查重复归还，不修改 GPU 完成状态。

## GPU 完成跟踪

VulkanDevice::submit 为每次图形队列提交分配递增的 SubmissionId。
completedSubmission 查询 fence，返回已完成的连续提交前缀；不会等待 GPU。
未提供 fence 的提交使用设备持有的 fence，查询完成后销毁。
提供外部 fence 的调用方必须在 reset 或销毁 fence 前查询完成进度。
VulkanSwapchain::beginFrame 在等待后、reset 前完成这一步；waitIdle 也更新完成进度。

collect 按提交编号从 retirement 队列移出完成的批次，把无 CPU 租借的条目压回空闲栈。
SubmissionId 属于当前设备的图形队列，不能用于其他设备或队列。
present 的 OutOfDate 可以发生在提交成功之后，这种批次仍需正常确认并等待 GPU。

Renderer 管理上述流程。回调异常时 VulkanSwapchain::cancelFrame 重置原 command buffer，
录制安全清屏与 Present 转换，提交并归还已获取的图像。这个备用提交不使用已取消的池纹理，
也使当前帧槽的 fence 得到信号，避免后续 beginFrame 永远等待。

## 回收与销毁

trim 只清理 Available、达到闲置批次年龄且没有外部 Ref<Texture> 的条目。
删除前通过 RgRenderPassCache 失效引用其 view 的 framebuffer。
Renderer 默认闲置 120 个批次后回收；引用计数不能替代租借或 GPU 完成跟踪。
销毁池之前必须完成所有批次、释放租借并等待 GPU；设备 UID 改变时清空旧设备缓存，
Texture 的设备 UID 检查避免向新设备销毁旧句柄。
