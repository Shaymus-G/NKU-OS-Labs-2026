# Build the default hello image and start it with `make debug` first.
set pagination off
set confirm off
set remotetimeout unlimited
file bin/kernel
add-symbol-file obj/__user_hello.out
target remote localhost:1234

hbreak *_start
continue
printf "first user instruction: pc=%#lx sp=%#lx satp=%#lx\n", $pc, $sp, $satp
x/i $pc
delete breakpoints

hbreak *__alltraps if $scause == 8
continue
set $user_ecall_pc = $sepc
set $user_ecall_nr = $a0
printf "user ecall entry: scause=%#lx sepc=%#lx sstatus=%#lx sp=%#lx sscratch=%#lx a0=%ld\n", $scause, $sepc, $sstatus, $sp, $sscratch, $a0
if (($sstatus & 0x100) != 0 || $sscratch == 0)
    quit 1
end
delete breakpoints

hbreak *syscall
continue
printf "syscall dispatch: pc=%#lx sp=%#lx tf.epc=%#lx saved_sp=%#lx syscall=%ld\n", $pc, $sp, current->tf->epc, current->tf->gpr.sp, current->tf->gpr.a0
if (current->tf->epc != $user_ecall_pc + 4 || current->tf->gpr.a0 != $user_ecall_nr)
    quit 1
end
delete breakpoints

set $sret_pc = (char *)forkrets - 4
if *(unsigned int *)$sret_pc != 0x10200073
    printf "could not locate sret before forkrets\n"
    quit 1
end
hbreak *$sret_pc if $sepc == $user_ecall_pc + 4
continue
printf "before sret: sepc=%#lx sstatus=%#lx sp=%#lx sscratch=%#lx\n", $sepc, $sstatus, $sp, $sscratch
x/i $pc
delete breakpoints
hbreak *($user_ecall_pc + 4)
continue
printf "back in user code: pc=%#lx sp=%#lx a0=%ld\n", $pc, $sp, $a0
x/i $pc
if ($pc != $user_ecall_pc + 4 || $a0 != 0)
    quit 1
end
delete breakpoints
detach
