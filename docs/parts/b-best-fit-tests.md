# B：Best-Fit 补充行为测试

验证日期：2026-10-18。源码基线为 lab2 分支的 79b5791，B 的实现、默认选择、脚本 LF 规则及本批测试已提交为 [f8ed38a8d6fb02e1093995eeb99d0744101eabe1](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/f8ed38a8d6fb02e1093995eeb99d0744101eabe1)，交接文档由后续提交补齐。运行日志中保存了相关源码 SHA256；原行为日志对应填写学号前，最终评分日志对应填写 2412559 后，算法及教师检查未因学号填写改变。

## 文件和入口

- kern/mm/best_fit_test.c：独立行为测试，调用真实的 alloc_pages/free_pages/nr_free_pages 接口。
- kern/mm/best_fit_pmm.h：声明行为测试入口。
- kern/mm/best_fit_pmm.c：增加 best_fit_check_all() 包装函数，check 回调先运行教师原 best_fit_check()，再运行新增测试。
- 教师 basic_check()、best_fit_check() 的函数内容不变；A 的 page_init()、Makefile 和评分断言本批不变。

普通 Best-Fit 启动即执行全部测试，不需要新增构建宏或命令行选项。其他管理器不调用 Best-Fit 的 check 回调。

## 测试边界和隔离

测试只用于启动自检阶段，要求 A 当前初始化提供一个至少 32 页的连续空闲区，且没有并发分配。测试先记录初始空闲块和保留页数量，再通过正常分配接口暂时取出全部空闲页；只在前 32 页构造布局，其余页暂不释放，防止背景空闲块影响选择和失败用例。

测试不直接改写私有 free_list、nr_free，也不重新初始化分配器。每个用例结束前收回其释放的全部页，最后归还整个原始区间。测试观察块头标志、长度、相邻链表连接、引用计数和空闲总数，并核对最终原始块位置、长度及保留页数量。

该测试面向当前单段初始化及 Best-Fit，不声明支持 Buddy、任意初始碎片布局、非法地址或重复释放。空闲区不足 32 页时会明确断言失败，不会跳过后打印成功。

## 覆盖和实际结果

下表偏移单位为页，相对临时取出的区间起点。

| 用例 | 断言重点 | 实际结果 |
|---|---|---|
| 最佳匹配 | 在偏移 0/12/22 处构造 8/5/7 页块，请求 4 页必须返回偏移 12；验证一页余块和回收恢复 | PASS |
| 精确匹配和同大小选择 | 逆地址顺序释放 6/4/4 页块；连续两个 4 页请求按低地址顺序取出，精确匹配不留余块 | PASS |
| 拆分 | 偏移 4 的 10 页块分配 3 页后，余块必须从偏移 7 开始，长度 7 | PASS |
| 向前合并 | 先释放前 3 页，再释放紧邻后 2 页，恢复同一个 5 页块 | PASS |
| 向后合并 | 先释放后 2 页，再释放前 3 页，恢复同一个 5 页块 | PASS |
| 双侧合并 | 先释放左右各 3 页，再释放中间 2 页，恢复 8 页连续块；旧块头标志被清除 | PASS |
| 碎片与耗尽失败 | 三个互不相邻的 4 页块，总量 12；请求 6 页必须失败，请求 13 页也失败；失败后布局和计数不变；耗尽后单页请求失败 | PASS |
| 多轮恢复 | 乱序释放 32 页区间的三部分、合并并整块重新分配，重复 3 轮 | PASS |
| 原始状态恢复 | 归还完整临时区后，原块地址、块数、长度、总空闲量及保留页数量与测试前相同 | PASS |

所有 PASS 均在相应断言执行后输出。QEMU 实际输出为：

```text
[Best-Fit behavior] minimum adequate block: PASS
[Best-Fit behavior] exact match and address tie: PASS
[Best-Fit behavior] split remainder: PASS
[Best-Fit behavior] merge previous block: PASS
[Best-Fit behavior] merge next block: PASS
[Best-Fit behavior] merge both neighbours: PASS
[Best-Fit behavior] fragmented and exhausted failures: PASS
[Best-Fit behavior] repeated full roundtrip: PASS
[Best-Fit behavior] original state restored: PASS (31928 free pages)
[Best-Fit behavior] ALL PASS
check_alloc_page() succeeded!
```

## 实际布局和回归

本次使用现有 WSL、SiFive GCC 10.2.0、QEMU 4.1.1 和默认 128 MiB RAM。

- 内核 end VA：0xffffffffc0207078。
- 描述符起点 PA：0x80208000。
- 空闲区：[0x80348000, 0x88000000)，31928 页。

新增代码让内核比上一版多占用一页；空闲页数相应减少一页。测试读取运行时计数，没有把此前的 31929 页写死。

本次完成 clean build、QEMU 行为断言和教师 make grade 回归。Page Init 和 Best-Fit 两项均 OK，总分 20/20。自动验证程序在内核完成输出并进入末尾循环后，于 5 秒时主动终止 QEMU；日志中的 signal 15 来自该清理动作。

日志：

- [编译日志](../logs/b-best-fit-behavior-build.log)
- [QEMU 原始输出及源码摘要](../logs/b-best-fit-behavior.log)
- [新增测试后的教师评分](../logs/b-grade-behavior.log)
- [填写学号后的最终评分及源码摘要](../logs/b-final-grade.log)
- [本轮实际要求和测试规格](../prompts/b-best-fit-tests.md)

用户随后手工复现了编译、QEMU 和评分，QEMU 启动及测试输出与上述日志一致，评分同为 20/20。手工复现截图保存在 docs/images/ 的 b-build.png、b-best-fit-layout.png、b-best-fit-behavior.png、b-grade.png；重复的 manual 日志已删除，保留上述日志作为统一记录。

## 手动复现

在 WSL 中进入实际工程目录，工具链和 QEMU 已在 PATH 时执行：

```bash
cd /mnt/d/3nd_grade/OS/lab/lab2/github
make clean
make
make qemu
```

确认出现全部行为 PASS 和 check_alloc_page() succeeded!，截图保存关键结果。退出 QEMU 按 Ctrl+A，松开，再按 X；随后可以运行：

```bash
set -o pipefail
make grade
```

每次重新构建的地址和页数以实际输出为准。make grade 会清理本地构建产物，之后再次手动运行时 make qemu 会重新构建。

## 2026-10-19 接入 Buddy 后回归

在 HEAD 6baf620ebb414d821c1e5647bdec9cb486636a5e 上，B 已先运行 Buddy 自检，再清理构建并切回默认 Best-Fit。本轮 WSL Ubuntu 24.04.5 LTS、GCC 10.2.0、QEMU 4.1.1、OpenSBI v0.4 下，既有 B 测试全部 PASS，空闲页恢复为 31928；普通教师评分仍为 20/20。测试函数和算法均未修改。

使用了独立 B 日志，保留上文 10 月 18 日历史日志和截图，不覆盖 C 的记录。运行后已恢复默认 Best-Fit 构建。本轮没有新增截图。联合回归日志与 B 报告更新提交为 [c3c4dd962cafea8050a1a1048973172e7408b244](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/c3c4dd962cafea8050a1a1048973172e7408b244)，本测试说明在后续文档提交中补齐。

- [本轮 Best-Fit 构建与最终默认恢复](../logs/b-integration-best-fit-build.log)
- [本轮 Best-Fit 行为测试](../logs/b-integration-best-fit-run.log)
- [本轮普通教师评分](../logs/b-integration-grade.log)
- [完整复核与两种模式操作记录](../notes/b-progress.md)
