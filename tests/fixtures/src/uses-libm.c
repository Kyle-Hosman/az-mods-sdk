/* Allowed: libm is on the deck's list. */
#include <math.h>
#include "azmod-mod.h"
volatile double g = 2.0;
static int init(const az_core_v1 *c)
{
    (void)c;
    return sqrt(g) > 10.0;
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "good-libm", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
