# Transfer 序列化系统

`Transfer` 为资产提供与数据格式无关的字段读写接口：

```text
Transfer
├── Reader
│   ├── BinaryReader
│   └── JsonReader
└── Writer
    ├── BinaryWriter
    └── JsonWriter
```

资产层另外提供 `Transferable` 抽象接口：

```cpp
class Transferable {
public:
    virtual ~Transferable() = default;
    virtual bool transfer(Transfer& archive) = 0;
};

class Asset : public Transferable {
    // Asset 身份和虚拟路径
};
```

所有具体 Asset 都通过 `Asset` 继承 `Transferable`，并必须实现自己的 `transfer()`。普通值类型不需要继承该接口，只要提供同名成员方法即可，避免为 Vertex、Node 数据等小对象增加虚表。

## 支持的数据

- 基础整数、浮点数、布尔值和字符串。
- Vec2、Vec3、Vec4、Mat33、Mat44 和 Quat。
- enum、vector、optional 和 variant。
- `std::vector<std::byte>` 与 `VirtualPath`。
- 提供 `bool T::transfer(Transfer&)` 成员方法的自定义类型。

## 自定义类型

需要参与 Transfer 的数据类型直接声明成员接口：

```cpp
struct Example {
    std::string name;
    math::Vec3 position;
    std::vector<float> values;

    bool transfer(Transfer& archive);
};

bool Example::transfer(Transfer& archive) {
    return archive.transfer("name", name) &&
           archive.transfer("position", position) &&
           archive.transfer("values", values);
}
```

`Transfer::transfer(name, value)` 会建立对象作用域，再调用 `value.transfer(*this)`。不再使用 ADL 或自由函数 `transferValue`。

接口不是 `const`，因为读取与写入共用同一个签名：Reader 修改字段，Writer 只读取字段。

## Binary 格式

- 固定使用小端序。
- string、字节块和 vector 使用 `uint32` 长度前缀。
- 字段名不写入二进制，读写字段顺序必须一致。
- Reader 会限制字符串、容器和字节块大小，并检查输入是否完整消费。

## JSON 格式

`JsonWriter` 使用字段名构建 JSON；`JsonReader` 按字段名读取并验证类型、整数范围、数组长度和浮点有限性。

ShaderLab、Material、Mesh 和 Scene 的源文件仍由对应 Importer 解析，因为源格式包含字符串枚举、默认值、相对路径和兼容规则。它们生成的 Artifact 使用同一套 Transfer 接口。

## 错误处理

Transfer 保存第一次格式错误。底层 Reader/Writer 不直接输出日志，也不调用 Fatal；Asset 或 Importer 在边界记录 Error 并返回失败。资产反序列化先写入临时对象，全部读取和验证成功后才替换原对象。
