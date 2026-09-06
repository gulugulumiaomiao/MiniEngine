# HandlePool

`HandlePool<Resource, HandleType>` 统一管理运行时对象的 Slot、generation 和 free list。目前由 `InstanceManager<Resource, HandleType>` 组合，ShaderManager 与 MaterialManager 通过继承它间接使用 HandlePool。

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

Handle 仍然只包含：

```cpp
struct Handle {
    std::uint32_t index;
    std::uint32_t generation;
};
```

## 创建与销毁

- `insert/emplace` 优先从 free list 弹出一个索引，free list 为空时才扩展 Slot 数组。
- `release` 校验 index、存活状态和 generation，销毁对象后递增 generation，并把 index 放回 free list。
- 旧 Handle 的 generation 与新 Slot 不同，因此无法错误访问复用后的对象。
- `clear` 一次性失效所有存活 Handle，并重建 free list，但保留 Slot 容量供下一次使用。

因此，复用一个已释放 Slot 从原来的线性扫描降为均摊 O(1)。

## Manager 使用方式

```cpp
class MaterialManager final
    : public InstanceManager<Material, MaterialHandle> {
public:
    MaterialHandle load(const VirtualPath& path) override;
};

MaterialHandle handle = MATERIAL_MANAGER.load(path);
Material* material = MATERIAL_MANAGER.find(handle);
MATERIAL_MANAGER.destroy(handle);
```

虚拟路径索引与 Manager 公共接口见 [InstanceManager.md](InstanceManager.md)。

带有 GPU 延迟销毁、retire serial 或文件时间戳的 Cache 暂不直接使用这个池。它们的 Slot 具有额外生命周期约束，后续应在保留延迟回收策略的前提下单独接入。
