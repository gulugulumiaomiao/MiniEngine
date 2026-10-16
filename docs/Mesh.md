# Mesh 资产与运行时分层

MeshAssetData 保存可序列化的描述、顶点/索引数据和可选生成配方。MeshCreateInfo 直接保存名称、布局、索引类型、usage、topology、SubMesh、包围盒、CPU 副本选项和 MeshData，供 Mesh::create/rebuild 使用。Mesh 本身分别保存必要状态，运行时没有描述包装层。资产侧 MeshAssetDesc 仅用于原 v4 序列化格式。两层的布局、包围盒、SubMesh、图元和配方结构分别定义，通过资产层显式转换；运行时类型不带 Transferable，也不引用资产身份。

## 资产与实例

```text
resolveMeshReference(path/id)
  -> AssetManager.loadAsset<MeshAsset>
  -> MeshAsset.buildRuntimeInfo()
  -> MeshAsset.instantiate()
  -> Ref<Mesh>
```

MeshAsset 强持有唯一 Ref<Mesh>，重复 instantiate 返回同一对象。Mesh 无资产回指、路径或 GUID，资产释放后外部持有的 Mesh 仍可使用。findMeshAsset/meshAssetPath 只在资产层扫描缓存，服务编辑器和场景导出。克隆和纯运行时网格没有源资产关联。

加载 MeshAsset、转换数据及生成图元不需要 GPU；Mesh::create/rebuild 创建 GPU 缓冲时需要活动设备。没有 MeshManager、MeshStorage 或运行时对象注册表。

## 运行时图元

```cpp
MeshBuildRecipe recipe;
recipe.parts.emplace_back(PlaneGeometry{{8.0F, 8.0F}, 4, 4});
recipe.keepCpuCopy = true;
auto info = MeshBuilder::build(recipe);
Ref<Mesh> mesh = info ? Mesh::create(std::move(*info)) : Ref<Mesh>{};
```

MeshBuilder::build 直接返回 MeshCreateInfo，不创建临时资产。资产导入通过资产层 buildMeshAsset 包装生成结果。MeshComponent 保存运行时配方，MeshComponentAsset 保存独立的序列化配方。

保留 Plane、Box、UvSphere、Cylinder、多部件变换、material slot、镜像切线和索引策略。图元重建调用原实例 rebuild，成功后才清除 dirty；失败保留旧网格，后续可以修正配方后重试。

## 完整重建与数据更新

rebuild 校验布局、流大小、顶点数、索引引用和 SubMesh 范围，准备完整的新 GPU 缓冲后再提交新状态。它更新布局、容量、索引格式、usage、topology、SubMesh 和包围盒，保持 instanceId，增加 revision。校验或分配失败保留旧状态。

upload 更新既有布局和容量下的数据。改变流数量、顶点/索引数量、布局、SubMesh 或包围盒时使用 rebuild。成功更新同步保留的 CPU 数据并增加 revision。

MeshAsset 热重载先解码和校验，再调用已有 Mesh 的 rebuild，最后提交资产数据。失败不破坏旧资产或实例；完整 Artifact 还会在 AssetManager 中预检尾部数据。资产热重载不更新已经创建的独立克隆。

## GPU 快照与帧保活

Mesh 持有 Ref<MeshGpuBuffers>，其中保存 vertex/index RID、容量及设备 UID。DrawListBuilder 保留当前缓冲快照，交给 FrameGpuManager 的通用 retain 列表。即使 Mesh 被销毁或重建，旧缓冲也活到所属帧 fence 完成后的 beginFrame。

- Static 使用 DeviceLocal 持久缓冲，通常通过 rebuild 替换。
- Dynamic 使用 Upload 内存；仍有外部快照或提交帧引用时，upload 创建新缓冲，避免覆盖正在使用的数据。
- Stream 每次 upload 获取新的设备瞬态缓冲，设备记录引用它的 command buffer，在所有相关提交完成且没有未提交的录制引用后回收；取消未提交的录制也会释放其独占缓冲。Mesh 不主动销毁，调用方仍需每帧生产并上传数据。

关闭项目先等待 GPU 空闲再释放帧资源。缓冲仅在创建它的设备 UID 仍活动时释放，避免旧 RID 操作到新设备。

## CPU 副本与克隆

keepCpuCopy=true 时 Mesh 保留运行时几何数据，Mesh::clone 复制当前状态并创建独立 GPU 缓冲，支持再次克隆；设备更换后可从 CPU 副本重建缓冲。

keepCpuCopy=false 时 Mesh 不保留完整几何数据，运行时 clone 返回空，也不会查询资产或进行 GPU 回读。调用方可以提供新数据重建；MeshAsset::clone 始终能从资产数据创建独立网格，instantiate 也可用资产数据恢复原实例的 GPU 资源。

## 兼容性与测试

Mesh Artifact 保持 v4 的 magic、字段顺序、枚举值和图元编码，场景源格式保持兼容。mesh_legacy_v4.artifact 是修改前的独立样本，测试要求读写载荷逐字节一致。

MeshIsolationTest 覆盖类型边界、唯一实例、资产释放、CPU 副本、克隆、完整重建、分配失败、帧保活、Stream、图元失败保留及跨设备行为。MeshAssetBoundaryTest 防止运行时重新依赖资产或序列化接口，Vulkan 集成测试验证实际重建及项目切换。
