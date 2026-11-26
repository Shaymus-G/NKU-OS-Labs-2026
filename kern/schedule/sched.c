#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <assert.h>

enum { ROUND_ROBIN_TICKS = 4 };
static unsigned ticks_left;

void sched_tick(void)
{
    if (current != NULL && (ticks_left == 0 || --ticks_left == 0)) {
        current->need_resched = 1;
    }
}

void wakeup_proc(struct proc_struct *proc)
{
    assert(proc->state != PROC_ZOMBIE);
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE)
        {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
        }
        else
        {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
}

//lab4 请补充你在lab4中的代码
void schedule(void)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    current->need_resched = 0;

    struct proc_struct *next = idleproc;
    list_entry_t *start = current == idleproc ? &proc_list : &current->list_link;
    list_entry_t *entry = start;
    do {
        entry = list_next(entry);
        if (entry == &proc_list) continue;
        struct proc_struct *proc = le2proc(entry, list_link);
        if (proc->state == PROC_RUNNABLE) {
            next = proc;
            break;
        }
    } while (entry != start);

    ticks_left = ROUND_ROBIN_TICKS;
    next->runs++;
    proc_run(next);
    local_intr_restore(intr_flag);
}
