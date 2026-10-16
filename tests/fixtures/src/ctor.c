#include "azmod-mod.h"
static int ready;
__attribute__((constructor)) static void too_early(void) { ready = 1; }
static int init(const az_core_v1 *c)
{
    (void)c;
    return !ready;
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "warn-ctor", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
