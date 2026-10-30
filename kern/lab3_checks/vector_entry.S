# Independent stvec table; each slot exactly four bytes. 2412456.
.text
.option push
.option norvc
.balign 256
.globl c_vector_table
c_vector_table:
    j c_sync_route
    j c_soft_route
    .rept 14
    j __alltraps
    .endr

c_sync_route:
    addi sp, sp, -16
    sd t0, 0(sp)
    sd t1, 8(sp)
    la t0, c_sync_routes
    ld t1, 0(t0)
    addi t1, t1, 1
    sd t1, 0(t0)
    j c_route_return
c_soft_route:
    csrci sip, 2
    addi sp, sp, -16
    sd t0, 0(sp)
    sd t1, 8(sp)
    la t0, c_soft_routes
    ld t1, 0(t0)
    addi t1, t1, 1
    sd t1, 0(t0)
c_route_return:
    ld t0, 0(sp)
    ld t1, 8(sp)
    addi sp, sp, 16
    j __alltraps
.option pop
