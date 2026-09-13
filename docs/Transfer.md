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

核心序列化层提供 `Transferable` 抽象接口：

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

引擎与编辑器中所有实现 `bool transfer(Transfer&)` 的数据类型都直接或间接继承 `Transferable`，包括配置、项目注册表、资产与嵌套值类型。具体 Asset 通过 `Asset` 继承接口。`Transfer`/Reader/Writer 是归档器，其字段读写重载不属于这个数据接口。

`Transferable` 不提供 `operator==`；需要比较的派生类自行比较字段。新增继承会使原聚合类型不再支持聚合初始化，使用位置参数构造的类型需提供相应构造函数。

## 支持的数据

- 基础整数、浮点数、布尔值和字符串。
- Vec2、Vec3、Vec4、Mat33、Mat44 和 Quat。
- enum、vector、optional 和 variant。
- `std::vector<std::byte>` 与 `VirtualPath`。
- 继承 `Transferable` 并覆盖 `bool transfer(Transfer&)` 的自定义类型。

## 自定义类型

需要参与 Transfer 的数据类型直接声明成员接口：

```cpp
struct Example : public Transferable {
    std::string name;
    math::Vec3 position;
    std::vector<float> values;

    bool transfer(Transfer& archive) override;
};

bool Example::transfer(Transfer& archive) {
    return archive.beginObject({}) &&
           archive.transfer("name", name) &&
           archive.transfer("position", position) &&
           archive.transfer("values", values) &&
           archive.endObject();
}
```

每个数据类型的 `transfer` 自己负责 `beginObject({})`/`endObject()`。根对象直接调用 `value.transfer(archive)`，调用方不再包装根对象。嵌套对象使用 `archive.transfer("child", value)`：归档器仅定位字段，不创建对象，再通过 `Transferable` 调用其实现。数组、optional、variant 内的自定义对象同样遵循此约定，JSON 层级与 Binary 字段顺序不变。

不要在已写入其他字段的对象内直接调用另一个对象的 `transfer`，否则空名称会选中当前对象，JSON 写入时会清空它；命名子对象必须通过字段重载传输。字段作用域在成功或失败后都会恢复父位置，但错误仍保留，不能把传输失败当作成功。根对象失败后应丢弃归档器；只有明确的可选字段兼容逻辑可以清错继续。

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
