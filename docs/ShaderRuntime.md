# Shader 资产与运行时隔离

Shader 资产层单向依赖运行时层。`src/render/shader/` 不包含资产或序列化头文件。

```text
ShaderAssetData --资产侧逐字段转换--> ShaderCreateInfo --> Shader
ShaderAsset -----------------------持有 Ref<Shader>-----> Shader
```

## 数据职责

- `asset/types/ShaderAssetData.h` 定义资产属性、接口、Pass、SubShader，以及独立的资产枚举。资产描述继承 `Transferable`；保留显示名、Range 范围、编辑器 attributes 和完整的持久化数据。
- `render/shader/ShaderTypes.h` 定义普通的运行时类型。`ShaderPropertyInfo` 仅含名称、类型、默认值；Range 转换为 Float，Color 转换为 Vec4。`ShaderCreateInfo` 是一次性的创建或重建输入，不在 Shader 中再完整保存一份。
- `Shader` 保存属性、全局属性、派生的 std140 布局和运行时 `SubShader / ShaderPass`。Pass 保存接口、渲染状态、去重排序后的 `keyword`，以及关键词组合到 `Ref<ShaderProgram>` 的哈希表；不持有 `ShaderProgramDesc`。编译源描述集中保存在 Shader 中。
- Material 保存实际材质数值和纹理绑定；GlobalUniformManager 保存全局属性的当前值；编译管线及 ShaderProgram 保存编译、反射及 GPU 程序资源。

## 关键词与程序选择

Material 通过 `enableKeyword / disableKeyword` 修改并保留全部启用关键词（包括当前 Shader 未声明的词），`variantKey(pass)` 与 Pass 的 `keyword` 声明求交集，未声明的词不会进入组合键。`program(pass)` 据此获取程序，绘制代码不再自行选择组合。同一 Pass 的多个材质使用相同组合时共享 Program；不同组合分别缓存，首次使用才编译和创建 GPU 资源。

程序表直接使用 `std::unordered_map<std::uint64_t, Ref<ShaderProgram>>`，没有额外缓存包装。键为最多 64 个声明关键词的位掩码；不再提供独立的 mesh/platform 变体维度。源码描述仍可由离线工具使用。Shader clone 创建空程序表；热重载遍历存活 Shader 清空其 Pass 程序表，并按 GPU 提交序号保留旧程序；关闭 GPU 存储时清空所有存活 Pass 的缓存。

## 生命周期与桥接

`ShaderAsset::instantiate()` 懒创建并强持有一个运行时实例，后续调用复用同一对象。资产释放后，外部 `Ref<Shader>` 仍可保持 Shader 存活。Shader 不保存资产回指、AssetId 或资产路径，也不参与资产反注册。

`asset/runtime/ShaderAssetResolver` 负责引用解析及资产关联查询。关联查询检查 AssetManager 已加载的资产缓存，不创建运行时实例。编辑器通过关联读取资产元数据；材质导出通过关联获取资产引用。未关联的运行时 Shader 使用基础属性控件，无法导出为资产 Shader 引用。

`ShaderAsset::clone()` 从当前资产描述创建独立对象；`Shader::clone()` 复制自身运行时快照。独立对象可以继续 clone，不随资产更新，也不登记为资产的唯一实例。

## 热重载

ShaderAsset 先将新载荷读入临时 ShaderAssetData，完成转换与运行时校验，再提交到已有 Shader；失败保留旧资产和运行时数据。成功提交保持 Shader 地址与 instanceId 不变，增加 revision，并更新属性、全局属性和派生布局。

引擎现有主线程资产事件处理阶段完成更新。资产事件按运行时实例刷新使用它的材质，并将编译失效请求排队到 GraphicsPipelineStorage 的帧刷新阶段；旧 GPU 程序和管线沿用提交序号延迟回收。渲染期间不可并发调用 rebuild。

## 编译标识

资产侧将规范化资产路径的 hash 作为不透明 `ShaderCompileKey` 传入运行时。离线工具和发布运行时使用同一标识、Pass、Stage、Variant 生成 SPIR-V 路径。clone 保留编译标识，但具有新的 instanceId。

开发时生成源码缓存使用 instanceId、revision、源路径、源码内容及阶段，避免不同运行时快照发生碰撞。GLSL/include 由文件依赖图失效；Shader JSON 由资产侧显式通知运行时实例失效。

资产 JSON 格式和 Shader Artifact v3 字段顺序及资产枚举序号保持兼容。
