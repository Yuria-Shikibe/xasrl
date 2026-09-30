# srl

`srl` 是一个使用 C++26 静态反射实现的游戏对象序列化实验。

## 文件结构

- `types.hpp`：公开基础类型、wire header、错误码与错误对象。
- `annotations.hpp`：字段、原子类型和容器注解。
- `container.hpp`：容器 traits、容器适配约束与插入工具。
- `context.hpp`：引用转换 handler 与序列化上下文。
- `codec/base.hpp`：`atomic_codec` 接口及基础类型特化。
- `codec/reflected.hpp`：由对象表示自动生成的原子 codec 特化。
- `schema.hpp`：可序列化概念、`schema<T>` 编译期查询入口和静态校验。
- `schema/model.hpp`：schema 节点、树视图及非模板查询接口。
- `schema/build.hpp`：非模板 `consteval` 树构建、注解校验及尺寸计算。
- `schema_visualization.hpp`：基于 schema 树生成结构文本。
- `detail/reflection.hpp`：纯反射遍历、注解读取与类型分析工具。
- `detail/codec.hpp`：按节点类型集中组织编解码；`value_codec`、`field_codec` 和 `root_codec` 通过嵌套 `encode_plan` 管理编码缓存，通过静态 `decode` 解码。
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

### 自动顺序 ID

`auto_seq` 将直接基类和成员都纳入记录，ID 从 `0` 开始，先按直接基类声明顺序，再按成员
声明顺序递增。`transient` 会跳过对应实体；显式 `field` 会设置当前 ID，后续实体从该 ID
的下一个值继续，类似枚举值的自动顺延。重复 ID 或顺延溢出会使 schema 无效。

```cpp
struct [[=srl::tag::auto_seq{}]] Base {
    std::uint32_t base_value{}; // ID 0
};

struct [[=srl::tag::auto_seq{}]] State : Base {
    std::uint32_t value{}; // ID 1

    [[=srl::tag::transient{}]]
    void *runtime_only{}; // 跳过

    [[=srl::tag::field{.id = 4}]]
    std::string name{}; // ID 4

    bool active{}; // ID 5
};
```

### 顺序流模式

`auto_stream` 按直接基类声明顺序、再按成员声明顺序编码所有非 `transient` 实体。
布局由反射得到的类型和字段编码规则决定，不是 C++ 对象的内存布局。
与 `auto_seq` 不同，它忽略内部的 `field{id}`，不排序、不生成字段 ID 或偏移表。

```cpp
struct Plain {
    std::uint32_t count{};
    std::string name;
};

struct [[=srl::tag::auto_stream{}]] State
    : [[=srl::tag::auto_stream{}]] Plain {
    [[=srl::tag::auto_stream{}]] Plain detail;
    bool active{};
    [[=srl::tag::transient{}]] void *cache{};
};
```

注解可以放在类型、字段或直接基类上。字段／基类注解只覆盖当前位置的子对象，
允许普通未标注类、显式 record 和 `auto_seq` 类；不会递归改变更深层类型的编码。
显式 record 的父字段仍需要 `field{id}`，该 ID 属于父 record。
局部覆盖不支持 atomic 类型、自定义 atomic codec 类型、容器或非类类型。
类型级 `auto_stream` 与 `auto_seq`、`atomic` 互斥；字段级注解不能与
`atomic`、`container`、`transient` 或 `bypass_pointer_check` 同时使用。
与 `conversion_tag` 同时使用时，stream 模式应用于转换后的持久化类型。

wire 使用现有 16 字节 `entry_head`，`kind = stream (4)`、`mapping = none`、
`mapping_count = 0`、`mapping_group_size = 0`，随后直接拼接各实体 payload。
原子字段按 codec 固定长度读取，复合字段通过自己的 entry header 确定长度，
容器保留原有元素映射。每个 stream 相比具有相同 payload 的 `auto_seq` 每字段减少
8 字节。字节序和 storage version 沿用现有格式，旧读者不识别新 kind。

调用方必须保证读写布局一致。不记录 schema 指纹，也不通过类型名哈希约束 stream
布局匹配；同长度字段换序等错误可能无法检测。截断、越界及尾随字节返回可恢复错误。
没有未知字段跳过或缺失字段补齐。stream 自身的错误以字节偏移定位，嵌套 record
仍保留真实字段 ID。`unpack` 沿用失败时不改变目标对象的语义。

所有现有序列化和文件 API 均支持该模式；`reflected_stream<T>` 用于识别类型注解，
`serialized_field_count<T>()` 返回非 transient 实体数，`schema_string<T>()` 展示加载顺序。
这里的“流”指字节区间上的顺序消费，不提供分块或异步 I/O。

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
    srl::error to_storage(Asset *const &mem, std::string &srl) override;
    srl::error from_storage(Asset *&mem, const std::string &srl) override;
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
`error`（`error{}` 表示成功）。参数指向已构造的类型对象，不是原始未初始化存储。对于指针字段，
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
- 所有对象级入口都必须显式传入上下文：`pack(value, context)`、
  `serialized_size(value, context)`、`pack_into(bytes, value, context)`、
  `extract<T>(bytes, context)`、`unpack(bytes, out, context)`、
  `decode_root(bytes, out, context)`、`save(path, value, context)` 和
  `load<T>(path, context)`。即使 schema 不需要 handler，也不会隐式创建临时上下文。
- 运行时策略属于上下文，而不是额外的 options 参数。例如根容器重复项策略通过
  `context.duplicates` 配置；同一个上下文因此完整描述一次操作所需的 handler 与策略。
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
如果 `ranges::size` 在值未知的 range 引用上仍可作为常量表达式，`container_traits`
会自动把尺寸认定为由类型决定的固定长度；这覆盖 `std::array`、原生数组 `T[N]` 以及
具有同类 `constexpr size()` 的自定义容器。`std::string` 仍作为变长字符容器处理，
`std::span` 和 range view 不会被当作可反序列化容器。原生数组可作为字段使用，
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

解码唯一 set/map 时默认沿用容器语义去重。根容器通过上下文请求拒绝重复项：

```cpp
srl::serialization_context context{
    srl::duplicate_policy::reject};
```

反射字段可用独立 tag 覆盖上下文策略：

```cpp
[[=srl::tag::field{.id = 1}]]
[[=srl::tag::container{
    .duplicates = srl::duplicate_policy::reject}]]
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

`atomic` 标记包含两个选项：

```cpp
struct atomic {
    bool allow_padding{false};
};
```

自动生成的 codec 只支持对象表示路径：

1. `allow_padding = true`：直接复制完整对象表示，包括 padding；类型必须 trivially copyable。
2. `allow_padding = false` 且 `std::has_unique_object_representations_v<T>`：仍直接复制完整对象
   表示；类型必须 trivially copyable。

不再回退到逐基类、逐成员调用子对象 codec。需要按实体顺序编码的类型应改用 `auto_seq`。

自动原子类型可以用 `transient` 排除仅供运行时使用的成员：

```cpp
struct [[=srl::tag::atomic{}]] CachedValue {
    std::uint32_t value{};

    [[=srl::tag::transient{}]]
    std::uint32_t cache_generation{};
};
```

存在 `transient` 成员时，编码、解码和固定大小计算会完全跳过该成员；解码时它保留目标
对象构造后的默认值。若类型设置了 `allow_padding = true` 或具有独一对象表示，且对象布局
不存在 transient 与其它子对象的重叠，自动 codec 会合并 transient 之外的连续对象表示区间，
用少量 `memcpy` 读写；无法安全形成这些区间时 schema 无效。`transient` 只能单独标记自动原子类型的
非静态数据成员，不能用于普通 record，也不能和 `field`、字段级 `atomic`、`container` 或
转换标记组合。

自动生成支持 public、protected 和 private 的非虚直接基类；成员不能是引用、bit-field 或
带字段标记。直接复制对象表示时，库会递归检查指针或成员指针并默认拒绝，避免把进程地址
写入 wire。

指针检查由独立的 `bypass_pointer_check` tag 控制。标在类型声明上时，对该类型的所有直接
基类和成员生效；标在单独成员或基类上时，只强制包含该实体。该规则同时适用于 `atomic`
和 `auto_seq`：`auto_seq` 中被放行的指针实体会直接复制该实体的对象表示。

```cpp
struct [[=srl::tag::atomic{}]]
       [[=srl::tag::bypass_pointer_check{}]] ProcessLocalHandle {
    void *value{};
};

struct [[=srl::tag::auto_seq{}]] State {
    [[=srl::tag::bypass_pointer_check{}]]
    void *runtime_address{};
};
```

该 tag 不提供地址修复、跨进程有效性或可移植性。库在编译期要求目标使用 8-bit byte 和
小端序，不提供大端兼容或运行时字节序转换。

无法修改声明的第三方平凡类型可以在字段上附加 `atomic`，强制该字段直接复制对象表示：

```cpp
struct Record {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::atomic{}]]
    third_party::trivial_value value;
};
```

字段级强制原子要求类型 trivially copyable，并且默认要求独一对象表示；若确认 padding 也
属于约定的 wire representation，可设置 `.allow_padding = true`。需要包含指针表示时，另加
`[[=srl::tag::bypass_pointer_check{}]]`。

整数、浮点、枚举和 `bool` 是内建原子类型。`std::string` 始终走字符容器路径，不提供
`atomic_codec<std::string>`。其它固定尺寸的特殊类型可显式特化 `srl::atomic_codec<T>`。
自定义 codec 必须提供可在编译期求值的 `fixed_size`，并直接对调用方提供的
`std::span<std::byte, fixed_size>` 和 `std::span<const std::byte, fixed_size>` 编解码，不创建
中间 payload buffer。变长类型应使用容器或 record 表示。自定义类型仍应在声明处标记
`[[=srl::tag::atomic{}]]`。

## 内存格式

线格式直接使用受支持小端目标的对象表示。header 和 mapping 是固定布局的
trivially copyable 类型，完整复制其对象表示；atomic content 只使用完整或 transient 分段的
对象表示复制。因此只有显式允许 padding，或类型具有独一对象表示时，复合原子类型才会
把对象表示复制到文件；结构化的逐实体编码由 record/`auto_seq` 承担。

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
srl::serialization_context context;

auto bytes = srl::pack(value, context);
auto copy = srl::extract<T>(bytes->span(), context);

auto size = srl::serialized_size(value, context);
srl::byte_buffer direct;
direct.resize(*size);
auto packed_into = srl::pack_into(direct.span(), value, context);

auto written = srl::write_block(path, bytes->span());
auto read = srl::read_block(path);

auto saved = srl::save(path, value, context);
auto loaded = srl::load<T>(path, context);
```

`write_block` 和 `read_block` 负责第 3、4 层转换；`save` 和 `load` 是它们与
`pack`/`extract` 的组合接口。所有可恢复的输入和 IO 错误通过
`std::expected<..., srl::error>` 返回；只返回错误状态的 `pack_into`、`unpack`、
`decode_root`、`write_block` 和 `save` 直接返回 `srl::error`，`error{}` 表示成功。

## 编译期 Schema 树

`schema<T>::tree` 是统一的编译期结构描述。`root` 表示根节点，`value` 表示实际值节点；
atomic 根节点额外包含 entry header，而嵌套 atomic 值仅包含 payload。通过 `node(index)`、
`children(index)` 和 `child(index, ordinal)` 查询节点及子节点，索引始终属于同一棵树。

```cpp
using description = srl::schema<Profile>;
static_assert(description::valid);
constexpr auto value = description::value;
constexpr auto first = description::child(value, 0);
constexpr auto field_id = description::node(first).id;
constexpr auto stored_type = description::node(first).type; // std::meta::info
constexpr auto encoded_size = description::node(description::root).encode_extent;
```

节点包含 wire 分类、字段/基类反射信息、内存与存储类型、转换选项、容器槽位与数量约束。
record 子节点按字段 ID 排序，stream 子节点按加载顺序排列；容器只描述 element 或 key/value
槽位，不按元素数量展开。同一类型在不同字段的 stream、forced atomic、转换及重复元素策略
分别记录在各自节点中。atomic 的对象表示是一个叶节点，其内部成员不是独立 wire 子节点。

`encode_extent` 递归计算编码长度，无法静态确定时为 `std::dynamic_extent`。
`decode_extent` 表示解码可要求的精确 payload 长度：atomic/forced 为固定尺寸，复合节点保留
动态尺寸，因为目标类型的编码长度并不限定所有合法输入布局。定长内部编码和解码接口要求
静态 extent 的 span；公共字节输入在边界检查长度后再转换，不截断过长的 payload。

树构建、校验和尺寸计算使用非模板 `consteval` 函数与普通循环，结果固化为静态数组。
模板仅连接 codec/容器适配接口、缓存根类型描述以及生成实际操作对象的代码。
schema 不保存对象、转换结果或运行期 handler；这些状态仍由编码计划及上下文持有。

## Schema 可视化

`schema_string<T>()` 在编译期遍历上述 schema 树，并返回具有静态存储期的
`std::string_view`。输出包含 record 字段 ID、转换后的存储类型、atomic 固定大小、容器
元素布局和固定元素数量；无效 schema 会在编译期被拒绝。原子节点区分
`primitive-atomic`、`codec-atomic` 和 `object-atomic`。进入原子模式的直接成员或基类使用
`[atomic-root member]` / `[atomic-root base]`，原子对象内部继续包含的实体使用
`[atomic member]` / `[atomic base]`。

```cpp
constexpr auto schema = srl::schema_string<Profile>();
std::println("{}", schema);
```

类型模板参数会按 C++ 规则丢失 alias 身份。需要保留调用处的 alias 名时，直接传入类型反射：

```cpp
using ProfileAlias = Profile;
constexpr auto schema = srl::schema_string<^^ProfileAlias>();
```

输出样式如下：

```text
record Profile {
  [1] person: record Person {
    [1] [atomic-root member] age: primitive-atomic int (size: 4 bytes)
    [2] [atomic-root member] score: primitive-atomic double (size: 8 bytes)
  }
  [2] name: container std::string (count: dynamic) {
    element: primitive-atomic char (size: 1 byte)
  }
  [3] [atomic-root member] active: primitive-atomic bool (size: 1 byte)
  [4] [atomic-root member] state: primitive-atomic activity (size: 1 byte)
  [5] position: record Vec2 {
    [0] [atomic-root member] x: primitive-atomic float (size: 4 bytes)
    [1] [atomic-root member] y: primitive-atomic float (size: 4 bytes)
  }
}
```

对象表示原子类型会进一步展示直接参与原子模式的成员和基类：

```text
object-atomic UniqueAtomic (size: 8 bytes, representation: direct) {
  [atomic-root member] first: primitive-atomic unsigned int (size: 4 bytes)
  [atomic-root member] second: primitive-atomic unsigned int (size: 4 bytes)
}
```
