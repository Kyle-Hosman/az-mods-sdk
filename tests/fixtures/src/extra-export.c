/* Built WITHOUT -fvisibility=hidden: the helper leaks out as an export. */
#include "azmod-mod.h"
int helper_scale(int x) { return x * 2; }
static int init(const az_core_v1 *c)
{
    (void)c;
    return helper_scale(0);
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "bad-extra", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
