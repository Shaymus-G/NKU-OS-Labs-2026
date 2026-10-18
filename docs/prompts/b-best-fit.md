# B：Best-Fit 实现约束记录

首次记录：2026-10-17；补充交接状态：2026-10-18。以下原始指令和约束保留为历史记录，后续变化另行追加，不将事后整理冒充生成前的提示词。

用户本轮实际要求：

> 你现在来完成成员B的工作。现在先集中推进项目的实际工作，不用和我讲解实验的原理，和我汇报一下实验进度就行。等小组的所有工作完成后最后再来准备lab2的答辩。如果你觉得一次性完成成员B的所有工作不方便，也可以一次只完成一部分。

以下是 Codex 在写代码前根据项目上下文和分工整理并采用的实现约束，不是学生另行发送的逐字提示词，也没有调用第二个模型：

1. 以当前教师工程为准，阅读 pmm.h、memlayout.h、list.h、default_pmm.c、best_fit_pmm.c 以及教师评分入口。
2. 只完成 B 的 Best-Fit 模块和必要接入；不实现 A 的 page_init() 或 C 的 Buddy。
3. 保持 struct Page 和 struct pmm_manager 接口不变；内部状态使用 static free_area_t，并提供教师检查函数依赖的 free_list、nr_free 宏。
4. 空闲块按地址递增排列，property 仅在空闲块头表示长度。搜索所有足够大的块，选择最小块；相同大小时选择较低地址，精确匹配时可以提前停止。
5. 分配恰好 n 页，处理剩余块、标志和计数；失败时不改变状态。n == 0 沿用教师接口的 assert 约定。
6. 注册和释放区间时保持有序链表，并合并前后相邻区间。注册仅接受尚处于保留状态的页，释放仅接受调用者合法拥有的已分配页。
7. 不修改 basic_check()、best_fit_check() 和 tools 中的教师测试、评分与注入脚本；注册 check 回调为原 best_fit_check()。
8. 将 init_pmm_manager() 的默认选择改为 best_fit_pmm_manager，其他 pmm.c 内容保持原样。
9. 学号未知，暂保留 YOUR CODE，最终交付前由负责人填写真实学号。
10. 本轮先交付实现，交由用户单独编译 Best-Fit 对象文件。记录未验证项目，不声称内核、评分或 Buddy 已通过。

后续实际修正及测试交互应继续追加记录，不倒填未实际使用的提示词或不存在的错误。

## 后续实际指令与验证（2026-10-18）

### 迁入 A 的成果

用户实际要求：

> 现在请你完成你之前说的工作的第二部分（迁入本地 Best-Fit 实现，保留教师测试和 A 的 `page_init()`  ）。

处理结果：只迁入 Best-Fit 模块实现，保留新仓库教师检查函数的原始字节和换行，先保持 First-Fit。用户普通 make 成功后，再只将 pmm.c 中默认 manager 切为 best_fit_pmm_manager；A 的初始化函数未覆盖。用户随后提供了 Best-Fit 已启用、check_alloc_page() succeeded! 的运行截图。

### 评分脚本问题

用户在评分报错后实际要求：

> 测试好像报错了。你看看要怎么改。你直接修改文件并进行测试。测试失败则撤销修改，测试通过则保留修改。

处理结果：sh -n 重现语法错误，确认 tools/grade.sh 使用 CRLF。仅将脚本转换为 LF，新增 .gitattributes 固定 shell 脚本的 LF 换行；不改变评分条件。语法检查和实际评分均通过，保留修改。[修复日志](../logs/b-grade-fixed.log) 记录 Total Score: 20/20。

原始约束第 7 项描述的是生成算法时的范围。后续脚本格式修复经过用户明确要求，仅处理换行；不能将其写成从未修改过 grade.sh 文件，也不能将其称为修改了算法评分逻辑。

### 补充行为测试

用户实际要求：

> 现在请你完成你之前说的工作的第五部分（补充行为测试，验证最佳匹配、拆分、前后合并、碎片失败及状态恢复）。

实现前采用的测试约束和实际结果见 [测试生成记录](b-best-fit-tests.md)、[测试说明](../parts/b-best-fit-tests.md)。新增独立 best_fit_test.c，并将 check 回调调整为 best_fit_check_all()，先调用教师原 best_fit_check()，再调用新增测试。原始约束第 7 项中“直接注册原检查函数”因此被后续包装入口取代，教师检查函数内容仍保持原样。

实际完成 clean build、QEMU 断言检查及教师评分回归；新增用例全部 PASS，评分 20/20。用户再次手工复现并保存四张截图，重复 manual 日志已核对后删除。

## 当前交接材料

- [First-Fit 分析、Best-Fit 规格、修复及已验证结果](../parts/b-first-best-fit.md)
- [行为测试说明](../parts/b-best-fit-tests.md)
- [当前编译日志](../logs/b-best-fit-behavior-build.log)
- [当前 QEMU 输出与源码 SHA256](../logs/b-best-fit-behavior.log)
- [当前教师评分](../logs/b-grade-behavior.log)
- [进度、复现命令及交接事项](../notes/b-progress.md)

以上保留了真实用户指令和 Codex 执行前采用的约束来源；不是一份事后伪造的独立学生实现 prompt。C 汇总报告时应如实描述 AI 辅助方式。

## 学号确认与提交准备（2026-10-18）

用户明确提供学号 2412559，并要求填写、推送及补充 commit ID。已仅将 B 的 Best-Fit 注释改为 `//lab2 function2-2 2412559`，A 的 `//lab2 function1 YOUR CODE` 保留由 A 确认。上文“学号未知”是生成时的历史状态。本次注释变更不改变算法或教师测试；原 QEMU 日志的源码 SHA256 对应填写学号前的运行版本。

填写后已再次执行教师评分，20/20，通过结果及新的源码 SHA256 保存于 [最终评分日志](../logs/b-final-grade.log)。代码、测试及证据提交为 [f8ed38a8d6fb02e1093995eeb99d0744101eabe1](https://github.com/Shaymus-G/NKU-OS-Labs-2026/commit/f8ed38a8d6fb02e1093995eeb99d0744101eabe1)，交接文档由后续提交补齐。用户已明确授权本次 Git 提交和推送。
