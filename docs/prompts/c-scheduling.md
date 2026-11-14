# 调度与切换规格（编码前，2026-11-14）

尚文轩2412456。任务来自用户明确指令及lab4-team-plan：补齐功能三、迁移必要Trap、完成联合验收与Challenge 1，保留A/B实现和教师测试；不push。

依赖：context为ra/sp/s0-s11共112字节；PCB.pgdir为物理根页表地址；idle不在proc_list，作为兜底；B已构造forkret/context及288字节帧。单核非抢占基础调度，不迁移Lab3自动关机。

交付：cpu_idle、schedule、proc_run、switch_to，必要Trap；补充测试、运行评分/GDB证据、个人章节和交接。

行为：从当前后继循环选runnable，idle从表头开始，无候选回idle；维护runs并清need_resched。临界区保持进入时SIE；proc_run对同一目标无操作，否则更新current、写Sv39 satp并sfence.vma、保存/恢复context。首次ret进入forkret链。失败或边界不得伪造运行输出，不删除do_exit占位panic或修改教师断言。

验证：普通启动到init实际输出；教师30/30；独立协作线程多次切换、私有栈、保存寄存器、扫描回绕、自选/兜底与SIE；实际ELF定位GDB首次链。扩展解释关中断及Sv32/39/48、查找/分配边界；PID回绕仅保证不撞活PID，不声称永久不复用。

本文为AI整理的实现规格，不是逐字聊天导出；实际用户指令保留于会话。

## 实际迭代

先按分工只增加function3及必要Trap，不覆盖B创建函数或A映射实现。首次构建发现旧obj依赖不存在的Best-Fit头，clean后正常。真实运行首次进入init，保留do_exit占位panic。

初次教师评分0/30但普通内核和独立线程测试通过；记录评分GDB输出发现教师PASS已经真实产生，却经semihosting remote console送到GDB，被评分丢弃。仅在外部PATH适配器保留并送回该原始输出后，原教师三项30/30；未改断言。另开GDB会话以当前ELF断点和断言证明首次链，避免用评分中的remote closed诊断冒充GDB流程成功。

真实用户没有中途追加技术指令，具体算法和工具修正属于AI执行判断。仍需要本人理解代码及队友环境联合复测。
