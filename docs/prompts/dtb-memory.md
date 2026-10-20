# DTB 物理范围扩展：实际实施前规格

来源：用户要求实现全部已知 Challenge，并补充助教四段式讲解。本规格在这一扩展的编码前写入，随后由本会话按此实现与验证。

## Prompt

完善现有 kern/driver/dtb.c 的物理内存信息读取，新增独立可测试的 blob 解析接口。处理合法与损坏设备树，验证动态内存配置，不写探测物理地址的破坏性模式。实际修改工程文件，保持 A 的页初始化、公共 PMM 接口、教师测试、工具链包装器不变。输出解析错误类型、真实 DTB 范围、管理上限裁剪和测试证据。

## Rely

最小依赖：OpenSBI 传入 boot_dtb、FDT header/token 格式、根节点 #address-cells/#size-cells、直接子 memory 节点 reg，已有 get_memory_base/get_memory_size，以及本框架固定 DRAM_BASE 和 128 MiB 管理上限。提供 buffer 的实际可读字节数，所有偏移都受此长度约束。不得无界 strlen、无界 token 遍历或依赖 8 字节指针对齐读取。

## Guarantee

交付 dtb_parse_memory(blob,length,&base,&size)，成功返回0，失败返回明确负值并不修改输出。保留 dtb_init/get_memory_base/get_memory_size 的既有调用方式；新增 Python 构造 FDT fixture 的独立测试和 QEMU 内存矩阵。

## Specification

前置：blob 指向 length 个可读字节，输出指针有效。成功必须经过合法 header、结构块、字符串块、token 层次和结束标记校验，给出一个非零且无溢出的连续物理内存范围。支持 1/2 cell；多 bank 或更宽 cell 明确拒绝，不能悄悄返回总内存不完整的第一段。失败时输出保持原值；坏 magic、截断、越界属性名、未终止字符串、嵌套错误、缺少 memory、长度不匹配等分别验证。dtb_init 不成功时保留零范围，既有 page_init 会停止启动。64/96/128 MiB 应使用对应真实末端；256 MiB 应报告256而管理裁剪至框架128，不能伪称已支持256 MiB 分配。
