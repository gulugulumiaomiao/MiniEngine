# Scene、Node 与 Component

第一版场景系统负责维护对象层级、组件生命周期以及渲染数据提取。`Scene` 是拥有者，使用带 generation 的 `NodeHandle` 管理 Node。每个 Scene 创建时都会创建一个永久 Root Node；普通 Node 默认挂到 Root，销毁父节点会递归销毁全部子节点，旧 Handle 随即失效。

## Node 层级

每个 Node 保存名称、激活状态、父 Handle、子 Handle 列表和独占的 Component。`Node::setParent` 同时提供 `NodeHandle` 和 `Node&` 版本；Node 版本会拒绝跨 Scene 设置父级。两个版本都会拒绝无效 Handle、自身作为父节点以及循环层级；Handle 版本传入空 Handle 表示重新挂到 Scene Root。更换父节点保留局部变换，不保留原世界变换。Root 不允许更换父级，也不能通过 `Scene::destroyNode` 删除；`Scene::clear` 只销毁 Root 的全部子树。

Node Pool 使用稳定地址的 slot 容器，创建其他 Node 不会让已经取得的 `Node*`/`Node&` 失效；对应 Node 被销毁后，外部仍应依靠 `NodeHandle` 重新查询，不能继续使用原指针。

每个 Node 创建时自动附加一个不可删除的 `TransformComponent`。世界矩阵按 `parentWorld * localMatrix` 计算；局部变换变化或重新设置父节点时，由 `Node::markTransformDirty` 将当前节点及后代的世界矩阵标记为脏。Scene 的组件更新、Transform 更新都只从 Root 开始递归遍历。

## Component 生命周期

组件由 Node 通过 `std::unique_ptr` 独占，同一 Node 第一版只允许一个相同具体类型。生命周期顺序如下：

```text
添加：onAttach -> onEnable（节点在层级中激活时）
更新：onUpdate（组件有效激活时）
禁用：onDisable
删除：onDisable（如有需要）-> onDetach -> 析构
```

`active()` 等于组件自身 `enabled()` 与所属 Node 的 `activeInHierarchy()` 同时为真。父节点停用会递归停用子树组件。

## 渲染组件

- `MeshComponent` 保存非拥有的 `MeshHandle`，以及可见性、阴影和 layer mask。
- `MaterialComponent` 保存非拥有的材质槽列表。请求的槽无有效材质时回退到 slot 0；slot 0 也不存在时返回无效 Handle。
- `CameraComponent` 支持透视/正交投影、裁剪面、清屏颜色、culling mask、primary 和 priority。多个有效相机优先选择 primary，相同类别选择 priority 更高者。
- `LightComponent` 支持 Directional、Point 和 Spot 数据；当前渲染器把提取到的第一盏方向光写入每帧 Scene UBO。

Mesh 与 Material 的运行时资源仍由各自 Manager 管理，组件销毁不会隐式销毁共享资源。

## 渲染提取

`Scene::buildRenderScene` 从 Root 递归遍历有效 Node，把同一 Node 上的 Transform、Mesh 和 Material 组合成 `RenderObject`，同时选择 Camera 并收集 Light。`RenderScene` 是每帧重建的渲染快照，Renderer 不直接依赖 Node 或 Component。

```text
Application::onUpdate
  -> Scene::update
  -> Scene::buildRenderScene
  -> Renderer::renderFrame
  -> RhiRenderBackend -> RHI
```

场景 UBO 使用 set 0/binding 0，保存 view-projection、相机位置和第一方向光；对象 Transform SSBO 移到 set 0/binding 1。Material 仍使用 set 1。示例 Shader 已使用相机矩阵，并以二维表面的固定法线演示第一版方向光漫反射；加入带 Normal 的 Mesh 后可以直接替换为真实逐顶点或逐像素法线。
