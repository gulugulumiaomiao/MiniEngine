# Material 资产与运行时分层

MaterialAssetData 保存名称、Shader 路径、属性覆盖、纹理引用、keywords 和可选 renderQueue，独立于运行时类型。Artifact 保持 v2 格式。

MaterialCreateInfo 保存运行时参数：Ref<Shader>、数值属性、MaterialTextureBinding、keywords 和可选队列。纹理 binding 保存 Ref<Texture>、可选 Ref<Sampler> 或资产层注入的延迟解析回调。Material 不保存资产身份、路径或资产回指，也不参与序列化。

## 实例与重载

```text
resolveMaterialReference(path/id) [资产层]
  -> AssetManager.loadAsset<MaterialAsset>
  -> MaterialAsset.buildRuntimeInfo()
  -> MaterialAsset.instantiate()
  -> Ref<Material>
```

MaterialAsset 强持有唯一实例，重复 instantiate 返回同一对象，运行时可在资产释放后继续存活。MaterialAsset::clone 从资产数据创建独立实例；Material::clone 复制当前运行时状态，分配新 instanceId，GPU 缓存独立。

没有 MaterialManager、MaterialResourceManager、MaterialStorage 或 gpu/material 子目录。instanceId 仅用于排序和编辑器身份。资产关联查询由资产层 findMaterialAsset/materialAssetPath 扫描缓存完成，克隆不会自动写回源资产。

setShader 使用新 Shader 默认值重建，再迁移同名同类型的数值和纹理，保留 keywords 和 renderQueue override。Shader 原实例重载增加 revision，Material 使用时检查 revision，无需材质注册表。

MaterialAsset 原实例重载先校验参数，再按默认值和完整覆盖重建；删除覆盖恢复默认值。地址及 instanceId 保持，version 增加。解码或校验失败保留旧数据及实例，克隆不随原资产更新。

## GPU 反射绑定

Material::uniformSet(program, frameIndex) 使用 Program 的 set 1 反射布局。缓存按帧及 ShaderProgramId 分开，不能只按 descriptor layout hash 去重：同 descriptor 类型仍可能有不同 UBO 偏移和纹理名称。

- 没有材质资源时不创建、不绑定材质集。
- 布局完全一致时批量复制，否则按反射成员名称、类型、偏移和大小写入，允许属性子集和不同偏移。
- 未知成员、类型/大小不匹配或不支持的资源使绑定失败，Forward 回退到错误材质。
- 只写实际反射的纹理 binding，支持稀疏 binding 和 sampler 覆盖。
- version 和实际 view/sampler 签名决定更新。仍被帧引用的旧集不原地改写，创建新集保留已提交资源。

FrameGpuManager 按帧保活通用 RefCounted 资源，在所属帧 fence 完成后的 beginFrame 释放，不查找或缓存 Material。设备 uid 变化时材质清空旧 GPU 绑定并重新解析延迟纹理。

## 错误材质与关闭

Material::errorMaterial 是静态 Ref。资产系统就绪后由资产层直接初始化 error.material.json，初始化失败不递归回退。加载或 Forward Program/绑定失败使用它，错误材质本身失败则跳过绘制。

关闭项目先等待 GPU 空闲，清空错误材质及程序/帧/全局资源，再释放资产和布局缓存，最后销毁 Renderer/Device。外部持有的运行时对象用设备 uid 避免向新设备释放旧 RID。

## 验证

MaterialIsolationTest 覆盖原实例重载、删除覆盖、失败保留、Shader revision、反射子集、不同 Program/帧缓存、帧保活、稀疏纹理和 sampler 覆盖。MaterialAssetBoundaryTest 防止运行时依赖资产/序列化类型，旧 v2 Artifact fixture 验证逐字节兼容。
