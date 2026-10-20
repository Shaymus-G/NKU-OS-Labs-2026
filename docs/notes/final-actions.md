# 距离最终交付还需谁做什么

2026-10-20，用户要求完成已知选做及扩展后的最新状态。三人学号已确认：A 国峻赫2412082、B 陈铭宇2412559、C 尚文轩2412456。

2026-10-21报告修订：正式正文改由 `docs/report/` 下的编辑稿生成，`docs/parts/` 保留个人原始材料。C负责统一结构与汇总，A/B核对本人章节。见 [编辑入口](../report/README.md)、[C交付复核](c-delivery-review-2026-10-21.md) 和 [群内对接稿](team-handoff-2026-10-21.md)。

## 已完成的技术交付

主体：A物理页初始化、页表遍历与TLB、分页前后与直接命中；B First-Fit分析和Best-Fit；C Buddy、独立测试和总报告。原联合版A/B/C均已验证，教师20/20只评分Page Init与Best-Fit。

扩展：C已实现并测试教学SLUB、完善动态FDT内存范围验证、制作实时GDB自动查询与单步演示及离线回放。SLUB两种管理器正常和预期异常通过；FDT解析2230项，64/96/128/256MiB矩阵通过；Sv39离线17项和真实GDB运行通过；新代码默认评分20/20。见 [SLUB](../parts/slub.md)、[DTB](../parts/dtb-challenge.md)、[演示](../parts/address-demo.md)。

## 还需执行的交付工作

| 负责人 | 仍需做什么 | 完成判据 |
|---|---|---|
| A | 根据自己的真实AI会话补齐初始化交互/验证/问题记录 | docs/prompts/a-page-init.md 不再保留待填写占位；不编造不存在的对话 |
| A | 复核C新增DTB边界与演示的技术表述 | 确认对单bank/启动映射的限制理解一致；区分实时PTE读取与宿主TLB证据 |
| B | 在其QEMU4.1.1环境清理重建并复测新增SLUB与DTB | 两种PMM的扩展自检及普通make grade通过；新日志使用b-extension前缀，保留旧日志 |
| C/用户 | 核对原实验手册是否还有当前资料未列出的具体要求 | 对原PDF的任务、思考题、Challenge逐项对照；当前尚未取得该PDF |
| A/B/C | 通读报告、检查分工与证据，准备各自即时答辩 | 每人能解释前置/后置/失败、Buddy取整、SLUB共享页、Sv39大页和证据边界 |
| 用户/组长 | 同步远程更新、审阅本地提交后push，并按课程入口提交 | 不强推；组员可取同一版本；在实际课程截止前完成正式提交 |
| 全组/组长 | 如争取课件贡献加分，向教师提交演示候选 | 提供脚本、回放、日志和说明；接收与加分由教师判断，本会话未发送 |

本轮新增扩展在C环境完成；A/B之前的联合回归不能冒充已经复测这批新代码。本地实现和验证已完成不代表人类复核、正式提交和教师接收已自动完成。

## 交给队友的复测命令

在仓库根目录、各自已配置工具链环境中：

```bash
make clean
make DEFS=-DLAB2_EXTENSIONS
make qemu DEFS=-DLAB2_EXTENSIONS
# 退出，切换Buddy：
make clean
make DEFS='-DLAB2_EXTENSIONS -DPMM_BUDDY'
make qemu DEFS='-DLAB2_EXTENSIONS -DPMM_BUDDY'
# 退出，恢复默认并评分：
make clean
make
make grade
```

A/B原环境用普通评分。C本机仍用 `make grade QEMU='python3 tools/qemu-grade-console.py'`。内存矩阵和非法释放可用 `tools/check-extensions.py`，其中自动评分默认使用C控制台转接，但对A/B手动上述普通评分无要求。不要把预期panic与正常启动混在同一次测试。
