# KeyedHandleRegistry

`KeyedHandleRegistry<Resource, HandleType, Key, KeyHash>` 是组合 `HandlePool` 与主键索引的抽象基类。它不关心资产加载或 GPU 创建，只统一管理运行时对象、Handle 生命周期和 `Key → Handle` 映射。

## 公共接口

- `insert(resource)`：调用 `validate()`，通过 `keyOf()` 取得主键，按主键去重后插入 HandlePool。
- `find(handle)`：校验 index 和 generation，返回资源指针；Handle 失效时返回 `nullptr`。
- `find(key)`：先查询主键索引，再解析 Handle。
- `findHandle(key)`：返回主键对应的活动 Handle。
- `destroy(handle/key)`：移除主键索引并释放 Slot，使旧 Handle 失效。
- `clear()`：清理主键索引和 HandlePool。
- `size()`：返回活动资源数量。

派生类实现 `keyOf(resource)`，并可覆盖 `validate(resource)`。具体 Manager 自行实现 `load()`、热重载和领域校验。

```text
MeshManager / MaterialManager / ShaderManager / TextureManager
                         │ 继承
                         ▼
              KeyedHandleRegistry
              ├── HandlePool
              │   ├── Slot + generation
              │   └── free list
              └── 自定义 Key → Handle
```

Shader 编译阶段的 `CompiledShaderCache` 和 `ShaderProgramCache` 也继承该类型，分别使用编译结果 ID 和程序 ID 作为主键。仅需要 HandlePool、不需要主键索引的 Scene 与 VulkanDevice 继续直接使用 HandlePool。
