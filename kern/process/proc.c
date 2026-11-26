#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

//lab4 请补充你在lab4中的代码

//lab5 function2 2412559 update alloc_proc
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(*proc));
    if (proc != NULL) {
        memset(proc, 0, sizeof(*proc));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
        list_init(&proc->list_link);
        list_init(&proc->hash_link);
    }
    return proc;
}


// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return strncpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}






// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

//lab5 function2 2412559 update kernel_thread
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(tf));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}


// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

//lab5 function3 YOUR CODE update copy_mm



//lab5 function2 2412559
// C calls this only after allocating the private child kernel stack.
static void
copy_thread(struct proc_struct *proc, uintptr_t stack, struct trapframe *tf)
{
    static_assert(sizeof(struct trapframe) == 36 * sizeof(uintptr_t));
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE - sizeof(*tf));
    *proc->tf = *tf;
    proc->tf->gpr.a0 = 0;
    if (tf->status & SSTATUS_SPP) {
        proc->tf->gpr.sp = (uintptr_t)proc->tf;
    } else if (stack != 0) {
        proc->tf->gpr.sp = stack;
    }
    proc->tf->status = (proc->tf->status | SSTATUS_SPIE) & ~SSTATUS_SIE;
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}

/* Copy through kernel aliases, not a raw user VA. VMA and leaf permissions
 * must both agree; no SUM setting or demand paging is assumed here. */
static bool
proc_mem_access(struct mm_struct *mm, uintptr_t addr, size_t len, bool write)
{
    if (len == 0) return 1;
    if (addr + len < addr || !user_mem_check(mm, addr, len, write)) return 0;
    if (mm == NULL) return 1;
    // Preflight the entire range before a write, including cross-page stores.
    for (uintptr_t at = addr; at < addr + len;) {
        pte_t *pte = get_pte(mm->pgdir, at, 0);
        uint32_t required = PTE_V | PTE_U | (write ? PTE_W : PTE_R);
        if (pte == NULL || (*pte & required) != required) return 0;
        at += ((addr + len - at < PGSIZE - PGOFF(at)) ?
               addr + len - at : PGSIZE - PGOFF(at));
    }
    return 1;
}

static bool
proc_mem_copy(struct mm_struct *mm, void *buffer, uintptr_t addr,
              size_t len, bool to_user)
{
    if (!proc_mem_access(mm, addr, len, to_user)) return 0;
    if (len == 0) return 1;
    if (mm == NULL) {
        if (to_user) memcpy((void *)addr, buffer, len);
        else memcpy(buffer, (void *)addr, len);
        return 1;
    }
    char *bytes = buffer;
    while (len != 0) {
        pte_t *pte = get_pte(mm->pgdir, addr, 0);
        char *alias = (char *)page2kva(pte2page(*pte)) + PGOFF(addr);
        size_t n = PGSIZE - PGOFF(addr);
        if (n > len) n = len;
        if (to_user) memcpy(alias, bytes, n);
        else memcpy(bytes, alias, n);
        bytes += n; addr += n; len -= n;
    }
    return 1;
}

static void
destroy_private_mm(struct mm_struct *mm)
{
    assert(mm_count(mm) == 0);
    if (mm->pgdir != NULL) {
        exit_mmap(mm);
        put_pgdir(mm);
    }
    mm_destroy(mm);
}

static bool
read_image(struct mm_struct *source, unsigned char *binary, size_t size,
           size_t offset, void *dst, size_t len)
{
    return offset <= size && len <= size - offset &&
           proc_mem_copy(source, dst, (uintptr_t)binary + offset, len, 0);
}

// Validate before allocating or calling mm_map (which asserts on overlaps).
static int
check_elf(struct mm_struct *source, unsigned char *binary, size_t size,
          struct elfhdr *elf)
{
    if ((uintptr_t)binary + size < (uintptr_t)binary ||
        !user_mem_check(source, (uintptr_t)binary, size, 0) ||
        !read_image(source, binary, size, 0, elf, sizeof(*elf))) return -E_INVAL_ELF;
    if (elf->e_magic != ELF_MAGIC || elf->e_elf[0] != 2 ||
        elf->e_elf[1] != 1 || elf->e_elf[2] != 1 || elf->e_machine != 243 ||
        elf->e_type != 2 || elf->e_version != 1 || elf->e_ehsize != sizeof(*elf) ||
        elf->e_phentsize != sizeof(struct proghdr) || elf->e_phnum == 0 ||
        elf->e_phoff > size ||
        elf->e_phnum > (size - elf->e_phoff) / sizeof(struct proghdr)) return -E_INVAL_ELF;
    bool entry_ok = 0;
    for (unsigned i = 0; i < elf->e_phnum; i++) {
        struct proghdr ph;
        if (!read_image(source, binary, size, elf->e_phoff + i * sizeof(ph),
                        &ph, sizeof(ph))) return -E_INVAL_ELF;
        if (ph.p_type != ELF_PT_LOAD) continue;
        if (ph.p_filesz > ph.p_memsz || ph.p_offset > size ||
            ph.p_filesz > size - ph.p_offset ||
            ph.p_va < USERBASE || ph.p_va >= USTACKTOP - USTACKSIZE ||
            ph.p_memsz > USTACKTOP - USTACKSIZE - ph.p_va ||
            !(ph.p_flags & (ELF_PF_R | ELF_PF_X)) ||
            ((ph.p_flags & ELF_PF_W) && !(ph.p_flags & ELF_PF_R)) ||
            (ph.p_flags & ~(ELF_PF_R | ELF_PF_W | ELF_PF_X)) ||
            (ph.p_align > 1 && ((ph.p_align & (ph.p_align - 1)) ||
             ((ph.p_va - ph.p_offset) & (ph.p_align - 1))))) return -E_INVAL_ELF;
        if (ph.p_memsz == 0) continue;
        if ((ph.p_flags & ELF_PF_X) && elf->e_entry >= ph.p_va &&
            elf->e_entry - ph.p_va < ph.p_memsz) entry_ok = 1;
        for (unsigned j = 0; j < i; j++) {
            struct proghdr prev;
            if (!read_image(source, binary, size, elf->e_phoff + j * sizeof(prev),
                            &prev, sizeof(prev))) return -E_INVAL_ELF;
            if (prev.p_type == ELF_PT_LOAD && prev.p_memsz != 0 &&
                ROUNDDOWN(ph.p_va, PGSIZE) < ROUNDUP(prev.p_va + prev.p_memsz, PGSIZE) &&
                ROUNDDOWN(prev.p_va, PGSIZE) < ROUNDUP(ph.p_va + ph.p_memsz, PGSIZE))
                return -E_INVAL_ELF;
        }
    }
    return entry_ok ? 0 : -E_INVAL_ELF;
}

// Build privately first: invalid images and OOM leave the old mm/tf intact.
static int
load_icode(unsigned char *binary, size_t size)
{
    struct mm_struct *old = current->mm, *mm = NULL;
    struct elfhdr elf;
    struct trapframe tf;
    int ret;
    lock_mm(old);
    if ((ret = check_elf(old, binary, size, &elf)) != 0) goto out;
    ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL) goto out;
    if (setup_pgdir(mm) != 0) goto fail;
    for (unsigned i = 0; i < elf.e_phnum; i++) {
        struct proghdr ph;
        if (!read_image(old, binary, size, elf.e_phoff + i * sizeof(ph), &ph, sizeof(ph))) {
            ret = -E_INVAL_ELF; goto fail;
        }
        if (ph.p_type != ELF_PT_LOAD || ph.p_memsz == 0) continue;
        uint32_t flags = 0, perm = PTE_U;
        if (ph.p_flags & ELF_PF_R) { flags |= VM_READ; perm |= PTE_R; }
        if (ph.p_flags & ELF_PF_W) { flags |= VM_WRITE; perm |= PTE_W; }
        if (ph.p_flags & ELF_PF_X) { flags |= VM_EXEC; perm |= PTE_X; }
        if ((ret = mm_map(mm, ph.p_va, ph.p_memsz, flags, NULL)) != 0) goto fail;
        for (uintptr_t va = ROUNDDOWN(ph.p_va, PGSIZE);
             va < ph.p_va + ph.p_memsz; va += PGSIZE) {
            struct Page *page = pgdir_alloc_page(mm->pgdir, va, perm);
            if (page == NULL) { ret = -E_NO_MEM; goto fail; }
            memset(page2kva(page), 0, PGSIZE);
            uintptr_t begin = va < ph.p_va ? ph.p_va : va;
            uintptr_t end = va + PGSIZE;
            if (end > ph.p_va + ph.p_filesz) end = ph.p_va + ph.p_filesz;
            if (begin < end && !read_image(old, binary, size,
                    ph.p_offset + begin - ph.p_va,
                    (char *)page2kva(page) + PGOFF(begin), end - begin)) {
                ret = -E_INVAL_ELF; goto fail;
            }
        }
    }
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE,
                     VM_READ | VM_WRITE | VM_STACK, NULL)) != 0) goto fail;
    // No demand paging: eagerly back the stack except its bottom guard page.
    for (uintptr_t va = USTACKTOP - USTACKSIZE + PGSIZE; va < USTACKTOP; va += PGSIZE) {
        struct Page *page = pgdir_alloc_page(mm->pgdir, va, PTE_U | PTE_R | PTE_W);
        if (page == NULL) { ret = -E_NO_MEM; goto fail; }
        memset(page2kva(page), 0, PGSIZE);
    }
    memset(&tf, 0, sizeof(tf));
    tf.epc = elf.e_entry;
    tf.gpr.sp = USTACKTOP;
    tf.status = SSTATUS_SPIE; // SPP=0, SIE=0, SUM=0; sret enters U mode.
    unlock_mm(old);
    bool intr_flag;
    local_intr_save(intr_flag);
    set_mm_count(mm, 1);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    *current->tf = tf;
    lsatp(current->pgdir);
    flush_tlb();
    asm volatile("fence.i" ::: "memory");
    if (old != NULL && mm_count_dec(old) == 0) destroy_private_mm(old);
    local_intr_restore(intr_flag);
    return 0;
fail:
    destroy_private_mm(mm);
out:
    unlock_mm(old);
    return ret;
}

int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    if (current == NULL || current->tf == NULL || name == NULL || binary == NULL ||
        len == 0 || len > PROC_NAME_LEN || size < sizeof(struct elfhdr)) return -E_INVAL;
    // Only a real kernel thread may use trusted kernel arguments.
    if (current->mm == NULL && !(current->tf->status & SSTATUS_SPP)) return -E_INVAL;
    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    lock_mm(current->mm);
    bool valid = proc_mem_copy(current->mm, local_name, (uintptr_t)name, len, 0);
    unlock_mm(current->mm);
    if (!valid) return -E_INVAL;
    int ret = load_icode(binary, size);
    if (ret == 0) set_proc_name(current, local_name);
    return ret;
}

int
do_wait(int pid, int *code_store)
{
    if (pid < 0 || pid >= MAX_PID) return -E_BAD_PROC;
    for (;;) {
        struct mm_struct *mm = current->mm;
        lock_mm(mm);
        // Validate output even when the child has not exited yet.
        bool valid = code_store == NULL ||
            proc_mem_access(mm, (uintptr_t)code_store, sizeof(int), 1);
        if (!valid) { unlock_mm(mm); return -E_INVAL; }
        bool intr_flag, has_child = 0;
        struct proc_struct *child = NULL;
        local_intr_save(intr_flag);
        if (pid > 0) {
            child = find_proc(pid);
            if (child != NULL && child->parent == current) has_child = 1;
            else child = NULL;
            if (child != NULL && child->state != PROC_ZOMBIE) child = NULL;
        } else {
            for (struct proc_struct *p = current->cptr; p != NULL; p = p->optr) {
                has_child = 1;
                if (p->state == PROC_ZOMBIE) { child = p; break; }
            }
        }
        if (child != NULL) {
            int code = child->exit_code;
            if (code_store != NULL && !proc_mem_copy(mm, &code,
                                    (uintptr_t)code_store, sizeof(code), 1)) {
                local_intr_restore(intr_flag); unlock_mm(mm); return -E_INVAL;
            }
            assert(child != idleproc && child != initproc && child->mm == NULL);
            unhash_proc(child);
            remove_links(child);
            put_kstack(child);
            kfree(child);
            local_intr_restore(intr_flag);
            unlock_mm(mm);
            return 0;
        }
        if (!has_child) {
            local_intr_restore(intr_flag); unlock_mm(mm); return -E_BAD_PROC;
        }
        if (current->flags & PF_EXITING) {
            local_intr_restore(intr_flag); unlock_mm(mm);
            do_exit(-E_KILLED);
            panic("do_exit returned to wait");
        }
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        unlock_mm(mm);
        // Keep interrupts masked until schedule consumes the sleeping state.
        schedule();
        local_intr_restore(intr_flag);
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
            panic("do_exit returned to wait");
        }
    }
}


//lab5 function3 YOUR CODE


//lab5 function4 YOUR CODE


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


//lab5 function2 2412559
static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    assert(current->mm == NULL && current->tf != NULL);
    assert((uintptr_t)current->tf == current->kstack + KSTACKSIZE - sizeof(*current->tf));
    bool intr_flag;
    local_intr_save(intr_flag);
    int ret = do_execve(name, strlen(name), binary, size);
    if (ret == 0) {
        // The reserved tf is above the live C stack; do not restore SIE here.
        forkrets(current->tf);
        panic("forkrets returned");
    }
    local_intr_restore(intr_flag);
    return ret;
}

static int
user_main(void *arg)
{
#ifdef TEST
    return KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    return KERNEL_EXECVE(hello);
#endif
}

static int
init_main(void *arg)
{
    size_t free_before = nr_free_pages();
    int pid = kernel_thread(user_main, NULL, 0);
    assert(pid > 0);
    int ret;
    while ((ret = do_wait(0, NULL)) == 0) schedule();
    assert(ret == -E_BAD_PROC);
    cprintf("all user-mode processes have quit.\n");
    assert(nr_process == 2 && initproc->cptr == NULL && idleproc->cptr == initproc);
    assert(list_next(&proc_list) == &initproc->list_link &&
           list_prev(&proc_list) == &initproc->list_link);
    assert(nr_free_pages() == free_before);
    cprintf("init check memory pass.\n");
    // init must remain alive to adopt orphans; it must never return to do_exit.
    for (;;) {
        int adopted = do_wait(0, NULL);
        assert(adopted == 0 || adopted == -E_BAD_PROC);
        schedule();
    }
}

void
proc_init(void)
{
    list_init(&proc_list);
    for (unsigned i = 0; i < HASH_LIST_SIZE; i++) list_init(&hash_list[i]);
    assert(nr_process == 0);
    idleproc = alloc_proc();
    assert(idleproc != NULL);
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    current = idleproc;
    nr_process = 1; // idle stays outside the ordinary list and PID hash.
    int pid = kernel_thread(init_main, NULL, 0);
    assert(pid > 0 && (initproc = find_proc(pid)) != NULL);
    set_proc_name(initproc, "init");
    assert(initproc->pid == 1 && initproc->parent == idleproc);
}

void
cpu_idle(void)
{
    for (;;) if (current->need_resched) schedule();
}



