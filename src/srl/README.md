# srl

`srl` 是一个使用 C++26 静态反射实现的游戏对象序列化实验。

## 文件结构

- `types.hpp`：公开基础类型、wire header、错误码与错误对象。
- `annotations.hpp`：字段、叶节点类型和容器注解。
- `container.hpp`：容器 traits、容器适配约束与插入工具。
- `context.hpp`：引用转换 handler 与序列化上下文。
- `codec/base.hpp`：`leaf_codec` 接口及基础类型特化。
- `codec/reflected.hpp`：由对象表示自动生成的叶节点 codec 特化。
- `schema.hpp`：可序列化概念、`schema<T>` 编译期查询入口和静态校验。
- `schema/model.hpp`：schema 节点、树视图及非模板查询接口。
- `schema/build.hpp`：非模板 `consteval` 树构建、注解校验及尺寸计算。
- `schema_visualization.hpp`：基于 schema 树生成结构文本。
- `detail/reflection.hpp`：纯反射遍历、注解读取与类型分析工具。
- `detail/codec.hpp`：按节点类型集中组织编解码；`value_codec`、`field_codec` 和 `root_codec` 通过嵌套 `encode_plan` 管理编码缓存，通过静态 `decode` 解码。
- `detail/utility.hpp`：无状态 wire、枚举、哈希和 trivial byte 工具。
- `detail/wire_parse.hpp`：已验证 entry 视图与 wire 解析。
- `storage/byte_buffer.hpp`：序列化结果使用的字节缓冲。
- `serialization.hpp`：`serialize`、`deserialize` 与 `serialized_size` 公共 API。
- `transaction.hpp`：保留 allocator 的事务副本与无异常提交定制。
- `limits.hpp`：输入大小、容器元素数和累计元素存储预算。
- `detail/object_layout.hpp`：对象叶节点成员布局；供编码、输入校验和 schema 测试共用。
- `wire.hpp`、`core.hpp`：保持稳定 include 路径的聚合入口。
- `io.hpp`：只处理字节的磁盘块读写 `write_block`、`read_block`。

## 编解码入口

对象编解码统一写入调用方提供的目标，参数顺序为输入、输出、上下文：

```cpp
srl::serialization_context context;
srl::byte_buffer bytes;
if (auto status = srl::serialize(value, bytes, context); status) {
    return status;
}
T restored{};
if (auto status = srl::deserialize(bytes.span(), restored, context); status) {
    return status;
}
```

两个操作都返回 `srl::error`，`error{}` 表示成功。没有返回新对象、非事务式解码、
对象直接存取文件的另一套入口，也不提供旧名称别名。

`serialize` 支持 `byte_buffer&` 和 `std::span<std::byte, Extent>` 两种输出：
缓冲重载自动调整长度并复用容量；span 重载要求长度精确匹配。编码开始后的错误或异常
允许输出内容被修改，缓冲对象本身仍保持有效。输入及其引用的数据不得与输出缓冲重叠。

`deserialize` 接受可写或只读、定长或动态的字节 span，采用复制目标、解码临时对象、
成功后提交的事务语义；错误或异常不改变原目标。目标必须满足 `transactional<T>`，
默认保留容器 allocator，必要时由反射逐成员复制和提交 aggregate；带 allocator 的
非 aggregate 可以特化 `transaction_traits<T>`，保证 `clone` 保留 `commit` 的前置条件。
普通类型仍使用复制和无异常交换，不引入运行时类型擦除或 allocator 比较。
根原生数组采用元素级复制和交换。未序列化状态沿用各 codec 的规则，不合并动态容器
中的旧元素；读取新对象时由调用方先默认构造目标即可。

定长类型可以只根据类型在编译期取得完整根 entry 的尺寸，包括 header 和 mapping：

```cpp
constexpr auto size = srl::serialized_size<FixedRecord>();
std::array<std::byte, size> bytes;
if (auto status = srl::serialize(value, std::span{bytes}, context); status) {
    return status;
}
```

`serialized_size<T>()` 是 `consteval noexcept`，直接返回 `srl::szt`，仅对定长类型可用。
统一的 `serialized_size(value, context)` 调用对定长类型选择 `constexpr noexcept` 重载，
同样直接返回尺寸，不执行转换、不绑定 handler；尺寸可知不代表实际编码一定成功。
变长类型的该重载返回 `std::expected<srl::szt, srl::error>`，可能调用转换及 handler，
因此不声明 `noexcept`。分配外部缓冲时先检查测量结果，再提供精确长度的 span。

span 的 `Extent` 从实参推导，类型编码尺寸与 span 长度均已知且不匹配时在编译期拒绝，
否则在写入前检查实际长度。反序列化不能以目标类型的编码尺寸限制输入长度，因为
兼容 record 可以包含额外字段；所有输入仍经过 wire 边界验证。

## 对象层级

对象自上而下分为：

1. 游戏运行时对象：可能包含指针、句柄、缓存等运行时状态。
2. 游戏元数据对象：脱离运行时环境后仍有意义的数据。
3. 已序列化内存对象：`entry_head + storage_mapping[] + content`。
4. 已存储磁盘块：`storage_head + serialized entry`。

库实现第 2、3、4 层，并支持字段级运行时表示与持久化表示转换。转换的业务语义由
游戏代码提供，库负责调用、上下文传递和字节编解码；未标记字段不会进入二进制。

## 反射记录

非叶节点记录只序列化带有稳定 ID 的成员和直接非虚基类：

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
`serialize` 时触发编译期错误。

解码记录时，以目标类型的字段 ID 为准：目标需要的字段必须存在，源对象中的未知字段
会被忽略。因此同一份二进制可以转换为多个字段兼容的元数据类型。

缺失字段默认返回 `missing_field`，且目标保持不变。只有显式附加
`[[=srl::tag::fallback{}]]` 的字段才使用字段类型的 `T{}`；这不会重新求值宿主类型的
成员初始化器。`fallback{defaults::value<42>{}}` 指定固定默认值，`provider` 和
`modifier` 支持业务生成和修改。fallback 仅用于缺失字段，不能修复存在但损坏的数据。
该策略保存在 `schema_node::fallback_type`，编解码与 schema 展示共同读取它。

错误的 `offset` 是相对根 entry 的字节偏移；嵌套 codec 每层只累计一次 payload 起点。
`error.id` 是 `std::optional<id_type>`，没有 ID 的结构错误与真实字段 ID `0` 明确区分。
自定义 codec 返回相对自身输入的偏移，库负责转换为根偏移。
`error.system_error` 仅在系统调用失败时包含 `std::error_code`；成功路径不初始化系统错误分类。
`schema_diagnostic<T>()` 返回编译期 schema 失败原因和相关实体，公开 API 的 `static_assert`
直接使用该诊断；测试可以检查 `schema<T>::tree.issue` 的结构化原因。

解码预算放在 `context.config.limits`：默认输入上限 256 MiB、单容器 1,048,576 个元素、
一次操作累计 4,194,304 个元素，以及 512 MiB 的累计元素存储。游戏加载器可以按资源类别
显式调整这些值。元素存储按所访问容器的 `count * sizeof(element_type)` 累计，包含固定
容器和重复输入元素；它不包含分配器 bookkeeping、目标事务副本或业务 callback 的分配，
因此不是进程堆内存的硬上限。预算在容器 reserve／构造元素前检查，嵌套容器共享一次操作
的预算，超限返回 `resource_limit_exceeded`。根字节边界在复制目标之前验证。

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
局部覆盖不支持 leaf 类型、自定义 leaf codec 类型、容器或非类类型。
类型级 `auto_stream` 与 `auto_seq`、`leaf` 互斥；字段级注解不能与
`leaf`、`container`、`transient` 或 `bypass_pointer_check` 同时使用。
与 `conversion_tag` 同时使用时，stream 模式应用于转换后的持久化类型。

wire 使用 16 字节 `entry_head`，`kind() = entry_kind::stream (2)`、
`mapping_count() = 0`，随后直接拼接各实体 payload。
叶节点字段按 codec 固定长度读取，复合字段通过自己的 entry header 确定长度，
容器保留原有元素映射。每个 stream 相比具有相同 payload 的 `auto_seq` 每字段减少
16 字节。使用小端字节序和 storage version 4，不兼容旧版本。

stream header 记录由字段顺序、名称和 wire 类型生成的布局指纹；同长度字段换序会被拒绝。
截断、越界及尾随字节返回可恢复错误。
没有未知字段跳过或缺失字段补齐。stream 自身的错误以字节偏移定位，嵌套 record
仍保留真实字段 ID。`deserialize` 失败时不改变目标对象。

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
// srl::byte_buffer bytes;
// auto encoded = srl::serialize(state, bytes, context);
// RenderState restored{};
// After checking encoded, call srl::deserialize(bytes.span(), restored, context).
```

上下文使用非拥有的 `unordered_map<const void*, context_reference_handler*>`。
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
- `deserialize` 使用临时副本加交换，错误或异常不会改变目标；不公开允许部分更新的解码入口。
  handler 自身在所有失败路径中也必须保持参数类型的不变量。
- 所有运行时对象级入口都必须显式传入上下文：`serialize(value, bytes, context)`、
  `deserialize(bytes, out, context)` 与 `serialized_size(value, context)`。
  即使 schema 不需要 handler，也不会隐式创建临时上下文；纯类型查询 `serialized_size<T>()`
  不读取对象或上下文。
- 运行时策略属于上下文，而不是额外的 options 参数。例如根容器重复项策略通过
  `context.config.duplicates` 配置；同一个上下文因此完整描述一次操作所需的 handler 与策略。
- 一次保存先创建递归编码计划，持有转换结果，再使用同一结果测量与写入；普通字段不额外复制。
  变长 `serialized_size` 与随后 `serialize` 是独立操作，不复用转换结果。整个调用期间，源对象、
  容器迭代顺序和外部解析环境必须稳定，库不对外部系统提供快照或锁定。

转换仅作用于普通可写成员，禁止引用、const/volatile 成员、bit-field、基类转换、重复标记、
与字段强制 `leaf` 并用，以及自动叶节点类型内部的转换。`SrlTy` 必须是可默认构造、可移动
的可序列化值类型，`MemTy` 必须可赋值；编译期拒绝循环 schema。

字段转换针对整个字段，不自动传播到容器元素；但转换后的记录或容器内部可以继续包含转换字段。
字段的 `tag::container` 检查并作用于转换后的 `SrlTy`。字段 ID 不变，payload 按 `SrlTy`
编码，有嵌套 entry 时使用 `SrlTy` 的类型信息。handler 地址绝不持久化。改变 `SrlTy` 需要
显式字段 ID 或业务版本迁移；mapping 中的 wire 类型标识会拒绝同尺寸但类型不同的字段。

## 容器

容器是独立于叶节点和反射记录的第三类 entry。库自动识别可重复遍历、可清空并可重新插入
元素的 owning range，并根据容器能力选择 `reserve`、尾插、普通插入或 `insert_after`。
如果 `ranges::size` 在值未知的 range 引用上仍可作为常量表达式，`container_traits`
会自动把尺寸认定为由类型决定的固定长度；这覆盖 `std::array`、原生数组 `T[N]` 以及
具有同类 `constexpr size()` 的自定义容器。`std::string` 仍作为变长字符容器处理，
`std::span` 和 range view 不会被当作可反序列化容器。原生数组可作为字段使用，
根数组通过相同的 `serialize` / `deserialize` 入口往返，由调用方提供目标数组。

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

## 叶节点类型

叶节点类型作为一个整体参与序列化，其 content 中不再启动嵌套 entry。用户定义类型在类型声明
处标记：

```cpp
struct [[=srl::tag::leaf{}]] Vec2 {
    float x{};
    float y{};
};
```

`leaf` 标记包含一个选项：

```cpp
struct leaf {
    bool allow_padding{false};
};
```

自动生成的 codec 只支持对象表示路径：

1. `allow_padding = true`：保留对象布局的字节位置，但编码将 padding 写零；类型必须 trivially copyable。
2. `allow_padding = false` 且 `std::has_unique_object_representations_v<T>`：仍直接复制完整对象
   表示；类型必须 trivially copyable。

不再回退到逐基类、逐成员调用子对象 codec。需要按实体顺序编码的类型应改用 `auto_seq`。

编译期生成的 `schema_node::object` 记录每个标量成员的对象偏移、wire 偏移、尺寸和类型。
自动和强制 leaf 共用此布局：读取先验证 bool／枚举，再复制有效成员；非法对象表示
不会进入 C++ 对象。无 padding、无 transient 的布局仍可合并为一次 `memcpy`。
包含 union 的自动对象布局无效，必须使用能表达活动分支的自定义 codec。

自动叶节点类型可以用 `transient` 排除仅供运行时使用的成员：

```cpp
struct [[=srl::tag::leaf{}]] CachedValue {
    std::uint32_t value{};

    [[=srl::tag::transient{}]]
    std::uint32_t cache_generation{};
};
```

存在 `transient` 成员时，编码、解码和固定大小计算会完全跳过该成员；解码时它保留目标
对象原有的值。若类型设置了 `allow_padding = true` 或具有独一对象表示，且对象布局
不存在 transient 与其它子对象的重叠，schema 会计算排除 transient 后的 wire 位置，
codec 按同一份标量布局展开复制并将 padding 写零；无法安全形成布局时 schema 无效。`transient` 只能单独标记自动叶节点类型的
非静态数据成员，不能用于普通 record，也不能和 `field`、字段级 `leaf`、`container` 或
转换标记组合。

自动生成支持 public、protected 和 private 的非虚直接基类；成员不能是引用、bit-field 或
带字段标记。直接复制对象表示时，库会递归检查指针或成员指针并默认拒绝，避免把进程地址
写入 wire。

指针检查由独立的 `bypass_pointer_check` tag 控制。标在类型声明上时，对该类型的所有直接
基类和成员生效；标在单独成员或基类上时，只强制包含该实体。该规则同时适用于 `leaf`
和 `auto_seq`：`auto_seq` 中被放行的指针实体会直接复制该实体的对象表示。

```cpp
struct [[=srl::tag::leaf{}]]
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

无法修改声明的第三方平凡类型可以在字段上附加 `leaf`，强制该字段直接复制对象表示：

```cpp
struct Record {
    [[=srl::tag::field{.id = 1}]]
    [[=srl::tag::leaf{}]]
    third_party::trivial_value value;
};
```

字段级强制叶节点要求类型 trivially copyable，并且默认要求独一对象表示；设置
`.allow_padding = true` 时 padding 占据格式空间但写零。需要包含指针表示时，另加
`[[=srl::tag::bypass_pointer_check{}]]`。

整数、浮点、枚举和 `bool` 是内建叶节点类型。`std::string` 始终走字符容器路径，不提供
`leaf_codec<std::string>`。其它固定尺寸的特殊类型可显式特化 `srl::leaf_codec<T>`。
自定义 codec 必须提供可在编译期求值的 `fixed_size`，并直接对调用方提供的
`std::span<std::byte, fixed_size>` 和 `std::span<const std::byte, fixed_size>` 编解码，不创建
中间 payload buffer。变长类型应使用容器或 record 表示。自定义类型仍应在声明处标记
`[[=srl::tag::leaf{}]]`。

## 内存格式

线格式直接使用受支持小端目标的对象表示。header 和 mapping 是固定布局的
trivially copyable 类型，完整复制其对象表示；leaf content 使用编译期成员布局，
padding 写零，transient 不写入目标。因此只有显式允许 padding，或类型具有独一对象表示时，复合叶节点类型才会
把对象表示复制到文件；结构化的逐实体编码由 record/`auto_seq` 承担。

```cpp
using szt = std::uint64_t;           // 序列化长度和字节偏移的统一类型。

struct entry_head {
    szt size;                       // 整个 entry 的字节数。
    entry_metadata metadata;        // 低 24 位 count，高 8 位 kind。
    std::uint32_t wire_type;         // schema 生成的 wire 契约标识。
};

struct entry_mapping {
    std::uint32_t id;
    std::uint32_t wire_type;         // 此字段的 wire 契约标识。
    szt offset;                     // 相对 content 起点的字节偏移。
};
```

线格式中的 `entry_head` 固定为 16 字节：

| Offset | Size | 内容 |
| --- | ---: | --- |
| 0 | 8 | entry 总大小 |
| 8 | 3 | mapping 数量，最多 16,777,215 |
| 11 | 1 | entry kind |
| 12 | 4 | wire 契约标识 |

`entry_metadata` 使用显式移位和掩码，不依赖 C++ 位域布局。构造时验证计数不超过
24 位，通过 `entry_head::mapping_count()` 和 `kind()` 读取。ID 与类型 hash 仍为 32 位。

`entry_kind` 只编码互斥的实际格式：

| 值 | 类型 | 内容 |
| --- | --- | --- |
| 1 | leaf | 无 mapping，叶节点 payload |
| 2 | stream | 无 mapping，按 schema 顺序拼接 payload |
| 3 | record | 按字段 ID 索引 |
| 4 | sequence | 元素 ID 从零连续递增，每个元素一个 mapping |
| 5 | map | 槽位 ID 从零连续递增，每个键值对两个 mapping |

其余 kind 保留，当前读取时拒绝。map 的 mapping 数量必须为偶数，最多容纳
8,388,607 个键值对。容器 traits 的 `mapping_group_size` 仅用于编译期，不再单独写入线格式。

每个 mapping 固定为 16 字节，ID、wire_type、offset 位于偏移 0、4、8。
wire_type 来自 `schema_node::wire_type`，编解码和元数据驱动测试共用同一值。mapping 按 ID 递增，
字节偏移单调不减，content 按相同顺序紧密排列，无对齐缩放或填充。
字段长度由下一 mapping 的偏移或 entry 结尾确定。

wire 契约由编译期元数据生成：整数按符号和宽度区分，浮点按宽度区分；record 按字段 ID
逐项检查，因此仍允许未知字段与兼容子集。容器身份取决于元素 wire 类型而不是容器实现，
保留 vector／list 等互读。stream 加入字段顺序与名称。枚举、自定义 codec 仍使用去别名后的
类型名作为名义身份，当前不承诺不同编译器的这些名称一致。32 位标识用于匹配格式，
不是内容校验和或认证机制。

大小计算、`serialized_size` 的结果和 `error.offset` 统一使用 `srl::szt`。
线格式可表达超过 4 GiB 的 entry，实际操作仍受 `size_t`、缓冲区容量及可用内存限制；
大小计算为 `std::dynamic_extent` 保留全一哨兵值。静态 mapping 数量超限使 schema 无效，
动态超限返回 `too_many_mappings`；尺寸溢出返回 `entry_too_large`。

非叶节点字段的 content 是一个完整嵌套 entry；叶节点字段的 content 是直接二进制 payload。
根叶节点对象使用 mapping 数量为零的 entry，整个 content 都是叶节点 payload。

## 磁盘块

磁盘文件包含一个 storage header 和一个完整根 entry：

```cpp
struct storage_head {
    std::uint64_t magic_number;
    std::uint32_t version;
    std::uint32_t compress_tag__cap_only;
};
```

当前版本为 `4`，压缩标记必须为 `0`。不兼容旧布局，读取时拒绝旧版本。
文件 I/O 分块读写，避免单次调用的 `streamsize` 限制，但序列化仍使用完整内存缓冲区。
Windows 写入先通过 `CREATE_NEW` 创建同目录唯一临时文件，完成写入并调用
`FlushFileBuffers` 后关闭句柄，再用同卷 rename 替换目标。写入或替换失败不会先截断
旧存档；替换失败返回 `io_commit_failed` 和系统错误码。未提交临时文件由 RAII 清理，
进程崩溃或清理失败可能留下带 `.srl-...tmp` 后缀的文件。此接口不承诺突然断电时目录
元数据的持久化，也不负责多写者之间的业务仲裁。暂不实现 CRC、压缩和增量更新。
`read_block(path, max_entry_bytes)` 在分配文件缓冲前检查大小；默认上限与解码输入相同。

```cpp
srl::serialization_context context;

srl::byte_buffer bytes;
if (auto status = srl::serialize(value, bytes, context); status) {
    return status;
}
if (auto status = srl::write_block(path, bytes.span()); status) {
    return status;
}
auto block = srl::read_block(path);
if (!block) {
    return block.error();
}
T restored{};
if (auto status = srl::deserialize(block->span(), restored, context); status) {
    return status;
}
```

`write_block` 和 `read_block` 只负责第 3、4 层转换，不接受对象或上下文。
`write_block` 返回 `srl::error`，`read_block` 返回 `std::expected<byte_buffer, srl::error>`。
对象文件读写由调用方显式组合上述入口；没有单独的 `save` / `load` 包装。

## 编译期 Schema 树

`schema<T>::tree` 是统一的编译期结构描述。`root` 表示根节点，`value` 表示实际值节点；
leaf 根节点额外包含 entry header，而嵌套 leaf 值仅包含 payload。通过 `node(index)`、
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
槽位，不按元素数量展开。同一类型在不同字段的 stream、forced leaf、转换及重复元素策略
分别记录在各自节点中。leaf 的对象表示是一个叶节点，其内部成员不是独立 wire 子节点。

`encode_extent` 递归计算编码长度，无法静态确定时为 `std::dynamic_extent`。
`decode_extent` 表示解码可要求的精确 payload 长度：leaf/forced 为固定尺寸，复合节点保留
动态尺寸，因为目标类型的编码长度并不限定所有合法输入布局。定长内部编码和解码接口要求
静态 extent 的 span；公共字节输入在边界检查长度后再转换，不截断过长的 payload。

树构建、校验和尺寸计算使用非模板 `consteval` 函数与普通循环，结果固化为静态数组。
模板仅连接 codec/容器适配接口、缓存根类型描述以及生成实际操作对象的代码。
schema 不保存对象、转换结果或运行期 handler；这些状态仍由编码计划及上下文持有。
容器元素是否需要编码缓存也由该树推导：leaf 以及不含转换或容器的定长 record/stream
使用空槽位计划，不为每个元素分配缓存；转换结果和动态布局仍在准备阶段缓存，避免重复计算。

## Schema 可视化

`schema_string<T>()` 在编译期遍历上述 schema 树，并返回具有静态存储期的
`std::string_view`。输出包含 record 字段 ID、转换后的存储类型、leaf 固定大小、容器
元素布局和固定元素数量；无效 schema 会在编译期被拒绝。叶节点节点区分
`primitive-leaf`、`codec-leaf` 和 `object-leaf`。进入叶节点模式的直接成员或基类使用
`[leaf-root member]` / `[leaf-root base]`。对象叶节点布局使用 `[wire +偏移]` 展示
实际标量的 wire 位置、对象内偏移及大小，与编解码和输入校验使用相同的布局元数据。

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
    [1] [leaf-root member] age: primitive-leaf int (size: 4 bytes)
    [2] [leaf-root member] score: primitive-leaf double (size: 8 bytes)
  }
  [2] name: container std::string (count: dynamic) {
    element: primitive-leaf char (size: 1 byte)
  }
  [3] [leaf-root member] active: primitive-leaf bool (size: 1 byte)
  [4] [leaf-root member] state: primitive-leaf activity (size: 1 byte)
  [5] position: record Vec2 {
    [0] [leaf-root member] x: primitive-leaf float (size: 4 bytes)
    [1] [leaf-root member] y: primitive-leaf float (size: 4 bytes)
  }
}
```

对象表示叶节点类型会进一步展示直接参与叶节点模式的成员和基类：

```text
object-leaf UniqueLeaf (size: 8 bytes, representation: direct) {
  [wire +0] first: unsigned int (size: 4, object +0)
  [wire +4] second: unsigned int (size: 4, object +4)
}
```
