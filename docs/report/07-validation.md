## 7. 综合验证

### 7.1 测试层次与结果

测试分为框架检查、算法行为检查、对象分配检查和调试观察。算法测试通过公共接口改变状态，配合描述符、计数和独立占用表验证；地址转换测试将合成页表判据与实际 GDB 记录分开。

| 验证项 | 范围 | 实际结果 |
|---|---|---|
| 教师评分 | Page Init、Best-Fit | A/B 原环境及 C 本机均为 20/20 |
| Best-Fit 行为 | 选择、拆分、三类合并、失败和恢复 | 全部通过 |
| Buddy | 取整、拆分、伙伴合并、碎片及 1024 次混合操作 | 全部通过 |
| SLUB | 缓存、大小类、大对象、混合负载、耗尽及六次预期异常 | 两种 PMM 下通过 |
| DTB | 2230 项解析检查、四种内存容量 | 通过 |
| 地址转换 | 双重 GDB、切换对照、直接命中及自动查询 | 获得对应日志 |

主实验联合版在 A、B 的 QEMU 4.1.1 环境通过。新增 SLUB、DTB 和自动演示在 C 的 QEMU 6.2.0 环境验证，尚需在原环境复测这一批扩展。教师评分不包含 Buddy、SLUB 或调试结果。

最新默认评分见 [扩展后的教师评分](../logs/extensions-runs/dtb-memory-matrix/default-grade.log)。新内核的容量矩阵中，两种 PMM 与 SLUB 共完成八次正常启动。相应 [源码摘要与审计](../logs/extensions-final-audit.log) 用于关联版本；报告结构调整不改变这些代码和结果。

评分脚本的关键输出为：

```text
-check page init: OK
-check best fit:  OK
Total Score: 20/20
```

该结果说明教师检查入口通过；算法的特定性质仍由独立用例支撑。例如，“最佳匹配”需要验证选择的块，“伙伴合并”需要验证阶结构，“对象隔离”需要检查区间和内容。最终空闲页数恢复可检出计数泄漏，但单凭计数不能排除两次返回重叠地址，因此还需占用表和对象数据校验。

### 7.2 复现方法

在仓库根目录、已激活工具链的环境中，默认模式执行：

```bash
make clean
make
make qemu
```

退出 QEMU（Ctrl+A，松开，再按 X）后运行 `make grade`。C 的 OpenSBI 1.3 使用半主机控制台，需将输出转交给评分脚本读取：

```bash
make grade QEMU='python3 tools/qemu-grade-console.py'
```

转接脚本仅路由控制台输出，不修改评分断言。A/B 的 OpenSBI 0.4 环境使用普通评分。

Buddy 与对象测试分别使用以下编译宏；每次切换先执行 `make clean`，再重建并重新启动：

```bash
make DEFS=-DPMM_BUDDY
make qemu DEFS=-DPMM_BUDDY

# 退出 QEMU，清理后再启用对象测试
make clean
make DEFS='-DPMM_BUDDY -DLAB2_EXTENSIONS'
make qemu DEFS='-DPMM_BUDDY -DLAB2_EXTENSIONS'
```

自动验证入口：

```bash
python3 tools/test-dtb-parser.py
python3 tools/check-extensions.py --negative-tests --memory-matrix
python3 tools/test-sv39.py
python3 tools/demo-address-translation.py
```

各次运行保存到新目录，保留早期证据。测试脚本在内核完成输出后结束其空转进程；正常日志末尾的终止信号不是 panic 判据。
