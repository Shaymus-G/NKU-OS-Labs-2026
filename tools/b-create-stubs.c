/* Linked only in the temporary creation-test copy. Any execution is a failure. */
#include <proc.h>
#include <trap.h>
#include <assert.h>
void cpu_idle(void) { panic("B isolation: cpu_idle must not run"); }
void schedule(void) { panic("B isolation: schedule must not run"); }
void proc_run(struct proc_struct *p) { panic("B isolation: proc_run must not run"); }
void switch_to(struct context *a, struct context *b) { panic("B isolation: switch_to must not run"); }
void interrupt_handler(struct trapframe *tf) { panic("B isolation: unexpected interrupt"); }
void exception_handler(struct trapframe *tf) { panic("B isolation: unexpected exception"); }
