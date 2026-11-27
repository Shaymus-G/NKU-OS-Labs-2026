# Lab5 B 联合验收与双重 GDB

陈铭宇（2412559），2026-11-27，Asia/Shanghai。集成基线 `a7295ce9872cde2dc5dc0964b006b703a167aa54`。本轮没有新增内核源码修改；新增了取证工具和总报告，适配现有客体 GDB 脚本。阶段 0～4 历史见 [装载器报告](b-user-loader.md)，全组正文见 [report.md](../../report.md)。

## 本机运行与评分

本机使用 GCC 10.2.0、客体 GDB 10.1、QEMU 4.1.1、OpenSBI v0.4、宿主 GDB 15.1、virt 单 hart 128 MiB。与 C 的 QEMU 6.2/OpenSBI 1.3 环境分别记录。

[集成版本与源码哈希](../logs/b-integration-20261127/manifest.json)；[干净构建](../logs/b-integration-20261127/build.log)；[真实 hello](../logs/b-integration-20261127/hello-qemu.log)；[16 项专项结果](../logs/b-integration-20261127/suite.log)；[教师评分 130/130](../logs/b-integration-20261127/grade.log)。本机直接 `make grade`，无需 C 环境的串口适配器。评分之后已重建普通 hello 镜像。

原始构建日志保留 testbss 的故意越界警告和 WSL/NTFS 小于一秒的文件时间偏差警告；从 clean 开始完整编译后，实际镜像的用户回归、评分与调试均通过。

专项脚本为 COW/逐页复制各运行 hello、exit、yield、spin、forktest、forktree、waitkill、c_lifecycle。专项副本注入额外真实内核测试，但没有替换 fork/调度/退出实现；主路径采用正式源码和用户 ELF。真实耗尽回滚、CLONE_VM、页引用、孤儿接管、跨页退出码及父子写隔离均通过，两套专项空闲页都恢复为 31548→31548。普通启动和专项测试在观察到 init 回收标记后由脚本终止 QEMU，不冒称内核自动关机。

补充 [COW 用户边界](../logs/b-integration-20261127/cow-boundary.log)与[逐页复制用户边界](../logs/b-integration-20261127/eager-boundary.log)：真实用户代码执行非法号 0/最大无符号值，返回 -E_INVAL 后继续执行；fork 子进程请求来自内核地址或损坏 ELF 的 exec，失败后原程序继续；随后请求用户缓冲区内的合法 ELF，成功进入新程序并 SYS_exit(42)，父进程 wait 验证退出码。测试程序只复制到临时树，未加入正式 user/。

## 两个调试目标

客体 GDB 调试 RV64 内核与用户程序，宿主 GDB 调试运行这些程序的 x86-64 QEMU。两个 GDB 同时控制的是同一个 QEMU 进程。脚本通过 GDB/MI 协调，保存原始 MI 文本，并在每个阶段读取真实寄存器/变量作断言；没有内核测试钩子。

现有安装文件已经 strip，但源码构建目录的 `riscv64-softmmu/qemu-system-riscv64` 保留调试信息。两者 Build ID 均为 `43a57e28ba870726acd7b94ba70b3c61d20e36f5`，`.text` 的 SHA-256 也完全相同。[身份与源码哈希](../logs/b-dual-gdb-20261127-attempt5/identity.json)记录了证据，因此没有拿不匹配的源码来解释另一个 QEMU。

## 已观察的关键现场

[完整摘要](../logs/b-dual-gdb-20261127-attempt5/summary.log)、[宿主原始日志](../logs/b-dual-gdb-20261127-attempt5/host-mi.log)、[客体原始日志](../logs/b-dual-gdb-20261127-attempt5/guest-mi.log)。

| 位置 | 实测 |
| --- | --- |
| 用户入口 | pc=0x800020，sp=0x80000000，宿主 env.priv=0 |
| 用户 ecall 前 | pc=0x8000aa，a0=30（putc） |
| TCG 翻译 | trans_ecall 的 ctx.pc_next=0x8000aa；调用栈含 translator_loop、gen_intermediate_code、tb_gen_code |
| 执行异常 helper | exception=8，env.priv=0，env.pc=0x8000aa |
| 投递后 | env.priv=1，scause=8，sepc=0x8000aa，pc=0xffffffffc0200e00 |
| syscall 分发 | tf.epc=0x8000ae，保存用户 SP=0x7ffffef0 |
| sret 前 | pc=0xffffffffc0200eca，用户 sp 已恢复；sepc=0x8000ae，sscratch=0xffffffffc04b8000 |
| TCG 翻译返回指令 | trans_sret 的 ctx.pc_next=0xffffffffc0200eca |
| 返回 helper | helper_sret 前 priv=1，结束后 priv=0，返回 PC=0x8000ae |
| 用户继续 | pc=0x8000ae，sp=0x7ffffef0，a0=0 |

上述地址来自本次 ELF 和镜像，脚本动态查找用户 sys_putc 的 ecall，不能将数字当作其他构建的固定断点。另有[独立客体 GDB 日志](../logs/b-integration-20261127/guest-gdb.log)，退出状态为 0，证明单独脚本也能复现往返。

## QEMU 4.1.1 中的源码链

本机源码根为 `/mnt/d/3nd_grade/OS/environment/qemu-4.1.1`，下列均为实际调试所用源码。

1. `target/riscv/insn_trans/trans_privileged.inc.c:21` 的 trans_ecall 调用 generate_exception。`translate.c:89` 将客体 PC 写入对应 TCG 变量，并生成对异常 helper 的调用。这里在产生 TCG 中间操作，不是在翻译函数中立即改变客体特权级。
2. TCG 生成的宿主代码执行后，进入 `op_helper.c` 的 helper_raise_exception/riscv_raise_exception。它设置 CPU 的异常索引，并退出当前执行块；宿主栈能看到来自 JIT 代码的调用。
3. `cpu_helper.c:503` 的 riscv_cpu_do_interrupt 根据异常类型、当前特权级和委派设置决定进入 S/M 态。U ecall 被委派到 S 态时保存 sepc/scause、更新状态并将 PC 指向 stvec，最后设置 priv=S。硬件模拟到此还没有替 ucore 执行 epc+=4，后者属于内核异常处理。
4. `trans_privileged.inc.c:43` 的 trans_sret 产生返回 helper 的调用，并结束当前 TB。`op_helper.c:74` 的 helper_sret 验证权限、读取 sepc、恢复先前特权级并返回目标 PC。观察中该 helper 的 priv 从 1 变为 0，返回的地址随后成为用户 PC。

TCG 先把客体指令译成中间操作，再生成可执行宿主代码，缓存为翻译块；之后运行缓存块不必每次重新译码。单步状态会影响块的选择/生成，所以本轮在 ecall 和 sret 前分别单步并设置宿主断点，确实观察到了翻译及执行两个阶段。Lab2 的页表查询同样发生在经过翻译的客体访存执行期间，页表遍历/TLB 缓存与 TCG 块缓存是不同机制。

这份源码较旧，helper 中状态位细节应按本次模拟器配置解读；不能用一次旧 QEMU 的 mstatus 数值替代最新版 RISC-V 规范。当前 sstatus/SRET 的规范含义可对照 [RISC-V Supervisor ISA](https://docs.riscv.org/reference/isa/v20260120/priv/supervisor.html)。

## 实际调试问题

- **CSR 远程读取失败：**原客体脚本停在首条用户指令后读取 `$satp`，旧 QEMU 返回 E14。[初次失败](../logs/b-integration-20261127/guest-gdb-csr-failure.log)保留。独立客体脚本改用 monitor info registers 及内核已保存的 tf；双重脚本直接读取模拟 CPU 的 env，未修改内核执行语义。
- **U 态访问内核断点失败：**旧远程接口在 U 态读取内核地址以解码断点时失败。[第二次失败](../logs/b-integration-20261127/guest-gdb-kernel-access-failure.log)保留。改在用户 sys_putc 找到实际 ecall 并 si，进入内核后再设置内核断点；sret 同样单步到返回地址。
- **宿主输入被 QEMU 抢读：**早期用 -nographic 启动宿主 inferior，串口/监视器与 GDB 共用输入，MI 命令前缀被吞掉。改用 -display none、-monitor none、-serial file:...，每个调试目标有独立通道。
- **宏不是调试变量：**first_cpu 是 QEMU 的宏，调试信息中没有该符号。依据头文件改为 cpus.tqh_first，直接读取结构，不在调试器中调用可能改变执行状态的辅助函数。
- **地址不能过早读：**验证 sret 指令编码时也遇到 U 态读取内核内存失败，故移动到进入 syscall 后再读。最终脚本动态取地址并验证指令编码，避免盲目假设位置。

双重调试 attempt1～4 的失败原始日志均保留，attempt5 为成功证据。日志是实际工具输出；此处是事后整理的解释，不是伪造的完整人机对话。

## 复现

在 WSL 工程根目录准备 GCC/GDB/QEMU PATH 后：

```bash
python3 tools/verify-lab5-b.py --log-dir /tmp/lab5-b-integration
python3 tools/verify-lab5-b.py --boundary-only --log-dir /tmp/lab5-b-integration
python3 tools/lab5-dual-gdb.py \
  --qemu /mnt/d/3nd_grade/OS/environment/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64 \
  --log-dir /tmp/lab5-b-dual
```

第一条会 clean/构建、运行正式 hello、执行专项、原样教师评分、重建 hello 并运行客体 GDB。第二条只在临时副本增加用户边界程序。第三条需要本机匹配源码和符号的 QEMU，使用同一正式 hello 镜像及动态本机端口。不要在评分清理镜像时同时运行 GDB。
