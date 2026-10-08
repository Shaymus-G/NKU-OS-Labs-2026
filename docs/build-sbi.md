# Lab 1：构建流程、QEMU 加载与 SBI 输出机制分析

实验者：陈铭宇  
对应分工：成员 C——Makefile 构建流程、SBI 输出机制、报告整合

## 1. 本部分目标与实验范围

本部分围绕 Lab 1 中“代码怎样生成内核，以及输出怎样到达终端”展开，主要完成以下工作：

1. 分析源码从 `.c/.S` 文件到目标文件、`bin/kernel` 和 `bin/ucore.img` 的构建过程；
2. 核对 `make qemu`、`make debug`、`make gdb` 的实际命令及关键参数；
3. 结合 Makefile 说明 QEMU 如何加载 OpenSBI 和内核镜像，并区分“镜像已放入内存”与“CPU 已开始执行内核”两个事件；
4. 从 `kern_init` 中的 `cprintf` 出发，沿源码整理字符输出到 SBI 的完整调用链；
5. 结合 GDB 在 `ecall` 执行前的现场，验证 SBI 服务号和参数寄存器；
6. 说明上述实现与操作系统课程中的构建、装载、特权级、系统调用式接口、模块分层等概念的联系，并指出 Lab 1 尚未覆盖的功能。

构建命令的原始文本保存在 [`logs/build-commands.txt`](logs/build-commands.txt)。本文中的运行截图和源码截图均来自本组实际实验过程。

---

## 2. 实验环境与构建结果

本成员在 Windows + WSL2 环境下完成实验，WSL 中使用 Ubuntu 24.04.4 LTS。RISC-V 交叉编译器使用 SiFive GCC-Metal 10.2.0，GDB 使用 SiFive GDB-Metal 10.1，QEMU 使用 4.1.1。运行 `make qemu` 时可见 OpenSBI v0.4。

进入实验目录后执行：

```bash
make clean
make
```

实际构建输出依次编译：

```text
kern/init/entry.S
kern/init/init.c
kern/libs/stdio.c
kern/driver/console.c
libs/printfmt.c
libs/readline.c
libs/sbi.c
libs/string.c
```

随后链接生成 `bin/kernel`，最后通过 `objcopy` 生成 `bin/ucore.img`：

```text
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

![Lab1 编译成功并生成 kernel 与 ucore.img](images/lab1_build_success.png)

图 1：实际编译结果。`bin/kernel` 和 `bin/ucore.img` 均成功生成。

执行：

```bash
make qemu
```

可见 OpenSBI 启动信息以及：

```text
(THU.CST) os is loading ...
```

![QEMU 与 OpenSBI 正常启动](images/lab1_qemu_boot.png)

图 2：QEMU 运行结果。OpenSBI 启动后，内核成功打印启动信息。

---

## 3. Makefile 与 `tools/function.mk` 的构建机制

### 3.1 交叉编译工具链

Makefile 首先定义 RISC-V 工具链前缀：

```make
GCCPREFIX := riscv64-unknown-elf-
```

在此基础上得到：

```make
GDB     := $(GCCPREFIX)gdb
CC      := $(GCCPREFIX)gcc
LD      := $(GCCPREFIX)ld
OBJCOPY := $(GCCPREFIX)objcopy
OBJDUMP := $(GCCPREFIX)objdump
```

因此本实验不是用宿主机的普通 `gcc` 直接生成 x86-64 程序，而是在 x86-64 的 WSL 环境中运行 RISC-V 交叉编译工具，生成可供 RISC-V 64 位目标平台执行的代码。

Makefile 中主要编译参数为：

```make
CFLAGS  := -mcmodel=medany -std=gnu99 -Wno-unused -Werror
CFLAGS  += -fno-builtin -Wall -O2 -nostdinc $(DEFS)
CFLAGS  += -fno-stack-protector -ffunction-sections -fdata-sections
CFLAGS  += -g
```

其中与本实验关系较直接的选项包括：

- `-mcmodel=medany`：采用适合当前 RISC-V 内核链接布局的代码模型；
- `-nostdinc`：不使用宿主系统标准头文件；
- `-fno-builtin`：避免把源码中的函数调用自动替换为宿主环境的内建实现；
- `-fno-stack-protector`：关闭宿主编译环境自动加入的栈保护运行时依赖；
- `-ffunction-sections -fdata-sections`：将函数和数据放入独立 section，配合链接阶段的 `--gc-sections`；
- `-g`：保留调试信息，以便 GDB 使用 `bin/kernel` 中的符号和源码行信息。

链接参数为：

```make
LDFLAGS := -m elf64lriscv
LDFLAGS += -nostdlib --gc-sections
```

这说明最终内核按照 RISC-V 64 位 ELF 进行链接，并且不依赖宿主操作系统的标准运行库。

### 3.2 自动发现源文件与生成目标文件

Makefile 引入：

```make
include tools/function.mk
```

`tools/function.mk` 中定义了若干通用函数，使 Makefile 不需要为每个源文件手写一条规则。

其中：

```make
listf = ...
```

用于从指定目录中过滤所需类型的文件；

```make
toobj = ...
```

把源文件路径映射到 `obj/` 下的目标文件路径；

```make
todep = ...
```

把 `.o` 文件路径转换为相应的 `.d` 依赖文件路径。

例如：

```text
kern/init/init.c
```

会对应到：

```text
obj/kern/init/init.o
```

实际编译规则由 `cc_template` 生成：

```make
$(V)$(2) -I$(dir $(1)) $(3) -c $< -o $@
```

同时还会用 `-MM` 生成 `.d` 文件。这样，当相关头文件发生变化时，Make 能根据依赖信息判断哪些目标文件需要重新编译。

### 3.3 `kernel` 与 `libs` 对象集合

Makefile 中先收集 `libs/` 下的源文件：

```make
LIBDIR += libs
$(call add_files_cc,$(call listf_cc,$(LIBDIR)),libs,)
```

再收集内核目录：

```make
KSRCDIR += kern/init \
           kern/debug \
           kern/libs \
           kern/driver \
           kern/trap \
           kern/mm

$(call add_files_cc,$(call listf_cc,$(KSRCDIR)),kernel,$(KCFLAGS))
```

`function.mk` 通过 packet 机制把不同目录产生的目标文件分别组织起来；最终：

```make
KOBJS = $(call read_packet,kernel libs)
```

把 `kernel` 和 `libs` 两组目标文件合并成链接内核所需的 `KOBJS`。

本次实际参与链接的目标文件包括：

```text
obj/kern/init/entry.o
obj/kern/init/init.o
obj/kern/libs/stdio.o
obj/kern/driver/console.o
obj/libs/printfmt.o
obj/libs/readline.o
obj/libs/sbi.o
obj/libs/string.o
```

### 3.4 链接生成 `bin/kernel`

Makefile 定义：

```make
kernel = $(call totarget,kernel)
```

`totarget` 会在目标名前加入 `bin/`，因此这里对应：

```text
bin/kernel
```

它依赖 `tools/kernel.ld` 和全部 `KOBJS`：

```make
$(kernel): tools/kernel.ld
$(kernel): $(KOBJS)
```

实际链接命令为：

```bash
riscv64-unknown-elf-ld \
  -m elf64lriscv \
  -nostdlib \
  --gc-sections \
  -T tools/kernel.ld \
  -o bin/kernel \
  $(KOBJS)
```

`tools/kernel.ld` 中明确：

```ld
ENTRY(kern_entry)
BASE_ADDRESS = 0x80200000;
```

因此 ELF 入口符号是 `kern_entry`，并按 `0x80200000` 作为内核链接基址组织各段。

链接完成后还生成两份分析文件：

```make
$(OBJDUMP) -S $@ > $(call asmfile,kernel)
$(OBJDUMP) -t $@ | ... > $(call symfile,kernel)
```

即：

```text
obj/kernel.asm
obj/kernel.sym
```

前者用于查看源码与反汇编对应关系，后者用于查看符号信息。

### 3.5 从 ELF 转为裸二进制镜像

Makefile 定义：

```make
UCOREIMG := $(call totarget,ucore.img)
$(UCOREIMG): $(kernel)
    $(OBJCOPY) $(kernel) --strip-all -O binary $@
```

因此：

```text
bin/kernel
```

经：

```bash
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

转换为：

```text
bin/ucore.img
```

二者用途不同：

- `bin/kernel` 是 ELF 文件，包含符号、段布局和调试信息，因此 GDB 使用它；
- `bin/ucore.img` 是用于直接放入目标内存的裸二进制镜像，因此 QEMU 的 loader 使用它。

整个构建链可以概括为：

```text
.c / .S
   │
   ▼
riscv64-unknown-elf-gcc
   │
   ▼
obj/.../*.o  +  .d
   │
   ▼
riscv64-unknown-elf-ld + tools/kernel.ld
   │
   ▼
bin/kernel (ELF)
   │
   ├── objdump -S → obj/kernel.asm
   ├── objdump -t → obj/kernel.sym
   │
   ▼
objcopy --strip-all -O binary
   │
   ▼
bin/ucore.img
```

---

## 4. `make qemu`、`make debug` 与 `make gdb`

实际命令通过 `make -n` 获取，并保存在 [`logs/build-commands.txt`](logs/build-commands.txt)。

### 4.1 `make qemu`

实际命令为：

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -device loader,file=bin/ucore.img,addr=0x80200000
```

![make qemu 的实际命令](images/lab1_make_qemu_command.png)

图 3：`make -n qemu` 展开的 QEMU 启动命令。

关键参数：

- `-machine virt`：选择 QEMU 的 RISC-V `virt` 虚拟平台；
- `-nographic`：不打开图形窗口，将串口/控制台输出放到当前终端；
- `-bios default`：使用 QEMU 提供的默认 RISC-V 固件，本次实际显示为 OpenSBI v0.4；
- `-device loader,file=bin/ucore.img,addr=0x80200000`：在虚拟机开始正常执行之前，把 `bin/ucore.img` 放到客户机物理地址 `0x80200000`。

### 4.2 `make debug`

实际命令为：

```bash
qemu-system-riscv64 \
    -machine virt \
    -nographic \
    -bios default \
    -device loader,file=bin/ucore.img,addr=0x80200000 \
    -s -S
```

![make debug 的实际命令](images/lab1_make_debug_command.png)

图 4：`make -n debug` 展开的命令。

与普通运行相比，多出的参数为：

- `-s`：开启 GDB 远程调试服务，等价于在默认 TCP 1234 端口监听；
- `-S`：QEMU 启动后暂停虚拟 CPU，不立即执行客户机指令，等待 GDB 连接后再继续。

### 4.3 `make gdb`

实际命令为：

```bash
riscv64-unknown-elf-gdb \
    -ex 'file bin/kernel' \
    -ex 'set arch riscv:rv64' \
    -ex 'target remote localhost:1234'
```

![make gdb 的实际命令](images/lab1_make_gdb_command.png)

图 5：`make -n gdb` 展开的 GDB 命令。

其中：

- `file bin/kernel`：加载 ELF 文件及其中的符号、调试信息；
- `set arch riscv:rv64`：指定调试目标架构为 RISC-V 64 位；
- `target remote localhost:1234`：连接 `make debug` 启动的 QEMU GDB Server。

因此，`make debug` 负责创建“暂停并等待调试”的虚拟机，`make gdb` 负责连接该虚拟机。

---

## 5. QEMU、OpenSBI 与内核镜像的实际加载关系

本次实验需要特别区分两个概念：

1. **内核镜像已经被放入物理内存；**
2. **CPU 已经开始从内核入口执行。**

根据本组 Makefile，内核镜像的放置由 QEMU 命令中的：

```text
-device loader,file=bin/ucore.img,addr=0x80200000
```

直接指定。因此，在 QEMU 按照该配置建立虚拟机时，`bin/ucore.img` 已被映射/装入 `0x80200000` 对应的客户机物理内存区域。

同时：

```text
-bios default
```

使 QEMU 使用默认 OpenSBI 固件。普通运行截图中可见：

```text
Firmware Base : 0x80000000
```

这与本组启动调试中观察到 CPU 从复位代码转入 `0x80000000` 的 OpenSBI 入口相对应。

之后，OpenSBI 完成其启动工作并把控制权交给内核，本组 GDB 启动调试进一步验证 CPU 最终到达：

```text
0x80200000 <kern_entry>
```

因此更准确的启动关系是：

```text
QEMU 创建 RISC-V virt 平台
      │
      ├── 准备默认 OpenSBI 固件
      └── 将 ucore.img 放入 0x80200000
                │
                ▼
CPU 从复位代码开始执行
      │
      ▼
OpenSBI（0x80000000）
      │
      ▼
将 CPU 控制权交给内核
      │
      ▼
kern_entry（0x80200000）
```

因此，不能把“GDB 在 `0x80200000` 命中内核断点”表述成“此时才把内核镜像加载到内存”。该断点直接证明的是 **CPU 此时开始执行内核入口附近的代码**；而镜像如何进入内存，需要结合 QEMU 的 loader 参数解释。

---

## 6. 从 `kern_init` 到终端的输出调用链

### 6.1 `kern_init` 发起输出

`kern/init/init.c` 中：

```c
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);
    while (1)
        ;
}
```

![kern_init 中的输出语句](images/lab1_kern_init_output.png)

图 6：`kern_init` 清零指定区域后调用 `cprintf` 输出启动信息。

这里的输出入口是：

```c
cprintf("%s\n\n", message);
```

因此后续从 `cprintf` 向下追踪即可得到完整输出路径。

### 6.2 `cprintf → vcprintf → vprintfmt`

`kern/libs/stdio.c` 中：

```c
int cprintf(const char *fmt, ...) {
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}
```

`cprintf` 把可变参数整理成 `va_list` 后调用：

```c
vcprintf(fmt, ap);
```

`vcprintf` 的实现为：

```c
int vcprintf(const char *fmt, va_list ap) {
    int cnt = 0;
    vprintfmt((void *)cputch, &cnt, fmt, ap);
    return cnt;
}
```

它把 `cputch` 作为回调函数传给 `vprintfmt`。`libs/printfmt.c` 中的 `vprintfmt` 负责解析 `%s` 等格式控制符；每得到一个最终字符，就调用传入的 `putch`。

![stdio 层中的 cputch、vcprintf 与 cprintf](images/lab1_stdio_printf_chain.png)

图 7：格式化输出上层调用关系。

### 6.3 `cputch → cons_putc`

`kern/libs/stdio.c` 中：

```c
static void cputch(int c, int *cnt) {
    cons_putc(c);
    (*cnt)++;
}
```

`cputch` 一方面把字符继续交给 `cons_putc`，另一方面通过 `(*cnt)++` 统计已经输出的字符数。这个计数最后由 `vcprintf/cprintf` 返回。

`kern/driver/console.c` 中：

```c
void cons_putc(int c) {
    sbi_console_putchar((unsigned char)c);
}
```

![console 层的 cons_putc](images/lab1_console_cons_putc.png)

图 8：console 层将单字符输出继续交给 SBI。

### 6.4 `sbi_console_putchar → sbi_call → ecall`

`libs/sbi.c` 定义：

```c
uint64_t SBI_CONSOLE_PUTCHAR = 1;
```

以及：

```c
void sbi_console_putchar(unsigned char ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, ch, 0, 0);
}
```

通用 SBI 调用函数中的内联汇编为：

```c
__asm__ volatile (
    "mv x17, %[sbi_type]\n"
    "mv x10, %[arg0]\n"
    "mv x11, %[arg1]\n"
    "mv x12, %[arg2]\n"
    "ecall\n"
    "mv %[ret_val], x10"
    : [ret_val] "=r" (ret_val)
    : [sbi_type] "r" (sbi_type),
      [arg0] "r" (arg0),
      [arg1] "r" (arg1),
      [arg2] "r" (arg2)
    : "memory"
);
```

![SBI 调用源码](images/lab1_sbi_source.png)

图 9：SBI 服务号、参数寄存器和 `ecall` 的源码实现。

RISC-V ABI 中：

```text
x10 = a0
x11 = a1
x12 = a2
x17 = a7
```

因此，对于一个字符的控制台输出，执行 `ecall` 前应形成：

```text
a7 = SBI_CONSOLE_PUTCHAR = 1
a0 = ch
a1 = 0
a2 = 0
```

执行 `ecall` 后，请求进入更高特权级的 SBI 环境，由 OpenSBI 处理控制台输出；返回时，源码又把 `x10/a0` 中的返回值移到 C 变量 `ret_val` 中。

因此，本实验输出调用链可以概括为：

```text
kern_init
   │
   ▼
cprintf
   │
   ▼
vcprintf
   │
   ▼
vprintfmt
   │
   ▼
cputch
   │
   ▼
cons_putc
   │
   ▼
sbi_console_putchar
   │
   ▼
sbi_call
   │
   ▼
ecall
   │
   ▼
OpenSBI
   │
   ▼
QEMU 虚拟控制台
   │
   ▼
宿主机终端
```

---

## 7. GDB 验证 `ecall` 前的 SBI 参数

### 7.1 定位实际 `ecall`

由于本次编译使用 `-O2`，GDB 中：

```gdb
break sbi_call
```

未能直接找到独立的 `sbi_call` 函数符号；`info functions sbi` 只列出了：

```text
sbi_console_putchar
```

这说明当前优化结果中，`sbi_call` 的代码已经被编译器内联到 `sbi_console_putchar` 的实际机器代码中。因此实验没有修改源码或关闭优化，而是按照实际反汇编继续定位。

`disassemble /r sbi_console_putchar` 显示：

```text
0x8020048a <+10>: mv a7,a4
0x8020048c <+12>: mv a0,a0
0x8020048e <+14>: mv a1,a5
0x80200490 <+16>: mv a2,a5
0x80200492 <+18>: ecall
```

于是把断点设置在：

```gdb
break *0x80200492
```

### 7.2 `ecall` 执行前寄存器

断点命中后，GDB 显示：

```text
sbi_call(arg2=0, arg1=0, arg0=40, sbi_type=1)
```

并通过：

```gdb
info registers pc a0 a1 a2 a7
x/i $pc
```

得到：

```text
pc = 0x80200492
a0 = 0x28 = 40
a1 = 0
a2 = 0
a7 = 1
=> 0x80200492 <sbi_console_putchar+18>: ecall
```

![ecall 执行前的 SBI 参数寄存器](images/lab1_sbi_ecall_registers.png)

图 10：`ecall` 执行前的实际寄存器和当前指令。

此时 `a0 = 0x28`，十进制为 40，对应启动字符串的第一个字符 `'('`；`a7 = 1` 与源码中的 `SBI_CONSOLE_PUTCHAR = 1` 一致；`a1` 和 `a2` 均为 0。这一现场直接验证了本次字符输出调用在进入 `ecall` 前的参数传递方式。

需要注意，在刚进入 `sbi_console_putchar` 时观察寄存器，`a7/a1/a2` 还可能保留旧值；因此本次最终证据选在参数搬运完成、`ecall` 尚未执行的地址 `0x80200492`，而不是函数入口。

---

## 8. `cprintf` 返回值 30 的解释

本组启动调试已经观察到 `cprintf` 正常返回，返回值为：

```text
30
```

这个结果可以直接由当前源码的字符计数逻辑解释。

`kern_init` 中：

```c
const char *message = "(THU.CST) os is loading ...\n";
cprintf("%s\n\n", message);
```

其中 `message` 本身共有 28 个字符，最后一个字符已经是换行 `\n`。格式字符串 `%s\n\n` 在输出完整 `message` 后，又额外输出两个换行字符，所以总输出字符数为：

```text
28 + 2 = 30
```

另一方面，`cputch` 每实际处理一个输出字符都会执行：

```c
(*cnt)++;
```

`vcprintf` 返回该 `cnt`，`cprintf` 再把这个返回值交回 `kern_init`。因此 GDB 中观察到的返回值 30 与源码和实际输出长度完全一致。

这也说明这里的 `cprintf` 返回值表示本次格式化输出过程中计数的字符数量，而不是 SBI 单次字符调用的返回值。

---

## 9. 与操作系统理论知识的联系

### 9.1 裸机/内核代码不能直接依赖宿主操作系统运行时

普通 Linux 用户程序可以直接使用系统提供的标准库和系统调用接口；而本实验正在构建操作系统内核本身，不能把宿主系统的 `printf`、运行库或系统调用当作目标环境的一部分。

因此本实验自行提供：

```text
cprintf → vprintfmt → console → SBI
```

这一整套最小输出路径，并在编译、链接阶段使用 `-nostdinc`、`-nostdlib` 等选项减少对宿主运行环境的依赖。

### 9.2 构建、链接与装载是三个不同阶段

Lab 1 清楚体现了三个容易混淆的阶段：

1. **编译/链接**：在 WSL 主机上把源文件变成按照 `0x80200000` 布局的 RISC-V ELF；
2. **镜像转换与装载**：`objcopy` 生成 `ucore.img`，QEMU loader 将它放入客户机内存；
3. **开始执行**：CPU 经过复位代码和 OpenSBI 后，PC 最终到达 `kern_entry`。

因此“文件在磁盘上生成”“镜像已经位于目标内存”“CPU 正在执行内核”不是同一个事件。

### 9.3 ELF、裸二进制与链接脚本

`bin/kernel` 保留 ELF 结构和调试符号，适合 GDB、`objdump` 等工具分析；`bin/ucore.img` 则更接近目标内存中的连续机器代码/数据映像，供 loader 使用。

`tools/kernel.ld` 明确规定 `ENTRY(kern_entry)` 和 `BASE_ADDRESS = 0x80200000`，说明操作系统内核不能完全依赖通用用户程序的默认链接布局，而需要主动控制自身的入口和各段位置。

### 9.4 特权级与 SBI 接口

实验中的内核通过 `ecall` 请求 OpenSBI 服务，体现了不同特权级之间不能像普通 C 函数那样无条件直接调用的特点。

对当前字符输出而言，内核先按约定把服务号和参数放入 `a7/a0/a1/a2`，再执行 `ecall`。这种“服务号 + 参数寄存器 + 特殊陷入指令”的结构，与之后用户态通过系统调用进入内核的思想具有相似性：调用者并不直接执行高特权级实现，而是通过受控接口跨越特权边界。

### 9.5 分层与模块化

从：

```text
cprintf
→ vprintfmt
→ cputch
→ cons_putc
→ sbi_console_putchar
→ ecall
```

可以看到明显的模块分层：

- `cprintf/vprintfmt` 负责格式化；
- `cputch/cons_putc` 负责抽象字符输出；
- `sbi_console_putchar/sbi_call` 负责把需求转换为 SBI 调用；
- OpenSBI 再负责更底层的机器态服务。

上层代码不需要知道 QEMU 控制台的底层实现细节，这种接口隔离是操作系统中常见的模块化设计方法。

### 9.6 Lab 1 尚未覆盖的内容

本实验完成的是一个极小的可运行内核启动路径。虽然源码目录中可能已经出现后续实验会使用的头文件或目录，但本次不能据此声称已经实现相应功能。

Lab 1 尚未真正完成的典型操作系统机制包括：

- 完整的异常与中断管理；
- 时钟中断驱动的调度；
- 物理内存分配；
- 页表与虚拟内存；
- 用户态进程；
- 用户态到内核态的系统调用接口；
- 进程/线程调度；
- 文件系统；
- 同步与互斥；
- 多核调度。

因此，本实验的重点应表述为：建立并理解“**构建内核 → 将镜像放入模拟机器 → 经固件进入内核 → 使用 SBI 完成最基本输出**”这一最小闭环。

---

## 10. 本部分结论

通过源码、Makefile、实际命令、运行结果和 GDB 调试，可以得到以下结论：

1. Lab 1 通过 `tools/function.mk` 自动收集源码、生成目标文件和依赖文件；
2. `riscv64-unknown-elf-ld` 结合 `tools/kernel.ld` 生成带符号信息的 `bin/kernel`；
3. `objcopy` 再将 ELF 转换为 `bin/ucore.img`；
4. `make qemu` 使用 QEMU RISC-V `virt` 平台、默认 OpenSBI，并通过 loader 将 `ucore.img` 放入 `0x80200000`；
5. “内核镜像已经位于内存”与“CPU 已经执行到 `kern_entry`”是不同阶段；本组分别通过 Makefile 参数和 GDB 启动调试进行说明；
6. 内核启动文字实际沿 `kern_init → cprintf → vcprintf → vprintfmt → cputch → cons_putc → sbi_console_putchar → sbi_call → ecall → OpenSBI` 输出；
7. GDB 在 `ecall` 前实际观测到 `a7=1、a0=40、a1=0、a2=0`，与 `SBI_CONSOLE_PUTCHAR` 的源码调用一致；
8. `cprintf` 返回值 30 与实际输出的 30 个字符及 `cputch` 的字符计数逻辑一致。

至此，本成员负责的“构建、加载与 SBI 输出机制”部分已经形成完整证据链，可与小组的入口/链接分析和启动调试草稿共同整合进最终 Lab 1 报告。
