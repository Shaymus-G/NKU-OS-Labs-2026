# Build the default hello image and start it with `make debug` first.
set pagination off
set confirm off
set remotetimeout unlimited
file bin/kernel
add-symbol-file obj/__user_hello.out
target remote localhost:1234

hbreak *_start
continue
printf "first user instruction: pc=%#lx sp=%#lx\n", $pc, $sp
# QEMU 4.1.1 does not expose every CSR through the remote register API.
monitor info registers
x/i $pc
delete breakpoints

hbreak *sys_putc
continue
delete breakpoints
set $steps = 0
while (*(unsigned int *)$pc != 0x00000073 && $steps < 16)
    si
    set $steps = $steps + 1
end
if (*(unsigned int *)$pc != 0x00000073)
    quit 1
end
set $user_ecall_pc = $pc
set $user_ecall_nr = $a0
printf "before user ecall: pc=%#lx syscall=%ld\n", $user_ecall_pc, $user_ecall_nr
si
printf "trap entry: pc=%#lx sp=%#lx\n", $pc, $sp
monitor info registers
delete breakpoints

hbreak *syscall
continue
printf "syscall dispatch: pc=%#lx sp=%#lx tf.epc=%#lx saved_sp=%#lx syscall=%ld\n", $pc, $sp, current->tf->epc, current->tf->gpr.sp, current->tf->gpr.a0
if (current->tf->cause != 8 || (current->tf->status & 0x100) != 0 || current->tf->epc != $user_ecall_pc + 4 || current->tf->gpr.a0 != $user_ecall_nr)
    quit 1
end
delete breakpoints

set $sret_pc = (char *)forkrets - 4
if *(unsigned int *)$sret_pc != 0x10200073
    printf "could not locate sret before forkrets\n"
    quit 1
end
hbreak *$sret_pc if current->tf->epc == $user_ecall_pc + 4
continue
printf "before sret: tf.epc=%#lx tf.status=%#lx sp=%#lx\n", current->tf->epc, current->tf->status, $sp
monitor info registers
x/i $pc
delete breakpoints
si
printf "back in user code: pc=%#lx sp=%#lx a0=%ld\n", $pc, $sp, $a0
x/i $pc
if ($pc != $user_ecall_pc + 4 || $a0 != 0)
    quit 1
end
delete breakpoints
detach
