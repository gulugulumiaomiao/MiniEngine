# Render GPU 资源分层

资源按自身生命周期选择所有权，不要求统一的 Manager/Cache/Factory。

| 资源 | GPU 所有者与入口 |
|---|---|
| Shader Program / Pipeline | GraphicsPipelineStorage 编译并缓存程序和管线 |
| Material set 1 | Material::uniformSet(program, frameIndex) |
| 场景/对象/实例表/阴影 set 0 | FrameGpuManager |
| 全局属性 set 2 | GlobalUniformGpuManager |
| Mesh | Mesh 持有 vertex/index buffer RID |
| Texture | Texture 持有 texture、默认 view、默认 sampler RID |

资源经 rhi::IDevice 操作，不依赖 Vulkan 类型。后端负责 RID 池和底层生命周期。Pipeline key 包含 Program、顶点布局和 RT 格式，RenderState 由命令设置动态状态。

Material 不经过 MaterialStorage。CPU 根据 Shader 属性布局存储，GPU 根据 Program 实际反射上传，同 descriptor layout 不代表同 UBO 布局。详见 [MaterialSystem.md](MaterialSystem.md)。

UniformSet 持有 Ubo、Texture 和 Sampler，FrameGpuManager 的通用 retained Ref 列表让资源活到所属帧 fence 完成。对仍被引用的旧集创建新集，避免原地改写。beginFrame 位于 Renderer 等待帧槽 fence 后、DrawListBuilder 创建新绑定前。

Texture/Mesh 由资产唯一实例复用。默认 Texture 按设备 uid 重建，Sampler 按描述去重。TextureDesc/SamplerDesc 不含序列化接口，资产序列化包装位于 asset/types/TextureAsset.h。

关闭项目先等待 GPU 空闲，清空错误材质和程序/帧/全局资源，再释放资产和共享布局，最后关闭 Renderer/Device。设备 uid 防止旧对象析构时误释放新设备同号 RID，Device 析构清空其全部底层池。
