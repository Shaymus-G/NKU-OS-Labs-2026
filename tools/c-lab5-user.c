#include <error.h>
#include <stdio.h>
#include <string.h>
#include <ulib.h>

#define PAGE_BYTES 4096

static volatile unsigned char data[2 * PAGE_BYTES] __attribute__((aligned(PAGE_BYTES)));
static unsigned char status_bytes[2 * PAGE_BYTES] __attribute__((aligned(PAGE_BYTES)));
static const int readonly = 91;

static void
check_fork_isolation(void)
{
    data[PAGE_BYTES - 1] = 11;
    data[PAGE_BYTES] = 12;
    memset(status_bytes, 0x5a, sizeof(status_bytes));
    const size_t offset = PAGE_BYTES - sizeof(int) / 2;

    int keeper = fork();
    assert(keeper >= 0);
    if (keeper == 0) {
        for (int i = 0; i < 24; i++) yield();
        assert(data[PAGE_BYTES - 1] == 11 && data[PAGE_BYTES] == 12);
        for (size_t i = 0; i < sizeof(int); i++) {
            assert(status_bytes[offset + i] == 0x5a);
        }
        exit(17);
    }

    int writer = fork();
    assert(writer >= 0);
    if (writer == 0) {
        data[PAGE_BYTES - 1] = 21;
        data[PAGE_BYTES] = 22;
        assert(data[PAGE_BYTES - 1] == 21 && data[PAGE_BYTES] == 22);
        exit(37);
    }

    assert(waitpid(writer, (int *)&readonly) == -E_INVAL);
    assert(waitpid(writer, (int *)(status_bytes + offset)) == 0);
    int code;
    memcpy(&code, status_bytes + offset, sizeof(code));
    assert(code == 37);
    assert(status_bytes[offset - 1] == 0x5a && status_bytes[offset + sizeof(code)] == 0x5a);
    assert(waitpid(writer, NULL) == -E_BAD_PROC);
    assert(data[PAGE_BYTES - 1] == 11 && data[PAGE_BYTES] == 12);
    data[PAGE_BYTES - 1] = 31;
    data[PAGE_BYTES] = 32;
    assert(waitpid(keeper, &code) == 0 && code == 17);
    assert(data[PAGE_BYTES - 1] == 31 && data[PAGE_BYTES] == 32);
    cprintf("C user: fork isolation, cross-page wait output and repeat wait PASS\n");
}

static void
check_orphan_adoption(void)
{
    int owner = fork();
    assert(owner >= 0);
    if (owner == 0) {
        int running = fork();
        assert(running >= 0);
        if (running == 0) {
            for (int i = 0; i < 16; i++) yield();
            cprintf("C user: adopted running child finished\n");
            exit(55);
        }
        int zombie = fork();
        assert(zombie >= 0);
        if (zombie == 0) exit(56);
        for (int i = 0; i < 4; i++) yield();
        exit(57);
    }
    int code;
    assert(waitpid(owner, &code) == 0 && code == 57);
    assert(wait() == -E_BAD_PROC);
    assert(kill(-1) == -E_INVAL && kill(0) == -E_INVAL && kill(1) == -E_INVAL);
    cprintf("C user: orphan handoff and invalid kill PASS\n");
}

int
main(void)
{
    check_fork_isolation();
    check_orphan_adoption();
    return 0;
}
