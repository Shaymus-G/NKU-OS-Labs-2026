.text
.align 2
.globl c_context_yield
c_context_yield:
addi sp, sp, -112
sd ra, 96(sp)
sd s0, 0(sp)
li s0, 1280
sd s1, 8(sp)
li s1, 1281
sd s2, 16(sp)
li s2, 1282
sd s3, 24(sp)
li s3, 1283
sd s4, 32(sp)
li s4, 1284
sd s5, 40(sp)
li s5, 1285
sd s6, 48(sp)
li s6, 1286
sd s7, 56(sp)
li s7, 1287
sd s8, 64(sp)
li s8, 1288
sd s9, 72(sp)
li s9, 1289
sd s10, 80(sp)
li s10, 1290
sd s11, 88(sp)
li s11, 1291
call schedule
li t0, 1280
bne s0, t0, 1f
li t0, 1281
bne s1, t0, 1f
li t0, 1282
bne s2, t0, 1f
li t0, 1283
bne s3, t0, 1f
li t0, 1284
bne s4, t0, 1f
li t0, 1285
bne s5, t0, 1f
li t0, 1286
bne s6, t0, 1f
li t0, 1287
bne s7, t0, 1f
li t0, 1288
bne s8, t0, 1f
li t0, 1289
bne s9, t0, 1f
li t0, 1290
bne s10, t0, 1f
li t0, 1291
bne s11, t0, 1f
li a0, 0
j 2f
1: li a0, 1
2:
ld s0, 0(sp)
ld s1, 8(sp)
ld s2, 16(sp)
ld s3, 24(sp)
ld s4, 32(sp)
ld s5, 40(sp)
ld s6, 48(sp)
ld s7, 56(sp)
ld s8, 64(sp)
ld s9, 72(sp)
ld s10, 80(sp)
ld s11, 88(sp)
ld ra, 96(sp)
addi sp, sp, 112
ret
