#include "azmod-mod.h"
static int init(const az_core_v1 *c)
{
    (void)c;
    return 0;
}
static const char *const T[] = { "any", 0 };
static const az_mod_v1 M = { AZ_MOD_ABI, sizeof(az_mod_v1), "bad-sqlite", T, init, 0, 0 };
const az_mod_v1 *az_mod_v1_entry(void) { return &M; }
__attribute__((visibility("default"))) int sqlite3_open(const char *f, void **db)
{
    (void)f;
    (void)db;
    return 1;
}
