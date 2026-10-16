#include "azmod-mod.h"
extern int other_fn(void);
static int init(const az_core_v1 *c)
{
    (void)c;
    return other_fn();
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "bad-other", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
