# Lab4 成员 B 第一阶段交接（陈铭宇，2412559，2026-11-13）

【当前状态】
已补齐六个创建函数，并完成独立创建测试、失败回滚测试和 GDB 取证，可以准备与 C 接口联调。
分支：lab4；基线提交：e49d3c12c8fa4fea533167e58f32748c778052d8。
B 的代码与测试证据提交：ebe4c84（feat(lab4): implement kernel thread creation with isolated validation）。
本交接随本次 GitHub 交付更新；协作方在干净的 lab4 工作区执行 git pull --ff-only origin lab4 后接入。
正式 proc.c SHA-256：6e551db627c75adb42a3315ed620927e4a81a64ce01a050ba7f9a7319dea3731。

【修改内容】
kern/process/proc.c 的 function2：alloc_proc、copy_mm、copy_thread、do_fork、kernel_thread、proc_init。
标记已改为 LAB4 function2 2412559。C 的 function3 空位保留。A 的内存实现、公共头文件、Makefile、教师测试及评分脚本未修改。
新增 tools/b-create-check.py、b-create-tests.inc、b-create-stubs.c，仅用于临时副本的独立验证，不进入普通内核构建。

【请 C 对齐的接口】
1. idleproc：PID=0，使用 bootstack，current 初始指向 idle，need_resched=1；idle 不加入 proc_list 和 PID 哈希表，但计入 nr_process。find_proc(0) 按框架接口返回 NULL。
2. initproc：PID=1，parent=idleproc，名字 init；普通线程追加到 proc_list 尾部，并加入 PID 哈希表。
3. 新线程独占两页内核栈，mm=NULL，共享 boot_pgdir_pa。PCB 的 pgdir 是物理根页表地址，不是高位虚拟地址，也不是编码后的 SATP。
4. context 顺序是 ra、sp、s0-s11，共 112 字节。context.ra=forkret，context.sp=tf。
5. trapframe 位于 kstack+KSTACKSIZE-sizeof(trapframe)，共 288 字节。tf.epc=kernel_thread_entry，s0=函数，s1=参数，a0=0；默认 tf.sp=tf；SPP=1、SPIE=1、SIE=0。
6. do_fork 全程保存并关闭当前 hart 的本地 S 中断，成功或失败均恢复进入时状态；适用于当前单 hart、非阻塞分配器，未提供多核锁。
7. 返回值：成功为正 PID；数量达到 MAX_PROCESS 返回 -E_NO_FREE_PROC；PCB/栈不足返回 -E_NO_MEM；copy_mm 错误原样传播。

【已实际执行】
环境：WSL Ubuntu，GCC 10.2.0、GDB 10.1、QEMU 4.1.1，-O2 -g，单 hart。
执行命令（Windows PowerShell）：
wsl.exe -d Ubuntu -- env -u QEMU TZ=Asia/Shanghai PATH=/mnt/d/3nd_grade/OS/environment/riscv-toolchain/bin:/mnt/d/3nd_grade/OS/environment/qemu/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin python3 /mnt/d/3nd_grade/OS/lab/lab4/github/tools/b-create-check.py
脚本实际执行正式树 make clean / make -j2，以及临时副本 make -j2、QEMU 和 GDB batch。

预期与实际结果：
- 正式树：proc.c 编译通过，整体链接因 cpu_idle 尚未实现失败（make 退出码 2）。未宣称整体编译通过。
- 临时副本：构建退出码 0；教师 test_lab4_process_create 原文件、原断言 PASS。
- PCB 先填 0xa5 再分配初始化，检查所有字段：PASS。
- 16 个线程的 PID 唯一性、父指针、独立栈、首次现场、链表/哈希一致性：PASS。
- 指定 stack、子线程 a0=0、源 trapframe 不变：PASS。
- PCB、栈、mm 三类故障各注入 64 次：计数/登记不变，PCB/栈释放配对，空闲页数恢复，PASS。
- 真实占用全部剩余物理页后，内核栈分配失败及回滚：PASS；随后释放保留页并恢复空闲页数。
- MAX_PROCESS 上限（合成计数夹具，未真实创建 4096 个线程）：PASS。
- 创建成功/失败均保持进入时 SIE 状态：PASS。
- GDB 在 proc_init 完成后取得 idle/init、context、trapframe 真实记录，检查 PASS；GDB 退出码 0。
- QEMU 输出 ALL PASS 后由测试驱动 SBI 关机，实际退出码 0。首次线程函数和调度没有执行。

【证据与文档】
docs/logs/b-create-20261113-094247/
  result.json：结果、基线、源码哈希、工具版本
  normal-build.log / isolated-build.log：正式及隔离构建
  isolated-qemu.log：创建与失败回滚结果
  gdb-session.log / gdb-commands.txt：GDB 原始取证及命令
  isolation.patch：临时注入差异
GDB 使用文本取证，本阶段没有截图。调试用 isolated-kernel.elf 为本机产物，受 .gitignore 忽略，可运行脚本重建。
阶段分析：docs/parts/b-process-create.md
spec/prompt：docs/prompts/b-process-create.md
仓库交接：docs/notes/b-handover.md

【验证边界与已知问题】
所有故障注入、数量上限、清理夹具和 C 测试桩只在临时副本。测试桩如被实际调度/调用会 panic，不能当作 C 实现。
copy_mm 本阶段不分配用户地址空间；mm 失败是合成故障，仅验证 do_fork 预留回滚分支。
未验证 PID 长期回绕、多核、用户 mm 复制或完整退出回收；make grade 未执行，没有正式评分分数。
已有 trap 隐式声明、kmalloc 指针转换、教师调度测试函数指针类型警告保留在原日志，未改教师测试消除警告。

【下一步请 C 接入】
完成 cpu_idle、schedule、proc_run、switch_to、Lab3 trap 必要适配和 RV64/Sv39 SATP 路径。保留 forkrets，勿直接用旧的 Sv32 lsatp；勿保留 Lab3 无条件 1000 ticks 关机。
与 B 联调 switch_to → forkret → forkrets → __trapret → sret → kernel_thread_entry → init_main，确认函数和参数真实执行，再测多线程反复切换，最后联合跑正式 make / make qemu / make grade。
init_main 输出后框架 do_exit 会 panic("process exit!!.")，需结合路径识别，不要删掉 panic 制造通过。
以上仅是第一阶段创建交接，首次执行、调度及完整评分仍待联合验证。
