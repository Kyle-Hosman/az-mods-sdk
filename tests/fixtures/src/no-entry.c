/* A typo in the entry point's name: the core's dlsym finds nothing. */
#include "azmod-mod.h"
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "bad-noentry", T, 0, 0, 0 };
__attribute__((visibility("default"))) const az_mod_v1 *az_mod_entry(void) { return &M; }
