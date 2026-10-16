#include <sys/types.h>
#include "azmod-mod.h"
static int init(const az_core_v1 *c)
{
    (void)c;
    return 0;
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "bad-read", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
/* An interposer: exactly what a mod must never export. */
__attribute__((visibility("default"))) ssize_t read(int fd, void *b, size_t n)
{
    (void)fd;
    (void)b;
    (void)n;
    return -1;
}
