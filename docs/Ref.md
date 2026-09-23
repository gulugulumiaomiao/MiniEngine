# Ref 与 RefCounted

`Ref<T>` 是侵入式、原子计数的拥有型智能指针，配合 `RefCounted` 基类为堆对象提供共享所有权的生命周期管理。命名与 `RID`/`HandlePool` 一脉相承，对齐引擎既有的 Godot 术语渊源。

## 组成

- `RefCounted`（`src/core/base/RefCounted.h`）：内嵌 `std::atomic<uint32_t>` 计数的基类。计数从 0 开始，仅 `Ref<T>` 可增减，对象在计数归零时被 `delete`。被管理类型须**公有继承** `RefCounted`。
- `Ref<T>`（`src/core/base/Ref.h`）：拥有型智能指针模板，要求 `T` 派生自 `RefCounted`（`static_assert` 强制）。

## 语义

```text
计数初值 0
├── 第一个 Ref 采纳对象    -> +1
├── 拷贝 Ref（共享所有权） -> +1
├── 移动 Ref（转移所有权） -> 不变，源置空
└── Ref 析构 / reset      -> -1，归零则 delete
```

- 构造：默认与 `nullptr` 为空；`explicit Ref(T*)` 采纳裸指针并 +1（可安全地用 `this` 重建）；`makeRef<T>(args...)` 在堆上构造并返回首个 Ref（计数 1）。
- 拷贝共享（+1），移动转移（计数不变、源置空）。
- 隐式 upcast：`Ref<Derived>` 可转 `Ref<Base>`；`refCast<U>(ref)` 以 `static_cast` 在相关类型间转换（下行转换须自行保证有效）；`refDynamicCast<U>(ref)` 以 `dynamic_cast` 做运行时校验的下行转换，类型不符返回空 `Ref`（`AssetManager::loadAsset<T>` 用它从 `Ref<Asset>` 多态取回具体子类）。
- 访问：`get()`、`operator*`、`operator->`、`explicit operator bool`、`valid()`、`useCount()`。
- 比较：`operator==` 比较所指对象地址，支持与 `nullptr` 比较，C++20 自动合成 `!=`。

## 线程安全

计数为原子操作：`addRef` 用 `relaxed`，最后一次归零的 `releaseRef` 用 `acq_rel`，保证对象析构 happens-after 其它线程对它的访问。据此：

- 多线程各自持有**不同的 Ref 副本**并发增减计数是安全的。
- 并发读写**同一个 Ref 变量**不安全（与 `std::shared_ptr` 一致）。
- 被管理对象 `T` 自身成员的线程安全由 `T` 负责，`Ref` 不提供。

## 与 HandlePool 的关系

`Ref`/`RefCounted` 管理的是**堆对象的共享所有权**；[HandlePool](HandlePool.md) 与 [KeyedHandleRegistry](KeyedHandleRegistry.md) 管理的是**分代弱句柄 + 单一所有者**（`release()` 立即销毁并递增 generation，使旧句柄失效）。二者正交：`Ref` 不介入 GPU 资源的延迟/fence 回收路径，句柄机制也不做引用计数。需要共享所有权时在更高层使用 `Ref`，需要弱引用与单一所有者时使用句柄。

## 用法示例

```cpp
struct Node : engine::RefCounted {
    explicit Node(int id) : id(id) {}
    int id;
};

engine::Ref<Node> a = engine::makeRef<Node>(7); // 计数 1
engine::Ref<Node> b = a;                         // 计数 2，共享同一对象
b.reset();                                       // 计数 1
// a 离开作用域 -> 计数 0 -> Node 被销毁
```
