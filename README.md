# SRL

SRL 是一个面向游戏数据的 C++26 序列化实验。它用静态反射和注解在编译期生成 schema 和编解码逻辑，不需要手写字段注册表

- `tag::field` 为 record 声明稳定字段 ID；`auto_seq` 可自动分配 ID，`auto_stream` 则按声明顺序编码
- 默认将映射定义写入文档，record 可按字段 ID 读取兼容的结构；`schema_only` 省略定义，要求读写端的 wire schema 指纹一致
- 解码失败不改变目标对象；文件读写和对象编解码是独立接口

项目仍在开发。当前使用 v5 格式，仅支持8位字节的小端目标。不承诺跨编译器、跨编译器版本或跨格式版本兼容，也不提供旧存档迁移

## 构建与运行

在 Windows 上使用已配置的 xmake、支持本项目反射特性的 MinGW-w64 GCC 16及配套 libstdc++。在仓库根目录运行以下命令，构建成功后再运行示例

```powershell
xmake -b srl.example
xmake run srl.example
```

示例会打印 schema（包括对象布局），并演示内存往返和两种定义存储模式。GoogleTest 是非默认目标，需要时单独构建运行

```powershell
xmake -b srl.tests
xmake run srl.tests
```

`srl` 是纯头文件目标，本身不生成二进制。同一 xmake 项目中的目标通过 `add_deps("srl")` 继承头文件路径、C++ 标准、反射选项和链接依赖

## 最小用法

```cpp
#include "srl/core.hpp"

struct Player {
    [[=xasrl::tag::field{.id = 1}]]
    int level{};
};

int main() {
    const xasrl::serialization_context context;
    const Player source{.level = 26};
    xasrl::byte_buffer bytes;
    if (auto status = xasrl::serialize(source, bytes, context); status) {
        return 1;
    }

    Player restored{};
    if (auto status = xasrl::deserialize(bytes.span(), restored, context); status) {
        return 1;
    }
    return 0;
}
```

状态转换为 `bool` 时，`true` 表示失败

## 仓库导航

- [库接口与格式参考](src/srl/README.md)：字段注解、转换、错误模型、内存格式与磁盘块
- [可运行示例](examples/main.cpp)：schema 展示、对象往返和嵌套容器
- [测试](tests/)：GoogleTest 用例及编译失败诊断
- [性能基准](benchmarks/README.md)：`srl.benchmarks` 的运行方法与对比口径
- [构建配置](xmake.lua)和[仓库约定](AGENTS.md)
