# SRL 接口与格式参考

项目简介、构建命令和最小示例见[根目录 README](../../README.md)

## 格式与兼容性契约

**本库不提供跨编译器兼容保证，也不提供同一编译器不同版本之间的兼容保证，对于非标准布局类型的leaf标注需要谨慎使用**
用户需要自行处理工具链升级、数据迁移和旧数据读取；相同 storage version 或 wire 类型标识
不构成跨工具链兼容承诺。对象布局、基础类型宽度和反射类型名称都可能受工具链、ABI、
编译选项影响。目前只支持 8-bit byte 和小端目标，不进行字节序转换。

- record 按稳定字段 ID 匹配，未知字段忽略；目标缺失字段仅在显式 fallback 时补齐。
  auto_seq 自动分配 ID，插入或移动声明可能改变后续 ID，持久化协议应显式管理 ID。
- stream 按声明顺序消费，指纹包含字段名称、顺序和 wire 类型；改名或换序可能不兼容，
  不提供未知字段跳过或缺失字段补齐。
- wire 类型标识为 32 位哈希，存在碰撞可能，不是身份认证或完整性校验。record 的根标识
  不包含字段集合，字段逐项校验；容器按元素类型匹配，不按容器实现匹配。枚举和自定义
  leaf codec 使用去别名后的类型名称；flags 枚举还包含允许位掩码。
- v5 将映射定义与实例数据分离：定义中的字段 ID、wire 类型和定界规则由 schema 生成；
  每个实例按定义顺序存放 payload，不再保存字段偏移。读取验证当前 entry 的边界，
  不递归验证被跳过的未知字段的语义。
  read_block / write_block 也只有字节结构验证；目标类型相关检查发生在 deserialize。
  已验证视图只能由解析函数创建；它借用输入，使用期间输入必须存活且内容不变。
- deserialize 的错误和异常不改变原目标；serialize 开始编码后允许输出部分更新。
  文件写入通过临时文件提交，提交失败保留原文件。业务 callback 的外部副作用不在事务内。
- 未参与序列化的普通成员及固定容器已有元素状态通过事务副本保留；动态容器重建元素，
  不合并旧元素状态。fallback{} 使用字段类型 T{}，不会重新执行宿主成员初始化器。
  defaults::automatic{} 显式复用编译期默认宿主对象的成员值，无法取得时拒绝 schema
- 对象表示 leaf 要求满足布局约束；允许 padding 时写零。绕过指针检查仅保存地址表示，
  用户负责地址有效性。当前 storage version 为 5，未实现压缩、CRC 或版本迁移。

## 文件结构

- `types.hpp`：公开基础类型、wire header、错误码与错误对象。
- `conversion.hpp`：轻量转换描述、handler key 与重复项策略；只前置声明上下文。
- `annotations.hpp`：字段、叶节点类型、flags 枚举和容器注解。
- `enum.hpp`、`error.hpp`：枚举语义与错误码名称；`diagnostics.hpp` 提供按需错误格式化。
- `wire_format.hpp`：wire 尺寸、版本与上限常量。
- `container.hpp`：容器 traits、容器适配约束与插入工具。
- `context.hpp`：引用转换 handler 与序列化上下文。
- `codec/base.hpp`：`leaf_codec` 接口及基础类型特化。
- `codec/reflected.hpp`：由对象表示自动生成的叶节点 codec 特化。
- `schema.hpp`：可序列化概念、`schema(^^T)` 编译期查询入口和静态校验。
- `schema/model.hpp`：schema 节点、树视图及非模板查询接口。
- `schema/build.hpp`：非模板 `consteval` 树构建、注解校验及尺寸计算。
- `schema_visualization.hpp`：基于 schema 树生成结构文本。
- `detail/reflection_facts.hpp`：共享反射事实、注解计数、指针分析和 leaf 元数据校验。
- `detail/reflection.hpp`：模板反射访问、对象表示范围与自动 leaf 资格。
- `detail/codec.hpp`：按节点类型集中组织编解码；编码计划保留类型有界的状态，逐元素计划统一写入 `storage/encoding_recorder.hpp`，解码仍走静态 `decode`。
- `detail/utility.hpp`：哈希和 trivial byte 工具。
- `detail/wire_document.hpp`：文档、定义和顺序 entry 解析。
- `storage/byte_buffer.hpp`：序列化结果使用的字节缓冲。
- `serialization.hpp`：`serialize`、`deserialize` 与 `serialized_size` 公共 API。
- `transaction.hpp`：保留 allocator 的事务副本与无异常提交定制。
- `limits.hpp`：解码入口的输入大小限制。
- `detail/object_layout_model.hpp`：纯对象布局数据模型，供 schema 描述使用。
- `detail/object_layout.hpp`：对象布局构建、输入校验和有效成员复制。
- `wire.hpp`、`core.hpp`：公开聚合入口。
- `io.hpp`：只处理字节的磁盘块读写 `write_block`、`read_block`。

依赖方向由轻量声明到实际执行：`types / conversion / annotations / object_layout_model`
→ 共享反射事实与 schema 构建 → codec 与运行时 context → serialization 与事务提交。
`schema/model.hpp` 不包含 handler 注册表、字节缓冲或对象布局算法；底层实现直接包含
所需头文件，`core.hpp` 和 `wire.hpp` 仅作为调用方可选的聚合入口。文件 I/O 独立处理字节块。

## 编解码入口

对象编解码统一写入调用方提供的目标，参数顺序为输入、输出、上下文：

```cpp
xasrl::serialization_context context;
xasrl::byte_buffer bytes;
if (auto status = xasrl::serialize(value, bytes, context); status) {
    return status;
}
T restored{};
if (auto status = xasrl::deserialize(bytes.span(), restored, context); status) {
    return status;
}
```

状态转为 `bool` 时，`true` 表示失败。`deserialize` 返回 `xasrl::error`，`error{}` 表示成功。
`serialize` 在输出为精确长度的固定 span、且准备和编码均无可恢复错误时返回
`xasrl::always_success`，其余情况返回 `xasrl::error`。推荐用 `auto` 保留返回类型，
也可以通过隐式转换存为 `xasrl::error`。没有返回新对象、非事务式解码、
对象直接存取文件的另一套入口，也不提供旧名称别名。

`serialize` 支持 `byte_buffer&` 和 `std::span<std::byte, Extent>` 两种输出：
缓冲重载自动调整长度并复用容量；span 重载要求长度精确匹配。编码开始后的错误或异常
允许输出内容被修改，缓冲对象本身仍保持有效。输入及其引用的数据不得与输出缓冲重叠。

`always_success` 是空状态类型，支持与 `error` 相同的布尔判断、`set_id` 和 `rebase`，
只能隐式转换为 `error`，不能反向转换。它表示正常返回时没有错误，不表示 `noexcept`。
自定义 leaf codec 的 `encode`、`decode` 可以分别返回这两种状态，且仍须接受精确长度的 span。
库根据返回类型递归合并准备、编码和 payload 解码的成功保证；动态长度、格式、资源限制
等输入边界校验仍返回 `error`。可通过 `decltype` 判断具体操作的返回类型，不能仅凭 `T`
断言整个反序列化过程恒成功。

`deserialize` 接受可写或只读、定长或动态的字节 span，采用准备暂存对象、解码临时对象、
成功后提交的事务语义；错误或异常不改变原目标。目标必须满足 `transactional<T>`，
默认保留容器 allocator，必要时由反射逐成员复制和提交 aggregate；带 allocator 的
非 aggregate 可以特化 `transaction_traits<T>`，保证 `clone` 保留 `commit` 的前置条件。
普通类型仍使用复制和无异常交换，不引入运行时类型擦除或 allocator 比较
根 `std::vector` 由容器解码器完整替换，因此使用原 allocator 创建空暂存，避免复制后清空旧元素
`transaction_traits::clone` 本身仍保持值复制语义，record 的未序列化状态与固定容器已有元素不变
`std::inplace_vector` 区分固定容量与固定长度，输入元素数超过容量返回 `resource_limit_exceeded`
其元素须满足安全的无异常提交及 allocator 条件，否则需显式定制事务
根原生数组采用元素级复制和交换。未序列化状态沿用各 codec 的规则，不合并动态容器
中的旧元素；读取新对象时由调用方先默认构造目标即可。

定长类型可以只根据类型在编译期取得完整文档的尺寸，包括文档头、映射定义（如果写入）和根 entry：

```cpp
constexpr auto size = xasrl::serialized_size<FixedRecord>();
std::array<std::byte, size> bytes;
if (auto status = xasrl::serialize(value, std::span{bytes}, context); status) {
    return status;
}
```

`serialized_size<T>()` 是 `consteval noexcept`，直接返回 `xasrl::szt`，仅对定长类型可用。
统一的 `serialized_size(value, context)` 调用对定长类型选择 `constexpr noexcept` 重载，
同样直接返回尺寸，不执行转换、不绑定 handler；尺寸可知不代表实际编码一定成功。
变长类型返回 `std::expected<xasrl::szt, xasrl::error>`，可能调用转换及 handler。需要逐元素记录的
变长类型须调用 `serialized_size(recorder, value, context)`。分配外部缓冲时先检查测量结果，
再提供精确长度的 span。

span 的 `Extent` 从实参推导，类型编码尺寸与 span 长度均已知且不匹配时在编译期拒绝，
否则在写入前检查实际长度。反序列化不能以目标类型的编码尺寸限制输入长度，因为
兼容 record 可以包含额外字段；所有输入仍经过 wire 边界验证。

## 对象层级

对象自上而下分为：

1. 游戏运行时对象：可能包含指针、句柄、缓存等运行时状态。
2. 游戏元数据对象：脱离运行时环境后仍有意义的数据。
3. 已序列化内存对象：`document_header + definitions + root_entry`。
4. 已存储磁盘块：`storage_head + 完整内存文档`

库实现第 2、3、4 层，并支持字段级运行时表示与持久化表示转换。转换的业务语义由
游戏代码提供，库负责调用、上下文传递和字节编解码；未标记字段不会进入二进制。

## Tag 的适用位置

SRL 的 tag 声明用 `[[=...]]` 附上 `xasrl::tag::target` 位掩码，类似 Java 的 `@Target`。
例如，`auto_stream` 标有 `compound | member_field | base_class`，分别表示类类型声明、
非静态数据成员和直接基类；`bit_flag` 标有 `enum_type`，表示枚举类型声明，区别于枚举项 `enumerator`。

工具可用 `std::meta::annotations_of_with_type(^^xasrl::tag::auto_stream, ^^xasrl::tag::target)`
查询，并用 `std::meta::extract<xasrl::tag::target>` 读取掩码。
默认只提供位置提示；`srl.tests` 用 `SRL_VALIDATE_ANNOTATION_TARGETS=1` 开启编译期位置检查，
违规会报告 `invalid_annotation_target`。具体类型、注解组合及模式限制仍由原有 schema 规则校验。

## 反射记录

非叶节点记录只序列化带有稳定 ID 的成员和直接非虚基类：

```cpp
struct Person {
    [[=xasrl::tag::field{.id = 1}]]
    int age{};

    [[=xasrl::tag::field{.id = 2}]]
    double score{};

    void *runtime_object{}; // 不参与序列化。
};
```

反射在编译期生成 ID 到字段的映射规则。重复 ID、没有转换规则的指针、引用、虚基类、
bit-field 和没有序列化规则的字段都会使 `schema_valid(^^T)` 返回 `false`，并在调用
`serialize` 时触发编译期错误。

解码记录时，以目标类型的字段 ID 为准：目标需要的字段必须存在，源对象中的未知字段
会被忽略。因此同一份二进制可以转换为多个字段兼容的元数据类型。

缺失字段默认返回 `missing_field`，且目标保持不变。只有显式附加
`[[=xasrl::tag::fallback{}]]` 的字段才使用字段类型的 `T{}`；这不会重新求值宿主类型的
成员初始化器。`fallback{defaults::value<42>{}}` 指定固定默认值，`provider` 和
`modifier` 支持业务生成和修改。fallback 仅用于缺失字段，不能修复存在但损坏的数据。
该策略保存在 `schema_node::fallback_type`，编解码与 schema 展示共同读取它。

### 自动默认值

不想在成员初始化器和 fallback 中维护两份默认值时，显式选择 `defaults::automatic{}`

```cpp
struct Player {
    [[=xasrl::tag::field{1}]]
    int id{};

    [[=xasrl::tag::field{2}, =xasrl::tag::fallback{xasrl::defaults::automatic{}}]]
    int hp = 100;
};
```

缺失 ID 2 时恢复为 `100`，而非目标中先前的值；存在 ID 2 时读取输入值。
未标注 fallback 的 ID 1 仍然必填。普通 `fallback{}` 保持字段类型 `T{}` 的原有语义

automatic 在编译期构造声明该成员的 `Owner{}`，取得构造完成后的成员值。
这不是直接反射成员初始化表达式：构造函数覆盖 `hp` 时取覆盖后的值；
没有成员初始化器但由构造函数赋值的字段也适用。嵌套字段只要求自己的声明宿主可求值，
不要求整个根对象可在编译期构造。宿主的构造、读取与销毁必须能完成常量求值，
取得的值也必须能被安全保存并赋给目标字段

支持标量、可作常量模板实参的结构化类值、原生数组、`std::array`，以及使用标准 allocator 的
`std::string`、`std::vector`；数组和容器元素可以递归组合。字符串和 vector 在编译期保存元素快照，
缺失字段时只重建该成员，不重新构造宿主。宿主本身可以包含编译期临时分配的容器，
只要分配在求值结束前被释放；不要求整个宿主能保存成 `constexpr` 变量

私有成员也可自动取值，但私有成员所在的宿主与字段自身的类型不同：字段类型若为包含私有数据的
非结构化自定义类，当前不能自动保存其值，需要显式 `provider`

宿主不能默认构造、构造需要运行时资源，或默认值无法编译期取得／保存时，schema 在编译期拒绝。
不会退回 `T{}`、保留旧值或改为运行时构造宿主。此时可显式使用 `value` 或 `provider` 策略。
automatic 同样只处理缺失字段，不能用于 stream、leaf 内部成员或 transient 成员，
也不会把存在但损坏的数据替换成默认值

对文档字节定位的错误，其 `offset` 相对内存文档起点；嵌套 codec 每层只累计一次 payload 起点。
`decode` 不传递预算、字段 ID 或偏移参数；调用方仅在解码失败时补充错误位置。
`error.id` 是 `std::optional<id_type>`，没有 ID 的结构错误与真实字段 ID `0` 明确区分。
自定义 codec 返回相对自身输入的偏移，库负责转换为文档偏移。
`error.system_error` 仅在系统调用失败时包含 `std::error_code`；成功路径不初始化系统错误分类。
`schema(^^T).tree`、对象布局和枚举策略只保存合法元数据，不再包含 `valid` 标志。
构建器遇到错误立即抛出编译期异常，停止遍历；只有成功后才固化元数据数组。
GCC 的异常诊断包含错误原因、根类型到出错实体的路径，以及可用的字段 ID、冲突字段或 mask 信息。
`schema_valid(^^T)` 保留非报错探测用途；`inspect_schema(^^T)` 捕获库自身的 schema 异常并返回
结构化 `schema_issue`（`reason`、`entity`、`path` 和 `detail`），成功时 `reason == schema_error::none`。
`schema_diagnostic(^^T)` 将该报告格式化为静态字符串，可用于调用方自己的 `static_assert`。
探测不返回部分构建的树，也不捕获用户自定义代码或编译器反射操作产生的其他异常。

独立的编译失败测试检查实际 GCC 诊断；传入 xmake verbose 输出中选定的编译器，不修改工具链配置：

```powershell
./tests/compile_fail/run.ps1 -Compiler C:/msys64/ucrt64/bin/x86_64-w64-mingw32-g++.exe
```

`context.config.limits.max_input_bytes` 控制解码输入大小，默认上限 256 MiB，
超限返回 `resource_limit_exceeded`。输入大小和根 entry 的字节边界在复制目标之前验证。
解码不再维护单容器元素数、累计元素数或累计元素存储预算。

### 自动顺序 ID

`auto_seq` 将直接基类和成员都纳入记录，ID 从 `0` 开始，先按直接基类声明顺序，再按成员
声明顺序递增。`transient` 会跳过对应实体；显式 `field` 会设置当前 ID，后续实体从该 ID
的下一个值继续，类似枚举值的自动顺延。重复 ID 或顺延溢出会使 schema 无效。

```cpp
struct [[=xasrl::tag::auto_seq{}]] Base {
    std::uint32_t base_value{}; // ID 0
};

struct [[=xasrl::tag::auto_seq{}]] State : Base {
    std::uint32_t value{}; // ID 1

    [[=xasrl::tag::transient{}]]
    void *runtime_only{}; // 跳过

    [[=xasrl::tag::field{.id = 4}]]
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

struct [[=xasrl::tag::auto_stream{}]] State
    : [[=xasrl::tag::auto_stream{}]] Plain {
    [[=xasrl::tag::auto_stream{}]] Plain detail;
    bool active{};
    [[=xasrl::tag::transient{}]] void *cache{};
};
```

注解可以放在类型、字段或直接基类上。字段／基类注解只覆盖当前位置的子对象，
允许普通未标注类、显式 record 和 `auto_seq` 类；不会递归改变更深层类型的编码。
显式 record 的父字段仍需要 `field{id}`，该 ID 属于父 record。
局部覆盖不支持 leaf 类型、自定义 leaf codec 类型、容器或非类类型。
类型级 `auto_stream` 与 `auto_seq`、`leaf` 互斥；字段级注解不能与
`leaf`、`container`、`transient` 或 `bypass_pointer_check` 同时使用。
与 `conversion_tag` 同时使用时，stream 模式应用于转换后的持久化类型。

wire v5 的 stream 仍按声明顺序写出，`kind() = entry_kind::stream (2)`，
`mapping_count() = 0`。定义写入模式会存放顺序定义；schema-only 模式从编译期 schema 取得定义。
两种模式都不为字段写入偏移表。
stream header 记录由字段顺序、名称和 wire 类型生成的布局指纹；同长度字段换序会被拒绝。
截断、越界及尾随字节返回可恢复错误。
没有未知字段跳过或缺失字段补齐。stream 自身的错误以字节偏移定位，嵌套 record
仍保留真实字段 ID。`deserialize` 失败时不改变目标对象。

所有现有序列化和文件 API 均支持该模式；`reflected_stream<T>` 用于识别类型注解，
`serialized_field_count(^^T)` 返回非 transient 实体数，`schema_string(^^T)` 展示加载顺序。
这里的“流”指字节区间上的顺序消费，不提供分块或异步 I/O。

## 内存对象赋值

包含 `srl/mem/struct_transform.hpp` 后，用 `xasrl::mem::transform_assign(dst, src)` 对两个 record 的直接字段赋值，
源为左值时按左值赋值，传入 `std::move(src)` 时按右值赋值。
两端 record 的拷贝或移动构造、赋值函数可以由编译器隐式声明或显式写成 `= default`，
但不能自定义实现或显式写成 `= delete`。
目标字段优先匹配同主 ID 的源字段，否则尝试目标字段的 `[[=xasrl::tag::accept_id{旧ID}]]`。
没有匹配时保留目标原值；多个别名同时命中、字段不可赋值或算术转换发生窄化时拒绝编译。
跨版本重定向宜使用显式 ID，避免字段重排改变 `auto_seq` 分配的 ID

别名仅在内存赋值时生效，不影响序列化格式，也不会触发序列化转换或缺失字段的 fallback。
赋值按目标 ID 顺序执行，不递归匹配。移动时同一个源字段只能匹配一个目标字段；
移动赋值抛出异常时直接传播，不回滚，也不保证源对象或目标对象的状态

## 字段表示转换

`xasrl::conversion_tag` 将字段的内存类型 `MemTy` 映射到编译期确定的持久化类型 `SrlTy`：

```cpp
struct conversion_tag {
    std::meta::info target_type;
    const void *handler_id = nullptr;
};
```

保存路径为 `MemTy -> SrlTy -> bytes`，读取路径相反。标记不代替字段选择：普通 record 仍需 `tag::field`，auto_seq / auto_stream
可以使用自动选择的字段。

### 直接转换

```cpp
struct RuntimeCount {
    int value{};
    RuntimeCount() = default;
    explicit RuntimeCount(int value) : value(value) {}
    explicit operator int() const { return value; }
};

struct Counters {
    [[=xasrl::tag::field{1}]]
    [[=xasrl::conversion_tag{^^int}]]
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
    [[=xasrl::tag::field{1}]]
    [[=xasrl::conversion_tag{^^std::string, xasrl::handler_key<AssetReferenceKey>()}]]
    Asset *asset{};
};

class AssetHandler : public xasrl::typed_reference_handler<Asset*, std::string> {
    xasrl::error to_storage(Asset *const &mem, std::string &srl) override;
    xasrl::error from_storage(Asset *&mem, const std::string &srl) override;
    // Implement using the application's asset registry.
};

AssetHandler handler;
xasrl::serialization_context context;
context.register_handler(xasrl::handler_key<AssetReferenceKey>(), handler);
// xasrl::byte_buffer bytes;
// auto encoded = xasrl::serialize(state, bytes, context);
// RenderState restored{};
// After checking encoded, call xasrl::deserialize(bytes.span(), restored, context).
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
- `serialize` 的参数顺序固定为源对象、目标缓冲区、`srl_context`。需要逐元素记录时，
  用 `srl_context{context, recorder}` 同时传入原有 handler、运行时配置和记录器；其余类型
  可以传 `srl_context{context}` 或直接传原有上下文。`deserialize` 和 `serialized_size`
  继续使用原有签名；纯类型查询 `serialized_size<T>()` 不读取对象或上下文。
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

## Flags 枚举

普通枚举只接受显式声明的枚举值。需要按位组合时，使用 `flags{0}`（或 `flags{}`）
将所有声明枚举值按位或，自动推导允许位掩码：

```cpp
enum class [[=xasrl::tag::bit_flag{0}]] Permissions : std::uint8_t {
    read = 1, write = 2, special = 0x80,
};
```

也可用非零 mask（如 `flags{0x83}`）显式指定允许位。最终 mask 必须非零；空枚举或
仅包含零值的枚举无法推导有效 mask，在编译期拒绝。
允许 mask 的任意子集，包括未命名的组合和零；未知位返回 `invalid_enum_value`。
标记不会生成位运算符，也不会根据 operator| 猜测语义。底层类型必须是最多 64 位的无符号
整数，mask 必须可表示，所有声明枚举值必须在 mask 内；重复标记、越界 mask 或不合法
底层类型在 schema 构建时拒绝。普通字段、容器元素和对象表示 leaf 共用相同校验。

## 错误展示

`xasrl::error` 保持轻量，不在返回错误时分配文本。需要日志时显式包含并调用：

```cpp
#include "srl/diagnostics.hpp"

auto text = xasrl::format_error(status);
auto located = xasrl::format_error(status, "profile.active");
// invalid_bool path=profile.active id=0 offset=123
```

输出包含错误码、可选 ID、字节偏移，以及存在时的系统错误分类、数值和消息。
调用方已知路径时可以传入第二个参数；函数不会仅凭 ID 猜测嵌套字段路径。
格式化过程可以分配和抛异常，不影响原有编解码错误契约。

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
struct xasrl::container_traits<MyContainer>
    : xasrl::default_container_traits<MyContainer> {
    static void clear(MyContainer &value);
    static void reserve(MyContainer &value, std::size_t size); // 可选
    static void insert(MyContainer &value, element_type element);
};
```

解码唯一 set/map 时默认沿用容器语义去重。根容器通过上下文请求拒绝重复项：

```cpp
xasrl::serialization_context context{
    xasrl::serialization_config{.duplicates = xasrl::duplicate_policy::reject}};
```

反射字段可用独立 tag 覆盖上下文策略：

```cpp
[[=xasrl::tag::field{.id = 1}]]
[[=xasrl::tag::container{
    .duplicates = xasrl::duplicate_policy::reject}]]
std::set<int> ids;
```

## 叶节点类型

叶节点类型作为一个整体参与序列化，其 content 中不再启动嵌套 entry。用户定义类型在类型声明
处标记：

```cpp
struct [[=xasrl::tag::leaf{}]] Vec2 {
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
自动和强制 leaf 共用此布局：默认读取先验证 bool／枚举，再复制有效成员；非法对象表示
不会进入 C++ 对象。无 padding、无 transient 的布局仍可合并为一次 `memcpy`。
包含 union 的自动对象布局无效，必须使用能表达活动分支的自定义 codec。

`[[=xasrl::tag::bypass_validation{}]]` 可标记类型、成员或基类，跳过该序列化子树的
内建标量值域检查，包括 bool 的 `0/1` 检查、枚举成员检查和 flags 掩码检查：

```cpp
struct [[=xasrl::tag::auto_seq{}]] TrustedFields {
    [[=xasrl::tag::bypass_validation{}]]
    std::vector<Mode> trusted_modes;
    Mode checked_mode; // 此字段仍校验
};
```

标记向嵌套 record、stream、容器元素／键值、数组和对象布局成员传播；字段转换时作用于
转换后的存储子树。类型上的标记作用于该类型的所有序列化使用位置，字段上的标记仅作用于
该字段；子节点不能重新开启已跳过的检查。重复标记、非序列化成员或 transient 成员上的
标记会在编译期报错。该选项不改变编码字节、尺寸或 wire 契约标识。

调用方须保证输入具有合法的 C++ 值表示，尤其 bool 及 bool 底层枚举的字节仍须为 `0/1`；
该标记不提供非法字节的归一化语义。长度、偏移、溢出、格式、资源限额和 schema 检查保留，
自定义 `leaf_codec` 和转换 handler 仍完整执行。内建解码的返回类型根据有效策略推导：
没有剩余值域检查的固定尺寸 payload 可返回 `always_success`，外层解析仍可能返回错误。

自动叶节点类型可以用 `transient` 排除仅供运行时使用的成员：

```cpp
struct [[=xasrl::tag::leaf{}]] CachedValue {
    std::uint32_t value{};

    [[=xasrl::tag::transient{}]]
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
struct [[=xasrl::tag::leaf{}]]
       [[=xasrl::tag::bypass_pointer_check{}]] ProcessLocalHandle {
    void *value{};
};

struct [[=xasrl::tag::auto_seq{}]] State {
    [[=xasrl::tag::bypass_pointer_check{}]]
    void *runtime_address{};
};
```

该 tag 不提供地址修复、跨进程有效性或可移植性。库在编译期要求目标使用 8-bit byte 和
小端序，不提供大端兼容或运行时字节序转换。

无法修改声明的第三方平凡类型可以在字段上附加 `leaf`，强制该字段直接复制对象表示：

```cpp
struct Record {
    [[=xasrl::tag::field{.id = 1}]]
    [[=xasrl::tag::leaf{}]]
    third_party::trivial_value value;
};
```

字段级强制叶节点要求类型 trivially copyable，并且默认要求独一对象表示；设置
`.allow_padding = true` 时 padding 占据格式空间但写零。需要包含指针表示时，另加
`[[=xasrl::tag::bypass_pointer_check{}]]`。

整数、浮点、枚举和 `bool` 是内建叶节点类型。`std::string` 始终走字符容器路径，不提供
`leaf_codec<std::string>`。其它固定尺寸的特殊类型可显式特化 `xasrl::leaf_codec<T>`。
自定义 codec 必须提供可在编译期求值的 `fixed_size`，并直接对调用方提供的
`std::span<std::byte, fixed_size>` 和 `std::span<const std::byte, fixed_size>` 编解码，不创建
中间 payload buffer。变长类型应使用容器或 record 表示。自定义类型仍应在声明处标记
`[[=xasrl::tag::leaf{}]]`。

## 内存格式

v5 使用小端字节序。每份文档的 16 字节前缀由 `u32 magic`（字节为 `SRL5`）、
`u32 definition_count`、`u64 schema_fingerprint` 组成。默认模式指纹为零，前缀之后是
按 ID 顺序排列的共享定义，再之后是一个根 entry。共享 ID 从 1 开始，只在本份文档内有效。

- 定义由 `u32 row_count + u32 kind` 和各行组成。每行是 `u32 field_id + u32 wire_type + u64 leaf_width`；
  变长的复合子项用全一宽度表示，应从子项 entry 头读取长度。record 的行按字段 ID 排序，
  stream 按声明顺序，sequence 和 map 分别只定义一个和两个槽位。
- entry 头仍是 16 字节：`u64 entry_size`、低 24 位为槽位数且高 8 位为 kind 的 `u32 metadata`、
  `u32 wire_type`。record 的槽位数等于定义的行数，sequence / map 使用实例槽位数，
  leaf 和 stream 的槽位数字段为零。默认模式的复合 entry 在头之后写 `u32 definition_id`，
  为零时紧跟内联定义；再按定义顺序拼接 payload。根 leaf 有 entry 头，子 leaf 只有 payload。
- `[[=xasrl::tag::inline_mapping{}]]` 可用于复合类型或字段，强制每次出现时写内联定义，
  后代仍独立决策。叶子、未序列化或 transient 字段不接受此注解；转换字段按存储类型判断。
  自动布局仅在编译期能保证复用至少两次时共享。相同定义按完整字节判等；
  动态容器的重复项不计入保证次数。运行时直接复制编译好的定义，不重建字段映射。
- 显式选择 `xasrl::schema_storage::schema_only` 时，定义数量为 `0xffffffff`，
  文档头包含完整 wire schema 指纹；不写共享区、内联定义或定义 ID。读端自动识别此模式，
  指纹不匹配即拒绝，不能投影到不同 schema。指纹只用于格式匹配，不是内容校验或身份认证。

```cpp
constexpr auto mode = xasrl::schema_storage::schema_only;
auto status = xasrl::serialize<mode>(value, bytes, context);
// 编译期固定长度：xasrl::serialized_size<T, mode>()
// 先准备后写：xasrl::prepared_serialization<T, mode>::prepare(value, context)
```

默认模式仍允许 record 兼容投影和未知字段跳过，但解码要顺序扫描前面的字段，不再按实例偏移直接跳转。
容器上限仍为 24 位槽位数；测长、字节偏移和尺寸使用 `xasrl::szt`。编码使用完整的可写缓冲区，
不提供不可回寻的流式 I/O。磁盘读取拒绝旧版本。

## 磁盘块

磁盘文件包含一个 storage header 和一份完整的 v5 内存文档（含文档头、可选共享定义及根 entry）：

```cpp
struct storage_head {
    std::uint64_t magic_number;
    std::uint32_t version;
    std::uint32_t compress_tag__cap_only;
};
```

当前版本为 `5`，压缩标记必须为 `0`。不兼容旧布局，读取时拒绝旧版本。
文件 I/O 分块读写，避免单次调用的 `streamsize` 限制，但序列化仍使用完整内存缓冲区。
Windows 写入先通过 `CREATE_NEW` 创建同目录唯一临时文件，完成写入并调用
`FlushFileBuffers` 后关闭句柄，再用同卷 rename 替换目标。写入或替换失败不会先截断
旧存档；替换失败返回 `io_commit_failed` 和系统错误码。未提交临时文件由 RAII 清理，
进程崩溃或清理失败可能留下带 `.srl-...tmp` 后缀的文件。此接口不承诺突然断电时目录
元数据的持久化，也不负责多写者之间的业务仲裁。暂不实现 CRC、压缩和增量更新。
`read_block(path, max_entry_bytes)` 在分配文件缓冲前检查大小；默认上限与解码输入相同。

```cpp
xasrl::serialization_context context;

xasrl::byte_buffer bytes;
if (auto status = xasrl::serialize(value, bytes, context); status) {
    return status;
}
if (auto status = xasrl::write_block(path, bytes.span()); status) {
    return status;
}
auto block = xasrl::read_block(path);
if (!block) {
    return block.error();
}
T restored{};
if (auto status = xasrl::deserialize(block->span(), restored, context); status) {
    return status;
}
```

`write_block` 和 `read_block` 只负责第 3、4 层转换，不接受对象或上下文。
`write_block` 返回 `xasrl::error`，`read_block` 返回 `std::expected<byte_buffer, xasrl::error>`。
对象文件读写由调用方显式组合上述入口；没有单独的 `save` / `load` 包装。

## 编译期 Schema 树

`schema(^^T).tree` 是统一的编译期结构描述。`root_node()` 和 `value_node()` 直接访问根节点和值节点；
对应的索引为 `schema_tree::root_index`（固定为 0）和 `tree.value_index()`。
leaf 根节点额外包含 entry header，而嵌套 leaf 值仅包含 payload。`tree[index, ordinals...]` 始终返回节点：
首个参数是树内的全局节点索引，之后每个参数是当前节点下从 0 开始的子节点序号，不是字段 ID。
仅需子节点索引时使用 `tree.child_indices(index)`。

```cpp
constexpr xasrl::schema description(^^Profile);
// 类型标签入口保留 reflected_stream<T> 的默认判断。
constexpr xasrl::schema typed_description(std::in_place_type<Profile>);
static_assert(xasrl::schema_valid(^^Profile));
constexpr auto value = description.tree.value_index();
constexpr auto first = description.tree[value, 0];
constexpr auto field_id = first.record_field().id;
constexpr auto stored_type = first.type(); // std::meta::info
constexpr auto encoded_size = xasrl::detail::root_encode_extent<Profile>;
```

节点是只读编译期视图。`kind()` 决定可用的形状数据，例如 `leaf()` 提供 codec 和可选对象布局，
`container()` 提供元素数量和槽位形状。`binding_kind()` 独立描述节点如何绑定到父节点：
`record_field()` 提供 ID 和可选 fallback，`stream_field()` 没有 ID，`slot()` 区分 element、key 和 mapped。
`field()` 访问 record 与 stream 共享的实体和转换信息。`field_id()` 在没有 ID 时返回空，而不是 0。
record 子节点按字段 ID 排序，stream 子节点按加载顺序排列；容器只描述 element 或 key/value
槽位，不按元素数量展开。同一类型在不同字段的 stream、forced leaf、转换及重复元素策略
分别记录在各自节点中。leaf 的对象表示是一个叶节点，其内部成员不是独立 wire 子节点。

`encode_extent` 递归计算编码长度，无法静态确定时为 `std::dynamic_extent`。
`decode_extent` 表示解码可要求的精确 payload 长度：leaf/forced 为固定尺寸，复合节点保留
动态尺寸，因为目标类型的编码长度并不限定所有合法输入布局；所有子节点解码尺寸固定的
闭合 stream 例外，可递推精确解码尺寸。定长内部编码和解码接口要求
静态 extent 的 span；公共字节输入在边界检查长度后再转换，不截断过长的 payload。

树构建、校验和尺寸计算使用非模板 `consteval` 函数与普通循环，结果固化为静态数组。
注解识别、叶类型资格、transient 复制区间、对象布局、wire ID 和事务聚合初始化表同样
直接操作反射值。`schema_valid(^^T)`、`inspect_schema(^^T)`、`schema_diagnostic(^^T)`、
`serialized_field_count(^^T)`、`schema_string(^^T)` 和 `type_hash(^^T)` 均为非模板入口。
模板保留在 codec/容器/事务自定义接口、表达式合法性检查、静态结果存储以及实际对象操作边界。
枚举值提取要求精确枚举类型，因此每个枚举保留一个提取函数；枚举值自身不再作为模板参数。
`make_enum_partitions(info, width)` 返回编译期 vector 值，调用方负责固化；
`enum_contains` 内部保存无指针的 `std::array` 值，分区使用有符号偏置后的无符号排序键。
事务聚合克隆按反射生成的初始化表定位成员和数组元素，保持原生数组的 brace elision 与 allocator。
schema 不保存对象、转换结果或运行期 handler；这些状态仍由编码计划及上下文持有。
codec 与编码计划直接从 schema 节点读取字段 ID、wire type 和尺寸，不通过子计划转发这些元数据。
非模板 `detail/codec_support.hpp` 负责公共 entry 类型检查、stream 切片、尺寸累加和重复策略选择；
实际对象操作、计划存储布局和成功/失败状态类型仍由模板生成。
容器元素是否需要编码记录由 schema 决定：只有需要保留字段转换结果的元素才留下逐项状态。
定宽、无转换的 sized range 以数量与静态步长做有界乘加测长，不需要遍历元素。
准备阶段计算整个文档的编码长度；写出游标按定义顺序在缓冲区中推进，并回填变长 entry 的长度，
不写实例字段偏移。动态容器写出前用 `size()` 取得当前层数量；非 sized range 额外遍历当前层计数，
不重测子树。转换记录按深度优先顺序追加和读取；普通字段的转换结果仍保留在所属计划内
同质原生数组的对象布局存储一次元素描述、数量及步长，复制和验证使用普通循环，
不按元素个数实例化模板

### 控制记录分配、复用准备结果

无转换的嵌套容器（如 `std::vector<std::vector<int>>`）不需要记录器。容器元素有字段转换
需要缓存时，必须显式传入记录器。先测长再编码到外部 span 时，可以使用同一个准备计划，
避免两次调用业务转换

```cpp
xasrl::encoding_recorder recorder{*std::pmr::new_delete_resource()};
auto prepared = xasrl::prepared_serialization<T>::prepare(recorder, value, context);
if (!prepared) { return prepared.error(); }
std::vector<std::byte> output(prepared->size());
return prepared->encode(std::span{output});
```

准备计划借用源，不是对象快照，工厂拒绝临时源。源及其引用的数据必须存活且保持不变，
包括形状、元素顺序和转换依赖；输入不能与编码输出重叠

同一个记录器一次只能服务一个需要记录的根计划，不可并发使用。记录器须比计划活得更久，
传入的内存资源须比记录器活得更久。计划销毁时自动回收记录、保留内存块供下次使用；
`retained_bytes()` 返回保留的块字节数，空闲时可调用 `release()` 归还这些块。记录器按需
分配，首块至少 4 KiB，后续按倍数扩容且不移动已创建的记录。资源耗尽时
分配异常照常传播。记录器只管理计划存储，不为输出或业务转换提供分配器

计划可以移动，也可重复写出；移动后的源计划只允许销毁。context 仅在准备时参与转换，
不由计划保存。直接写 `byte_buffer` 时调用
`serialize(value, bytes, xasrl::srl_context{context, recorder})`，不必预先测长。
`srl_context` 借用 `context` 和 `recorder`，不接管它们的生命周期

## Schema 可视化

`schema_string(^^T, options)` 在编译期返回具有静态存储期的 `std::string_view`，
无效 schema 会在编译期被拒绝。省略 `xasrl::schema_format_options` 时默认输出精简结构，
同一层级的字段标签按列对齐：
`[ID]` 标记 record 字段，`[#位置]` 标记 stream 的零基位置，`sequence[N]` 和 `map[N]`
标记容器数量（`*` 表示动态）

```cpp
constexpr auto compact = xasrl::schema_string(^^Profile);
constexpr auto detailed = xasrl::schema_string(^^Profile, {.detailed = true});
std::println("{0}\n\n{0:d}", xasrl::schema_view<^^Profile>{});
```

`b<数字>` 说明符改为绘制对象 leaf 内部的内存布局字节图：`<数字>` 是每行字节数的指数
（`b0`=1B、`b1`=2B、`b2`=4B、`b3`=8B，依此类推），省略数字时默认为 `b3`。
每个字段用 `[字段名: 类型]` 方框表示，方框宽度按占用的字节数成比例。顶部的 `4B-` 等刻度表示行内累计字节数，
左侧不显示行偏移，也不另列图例。跨行方框只在首行标注字段名和类型，续行用 `_` 表示，padding 用 `[.]` 表示。
`b` 与 `d` 不可同时使用

```cpp
std::println("{0:b}\n\n{0:b2}", xasrl::schema_view<^^Profile>{});  // 8B/行、4B/行
// 等价的编译期入口：
constexpr auto memory = xasrl::schema_string(^^Profile,
    {.memory_layout = true, .memory_layout_exponent = 3});
```

如果编译器在成员反射时展开了类型别名，可在显示为单个方框的成员上用注解显式保留原名；注解不改变序列化格式：

```cpp
using meters = float;
struct [[=xasrl::tag::leaf{.allow_non_unique_presentation = true}]] Position {
    [[=xasrl::schema_type_alias{^^meters}]] meters x;
};
// {:b} 显示 [x: meters]
```

元数据查询统一接收 `std::meta::info`。直接传入 alias 的反射即可保留调用处的 alias 名：

```cpp
using ProfileAlias = Profile;
constexpr auto schema = xasrl::schema_string(^^ProfileAlias);
```

Leaf 行始终显示 wire payload 字节数，不包含外层条目头；leaf 类别只在详细模式显示。
对象 leaf 用 `|` 标记布局条目，这些条目不是独立的 wire 节点。数组只显示重复次数和一份元素布局；
自定义 codec 不推测内部字节结构

```text
record Profile {
  [1] name    : sequence[*] {
    element: char [1B]
  }
  [2] position: Vec2 [8B] {
    | x: float [4B]
    | y: float [4B]
  }
  [3] state   : activity [1B]
}
```

详细模式（`{:d}`）额外显示容器类型、转换来源、字段 fallback／required 状态、
显式重复项策略，以及对象布局的 wire／object 偏移与数组步长。嵌套数组布局的偏移相对于单个元素

```text
Vec2 [direct leaf, 8B] {
  | [wire/object +0] x: float [4B]
  | [wire/object +4] y: float [4B]
}
```

内存布局模式（`{:b}`）把同一份对象布局画成按行截断的字节图（`{:b2}` 为每行 4B）：

```text
trans2 [12B] {
           4B-
  [ x: float ]
  [ y: float ]
  [rot: float]
}
```

## 字节缓冲存储

`byte_buffer` 直接拥有 allocator 分配的字节存储，支持容量复用、复制及移动。
`resize(n)` 初始化新增字节；`resize_uninitialized(n)` 不初始化新增区域，
调用方必须先写入再读取。`read_block` 使用后者直接填充文件内容。

`resize_and_overwrite(n, writer)` 向回调传入长度为 `n` 的可写 span，回调返回
`std::size_t` 有效长度（不超过 `n`），并负责初始化有效前缀。回调不得通过其他
别名修改缓冲。分配失败保留原值；回调抛出异常则长度清零、容量保留并传播异常。
非传播 allocator 的缓冲交换要求两个 allocator 相等，与标准容器一致。

当前 `serialize` 仍使用初始化扩容，编码失败后的既有契约保持不变；覆盖写入
接口的接入与序列化错误处理调整留待后续完成。
