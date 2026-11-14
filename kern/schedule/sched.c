#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

void
wakeup_proc(struct proc_struct *proc) {
    assert(proc->state != PROC_ZOMBIE && proc->state != PROC_RUNNABLE);
    proc->state = PROC_RUNNABLE;
}

//LAB4 function3 2412456
void schedule(void)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    current->need_resched = 0;
    struct proc_struct *next = NULL;
    list_entry_t *start = current == idleproc ? &proc_list : &current->list_link;
    list_entry_t *le = start;
    do {
        le = list_next(le);
        if (le == &proc_list) continue;
        struct proc_struct *p = le2proc(le, list_link);
        if (p->state == PROC_RUNNABLE) { next = p; break; }
    } while (le != start);
    if (next == NULL) next = idleproc;
    next->runs++;
    proc_run(next);
    local_intr_restore(intr_flag);
}


