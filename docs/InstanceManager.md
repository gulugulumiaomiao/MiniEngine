# InstanceManager

`InstanceManager<Resource, HandleType>` 是 ShaderManager 和 MaterialManager 共用的抽象基类。内部组合 `HandlePool` 和虚拟路径索引，统一运行时实例的生命周期与缓存行为。

## 公共接口

- `insert(resource)`：校验实例，按资源虚拟路径去重，然后插入 HandlePool 并返回 Handle。
- `load(path)`：抽象接口，由具体 Manager 通过 AssetManager 加载对应 Asset、实例化后调用 `insert`。
- `destroy(handle)`：删除路径索引并从 HandlePool 释放实例；Slot 进入 free list，generation 增加。
- `find(handle)`：校验 index 和 generation，返回 Pool 中的实例指针，失效时返回 `nullptr`。
- `find(path)`：通过规范化虚拟路径索引找到 Handle，再返回 Pool 中的实例指针。
- `clear()`：清空虚拟路径索引和 HandlePool，使所有现存 Handle 失效。
- `size()`：返回 Pool 中活动实例数。

具体 Manager 只实现 `load()`、`pathOf()` 和必要的 `validate()`，Shader 替换、Material Shader 切换等领域逻辑仍保留在各自 Manager 中。

```text
ShaderManager / MaterialManager
             │ 继承
             ▼
       InstanceManager
       ├── HandlePool
       │   ├── Slot + generation
       │   └── free list
       └── VirtualPath -> Handle
```
