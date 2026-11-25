# 内存序列化基准

`srl.benchmarks` 是非默认目标，使用 [Google Benchmark 1.9.5](https://google.github.io/benchmark/user_guide.html)。即使项目当前为 debug，也用 `-O3 -DNDEBUG` 编译该目标，并链接 Release 版 Google Benchmark，不需要重新配置 xmake

```powershell
xmake -b -v srl.benchmarks
xmake run srl.benchmarks --benchmark_min_time=0.1s --benchmark_repetitions=5 --benchmark_display_aggregates_only=true "--benchmark_out=$PWD/build/serialization-benchmarks.json" --benchmark_out_format=json
# 只测场景解码
xmake run srl.benchmarks '--benchmark_filter=^Decode/Scene/'
# 只测 leaf 对照组（编码和解码）
xmake run srl.benchmarks '--benchmark_filter=^(Encode|Decode)/Transform/'
```

| 场景 | 同一份逻辑数据 |
| --- | --- |
| `Snapshot` | 单个对象的 ID、标志和固定长度坐标/朝向 |
| `Samples/1024`、`Samples/65536` | 连续 `uint32_t` 缓冲区，原始数据为4 KiB和256 KiB |
| `Profile` | 带动态字符串和32件物品的玩家档案 |
| `Scene/128`、`Scene/1024` | 128或1024个实体，包含固定状态、长短不一的名称和0到8个路径点 |
| `Transform/Single`、`Transform/Batch4096` | 单个或4096个16 B变换，分别用 `auto_stream` 和直接对象表示的 `tag::leaf` |

每个场景在同一 C++ 类型上对比 SRL 内嵌定义模式（`SRL_Embedded`）、SRL 仅 schema 模式（`SRL_SchemaOnly`）、[Bitsery 5.2.5](https://github.com/fraillt/bitsery/tree/v5.2.5) 的 buffer adapter 和 [cereal 1.3.2](https://uscilab.github.io/cereal/assets/doxygen/binary_8hpp_source.html) 的 Binary archive。Bitsery 和 cereal 使用顺序布局，不提供 SRL 默认 record 的字段映射或兼容投影；`schema_only` 不写定义但仍保留 SRL 文档头、schema 指纹和边界检查。结果不等价于在相同格式能力或输入校验策略下比速度

`Transform` 的 `Stream` 和 `Leaf` 使用完全相同的四个 `float` 字段和值。只有 SRL 的编码类型随注解改变，Bitsery 和 cereal 对两者都使用同一份逐字段映射。批量场景直接以 `std::vector` 为根，`items_per_second` 按变换个数计算

- `Encode`：从已构造的对象编码到复用的内存缓冲区，包含序列化规划、编码和库的内存适配开销。SRL 的 recorder 也复用，但不复用编码计划
- `Decode`：从预生成的热内存字节读到每次新建的同类型拥有型对象。计时包含解码校验、必要分配，以及对象的构造和销毁；cereal 直接用 `ispanstream` 借用内存，不复制输入
- 数据构造、首次编码、逐字段往返校验和报告输出均在计时外，不计文件 I/O。未测冷缓存或首次分配输出缓冲区的成本
- JSON 的 `wire_bytes` 是一份完整的未压缩序列化数据的精确字节数，包括各格式自己的头部；不是缓冲区容量或可执行文件体积。控制台可能缩写大数。优先看中位数的 CPU 时间和 `items_per_second`；`bytes_per_second` 以各库自己的字节数为分子，不能单独用来比较转换速度
