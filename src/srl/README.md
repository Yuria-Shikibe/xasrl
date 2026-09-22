# srl

`srl` 是一个使用 C++26 静态反射实现的游戏对象序列化实验。

## 文件结构

- `types.hpp`：公开基础类型、wire header、错误码与错误对象。
- `annotations.hpp`：字段、原子类型和容器注解。
- `container.hpp`：容器 traits、容器适配约束与插入工具。
- `context.hpp`：引用转换 handler 与序列化上下文。
- `codec/base.hpp`：`atomic_codec` 接口及基础类型、字符串特化。
- `codec/reflected.hpp`：由反射成员自动生成的原子 codec 特化。
- `schema.hpp`：可序列化概念、schema 静态校验和上下文需求分析。
- `detail/reflection.hpp`：纯反射遍历、注解读取与类型分析工具。
- `detail/encode.hpp`：记录、容器和根节点的编码计划实现。
- `detail/decode.hpp`：记录、容器和原子值的解码实现。
- `detail/utility.hpp`：无状态 wire、枚举、哈希和 trivial byte 工具。
- `detail/wire_parse.hpp`：已验证 entry 视图与 wire 解析。
- `storage/byte_buffer.hpp`：序列化结果使用的字节缓冲。
- `serialization.hpp`：`pack`、`unpack`、`extract` 等公共 API。
- `wire.hpp`、`core.hpp`：保持稳定 include 路径的聚合入口。
- `io.hpp`：磁盘块读写以及 `save`、`load`。

## 对象层级

对象自上而下分为：

1. 游戏运行时对象：可能包含指针、句柄、缓存等运行时状态。
2. 游戏元数据对象：脱离运行时环境后仍有意义的数据。
3. 已序列化内存对象：`entry_head + storage_mapping[] + content`。
4. 已存储磁盘块：`storage_head + serialized entry`。

库实现第 2、3、4 层，并支持字段级运行时表示与持久化表示转换。转换的业务语义由
游戏代码提供，库负责调用、上下文传递和字节编解码；未标记字段不会进入二进制。

## 反射记录

非原子记录只序列化带有稳定 ID 的成员和直接非虚基类：

```cpp
struct Person {
    [[=srl::tag::field{.id = 1}]]
    int age{};

    [[=srl::tag::field{.id = 2}]]
    double score{};

    void *runtime_object{}; // 不参与序列化。
};
```

反射在编译期生成 ID 到字段的映射规则。重复 ID、没有转换规则的指针、引用、虚基类、
bit-field 和没有序列化规则的字段都会使 `schema_valid<T>()` 返回 `false`，并在调用
`pack` 时触发编译期错误。

解码记录时，以目标类型的字段 ID 为准：目标需要的字段必须存在，源对象中的未知字段
会被忽略。因此同一份二进制可以转换为多个字段兼容的元数据类型。

## 字段表示转换

`srl::conversion_tag` 将字段的内存类型 `MemTy` 映射到编译期确定的持久化类型 `SrlTy`：

```cpp
struct conversion_tag {
    std::meta::info target_type;
    const void *handler_id = nullptr;
};
```

保存路径为 `MemTy -> SrlTy -> bytes`，读取路径相反。标记不代替 `tag::field`，必须显式
指定字段 ID；旧实验名称 `convertion_tag` 已修正为 `conversion_tag`。

### 直接转换

```cpp
struct RuntimeCount {
    int value{};
    RuntimeCount() = default;
    explicit RuntimeCount(int value) : value(value) {}
    explicit operator int() const { return value; }
};

struct Counters {
    [[=srl::tag::field{1}]]
    [[=srl::conversion_tag{^^int}]]
    RuntimeCount count;
};
```

`handler_id == nullptr` 时使用 `static_cast<SrlTy>(const MemTy&)` 和
`static_cast<MemTy>(SrlTy&&)`。允许显式构造和转换运算符，不要求两端都提供重复的接口。
双向转换与字段赋值必须在编译期合法；业务语义是否可逆由协议保证，转换抛出的异常直接传播。
当前 GCC 不支持直接反射某些 using-declaration，例如 `^^std::uint32_t`；可以先声明
`using StoredId = std::uint32_t;`，再使用 `^^StoredId`。

### 上下文转换

```cpp
struct Asset;
struct AssetReferenceKey {};

struct RenderState {
    [[=srl::tag::field{1}]]
    [[=srl::conversion_tag{^^std::string, srl::handler_key<AssetReferenceKey>()}]]
    Asset *asset{};
};

class AssetHandler : public srl::typed_reference_handler<Asset*, std::string> {
    std::expected<void, srl::error> to_storage(Asset *const &mem, std::string &srl) override;
    std::expected<void, srl::error> from_storage(Asset *&mem, const std::string &srl) override;
    // Implement using the application's asset registry.
};

AssetHandler handler;
srl::serialization_context context;
context.register_handler(srl::handler_key<AssetReferenceKey>(), handler);
// auto bytes = srl::pack(state, context);
// auto restored = srl::extract<RenderState>(bytes->span(), context);
```

上下文使用非拥有的 `unordered_map<const void*, reference_wrapper<context_reference_handler>>`。
`handler_key<Tag>()` 返回每种标记类型对应的静态对象地址；也可以使用独立静态对象的地址，
例如 `inline constexpr char asset_key{};` 和 `&asset_key`。注解中的地址必须可用于常量求值。
同一 `MemTy/SrlTy` 类型对可以注册多个不同业务标识。

底层接口为纯虚函数 `mem2srl(const void*, void*)` 和 `srl2mem(void*, const void*)`，均返回
`expected<void, error>`。参数指向已构造的类型对象，不是原始未初始化存储。对于指针字段，
`mem` 指向存放指针的字段，而不是被引用对象。handler 只能赋值，不得 placement new、
销毁参数对象或保留参数地址。强类型适配基类完成擦除与恢复，业务代码通常不必自行强转。
需要直接覆写擦除接口时，派生类必须通过基类的两个 `std::in_place_type` 参数声明类型对。

注册禁止空键和重复键，不覆盖旧 handler。字段绑定时检查 handler 声明的类型对；空键注册
抛出 `invalid_argument`，缺失 handler、重复注册和类型对不匹配抛出 `logic_error`。
业务错误使用 `invalid_reference`、`unresolved_reference` 等 `error_code`，库补充字段 ID
和可用的偏移；其他异常不吞掉。合法空引用由协议明确定义，不得把未知非空 ID 静默变成空引用。

### 生命周期与接口边界

- handler 及其依赖服务必须覆盖上下文的使用期。上下文不拥有、不复制、不销毁 handler。
  操作期间不得改变注册表，不承诺并发安全；键只在单一链接映像内有效，不支持跨 DLL 或热重载。
- handler 只解析外部已有对象，不创建对象图，不修改外部业务状态。恢复的引用必须由外部系统
  保证存活，不能指向临时 `SrlTy` 或解码临时副本。
- `unpack` 仍使用临时副本加交换，错误或异常不会改变目标；`decode_root` 允许部分更新。
  handler 自身在所有失败路径中也必须保持参数类型的不变量。
- `pack(value, context)`、`serialized_size(value, context)`、`pack_into(bytes, value, context)`
  和 `save(path, value, context)` 均支持上下文；`extract<T>(bytes, context, options)`、
  `unpack(bytes, out, context, options)`、`decode_root(bytes, out, context, options)`、
  `load<T>(path, context, options)` 的 `options` 可省略。
- 无上下文重载保留，但仅接受递归检查后不需要 handler 的 schema。可用
  `requires_context<T>()` 查询合法 schema 是否需要上下文。
- 一次保存先创建递归编码计划，持有转换结果，再使用同一结果测量与写入；普通字段不额外复制。
  `serialized_size` 与随后 `pack_into` 是独立操作，不复用转换结果。整个调用期间，源对象、
  容器迭代顺序和外部解析环境必须稳定，库不对外部系统提供快照或锁定。

转换仅作用于普通可写成员，禁止引用、const/volatile 成员、bit-field、基类转换、重复标记、
与字段强制 `atomic` 并用，以及自动原子类型内部的转换。`SrlTy` 必须是可默认构造、可移动
的可序列化值类型，`MemTy` 必须可赋值；编译期拒绝循环 schema。

字段转换针对整个字段，不自动传播到容器元素；但转换后的记录或容器内部可以继续包含转换字段。
字段的 `tag::container` 检查并作用于转换后的 `SrlTy`。字段 ID 不变，payload 按 `SrlTy`
编码，有嵌套 entry 时使用 `SrlTy` 的类型信息。handler 地址绝不持久化。改变 `SrlTy` 需要
显式字段 ID 或业务版本迁移；现有原子字段没有独立类型信息，不能依赖类型名称哈希自动检测。

## 容器

容器是独立于原子和反射记录的第三类 entry。库自动识别可重复遍历、可清空并可重新插入
元素的 owning range，并根据容器能力选择 `reserve`、尾插、普通插入或 `insert_after`。
`std::array` 和原生数组 `T[N]` 作为固定长度容器单独处理；`std::string` 仍使用原子
codec，`std::span` 和 range view 不会被当作可反序列化容器。原生数组可作为字段使用，
根数组则通过 `pack`/`unpack` 往返；`extract<T>` 不能返回 C++ 原生数组值。

内建规则覆盖 `array`、`vector`、`deque`、`list`、`forward_list`、set/map 系列及其
unordered 和 multi 变体。单值容器使用一个 mapping 槽位表示一个元素；map 类容器使用
两个连续槽位表示一个逻辑元素，其中偶数槽位是 key，随后槽位是 mapped value。

相同 mapping group size 的容器可互相读取，例如 `vector<T>` 到 `list<T>`、`map<K,V>`
到 `unordered_map<K,V>`。unordered 容器按实际迭代顺序写入，因此不保证相同逻辑内容
产生相同字节序列。

标准风格的自定义容器通常不需要注册。特殊容器可以特化 `container_traits<T>`，继承
`default_container_traits<T>` 后只补充缺失操作：

```cpp
template<>
struct srl::container_traits<MyContainer>
    : srl::default_container_traits<MyContainer> {
    static void clear(MyContainer &value);
    static void reserve(MyContainer &value, std::size_t size); // 可选
    static void insert(MyContainer &value, element_type element);
};
```

解码唯一 set/map 时默认沿用容器语义去重。根容器可通过 `decode_options` 请求拒绝重复项；
反射字段可用独立 tag 覆盖该策略：

```cpp
[[=srl::tag::field{.id = 1}]]
[[=srl::tag::container{.duplicates = srl::duplicate_policy::reject}]]
std::set<int> ids;
```

## 原子类型

原子类型是序列化树的叶节点，其 content 中不再启动嵌套 entry。用户定义类型在类型声明
处标记：

```cpp
struct [[=srl::tag::atomic{}]] Vec2 {
    float x{};
    float y{};
};
```

`atomic` 标记包含三个选项：

```cpp
struct atomic {
    bool propagate{true};
    bool allow_padding{false};
    bool bypass_pointer_check{false};
};
```

原子属性默认允许向外传播。一个自动生成原子 codec 的类型，其每个成员都必须已有原子
codec，并且允许传播；整数、浮点、枚举和 `bool` 等原生类型天然允许传播，不需要额外标记。
若某个原子类型不应成为更大原子对象的一部分，可声明
`[[=srl::tag::atomic{.propagate = false}]]`，使用它作为成员的类型便不能再自动标记为原子。

自动生成的 codec 按以下顺序选择二进制写法：

1. `allow_padding = true`：直接复制完整对象表示，包括 padding；类型必须 trivially copyable。
2. `allow_padding = false` 且 `std::has_unique_object_representations_v<T>`：仍直接复制完整对象
   表示；类型必须 trivially copyable。
3. 其余情况：按成员声明顺序逐个调用成员的原子 codec，不写入类型自身的 padding；成员
   codec 必须是固定大小，但外层类型本身不要求 trivially copyable。

自动生成还要求类型没有基类、引用、bit-field 或字段标记。直接复制完整对象表示时，库会
递归检查对象表示中是否包含指针或成员指针并默认拒绝，避免把进程地址直接写入 wire。
只有明确安排了解码后的指针修复时，才应使用
`[[=srl::tag::atomic{.bypass_pointer_check = true}]]` 跳过该检查；该选项不会提供地址修复、
跨进程有效性或可移植性。库在编译期要求目标使用 8-bit byte 和小端序，不提供大端兼容
或运行时字节序转换。

无法修改声明的第三方平凡类型可以在字段上附加 `atomic`，强制该字段直接复制对象表示：

```cpp
struct Record {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::atomic{}]]
    third_party::trivial_value value;
};
```

字段级强制原子要求类型 trivially copyable，并且默认要求独一对象表示；若确认 padding 也
属于约定的 wire representation，可设置 `.allow_padding = true`。指针检查和 bypass 规则
与类型级直接对象复制一致。

整数、浮点、枚举、`bool` 和 `std::string` 是内建原子类型。变长或特殊类型可显式特化
`srl::atomic_codec<T>`。自定义 codec 先通过 `encoded_size(value)` 报告准确大小，随后直接
对调用方提供的 `std::span<std::byte>` 和 `std::span<const std::byte>` 编解码，不创建中间
payload buffer。自定义类型仍应在声明处标记 `[[=srl::tag::atomic{}]]`。

## 内存格式

线格式直接使用受支持小端目标的对象表示。header 和 mapping 是固定布局的
trivially copyable 类型，完整复制其对象表示；atomic content 则按上节的三条规则选择完整
对象复制或逐成员写入。因此只有显式允许 padding，
或类型具有独一对象表示时，复合原子类型才会把完整对象表示直接复制到文件。

```cpp
struct entry_head {
    std::uint32_t size;               // 整个 entry 的字节数。
    std::uint32_t mapping_count;
    std::uint8_t mapping_align;
    entry_kind kind;
    mapping_kind mapping;
    std::uint8_t mapping_group_size;
    std::uint32_t type_raw_name_hash;
};

struct entry_mapping {
    std::uint32_t id;
    std::uint32_t aligned_address;    // content 偏移 / mapping_align。
};
```

线格式中的 `entry_head` 固定为 16 字节：

| Offset | Size | 内容 |
| --- | ---: | --- |
| 0 | 4 | entry 总大小 |
| 4 | 4 | mapping 数量 |
| 8 | 1 | mapping alignment |
| 9 | 1 | entry kind：atomic、record 或 container |
| 10 | 1 | mapping kind：none、field 或 element |
| 11 | 1 | mapping group size：0、1 或 2 |
| 12 | 4 | 原始类型名称哈希 |

合法组合固定为：原子 `atomic/none/0`、反射记录 `record/field/1`、单值容器
`container/element/1`、key/value 容器 `container/element/2`。parser 在暴露 content 前
验证该组合；容器还要求 mapping ID 从零连续递增，并且 mapping 数量必须能被 group size
整除，因此 map 解码器永远只会接收到完整 key/value 对。

每个 mapping 固定为 8 字节，依次保存 ID 和 aligned address。mapping 按 ID 递增，
content 也按相同顺序排列。字段长度由下一 mapping 的地址或 entry 结尾确定。

第一阶段 writer 固定使用 `mapping_align = 1`。这是因为当前 mapping 没有独立 size 字段，
使用更大对齐会使 padding 与变长原子 payload 无法区分。后续若增加 payload size，可扩展
为更大的 alignment。

非原子字段的 content 是一个完整嵌套 entry；原子字段的 content 是直接二进制 payload。
根原子对象使用 mapping 数量为零的 entry，整个 content 都是原子 payload。

## 磁盘块

磁盘文件包含一个 storage header 和一个完整根 entry：

```cpp
struct storage_head {
    std::uint64_t magic_number;
    std::uint32_t version;
    std::uint32_t compress_tag__cap_only;
};
```

当前版本为 `2`，压缩标记必须为 `0`。版本 2 使用 entry 分类字段，明确不兼容版本 1 的
零标签 entry。暂不实现 CRC、压缩、临时文件替换、`fsync`、
文件锁和增量更新。

```cpp
auto bytes = srl::pack(value);
auto copy = srl::extract<T>(bytes->span());

auto size = srl::serialized_size(value);
srl::byte_buffer direct;
direct.resize(*size);
auto packed_into = srl::pack_into(direct.span(), value);

auto written = srl::write_block(path, bytes->span());
auto read = srl::read_block(path);

auto saved = srl::save(path, value);
auto loaded = srl::load<T>(path);
```

`write_block` 和 `read_block` 负责第 3、4 层转换；`save` 和 `load` 是它们与
`pack`/`extract` 的组合接口。所有可恢复的输入和 IO 错误通过
`std::expected<..., srl::error>` 返回。
