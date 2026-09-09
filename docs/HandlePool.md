# HandlePool

`HandlePool<Resource, HandleType>` 统一管理运行时对象的 Slot、generation 和 free list。Handle 使用统一的强类型形式：

```cpp
struct MaterialHandleTag;
using MaterialHandle = Handle<MaterialHandleTag>;
```

## 数据结构

```text
HandlePool
├── slots
│   └── Slot
│       ├── optional<Resource>
│       └── generation
└── freeList
    └── 可立即复用的 Slot index
```

- `insert/emplace` 优先从 free list 取得索引，free list 为空时才扩展 Slot 容器。
- `release` 校验 index、存活状态和 generation，销毁对象后递增 generation，并把 index 放回 free list。
- 旧 Handle 的 generation 与复用后的 Slot 不同，因此不能访问新对象。
- `clear` 使所有存活 Handle 失效并重建 free list，同时保留 Slot 容量。

需要额外维护 `Key → Handle` 映射的 Manager 或 Cache 使用 [KeyedHandleRegistry.md](KeyedHandleRegistry.md)。Scene 和 VulkanDevice 等只需要 Handle 生命周期的容器可直接使用 HandlePool。
