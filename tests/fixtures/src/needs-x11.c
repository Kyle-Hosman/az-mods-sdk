#include "azmod-mod.h"
extern int XFlush(void *dpy);
static int init(const az_core_v1 *c)
{
    (void)c;
    return XFlush(0);
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "bad-x11", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
