#include "hle.h"

/* The NSO modules are loaded before nnMain. No NRO files exist in this game
 * image. Symbol queries still need to search the loaded module set. */
HLE_FORCE("_ZN2nn2ro10InitializeEv", "Use the host module loader", void, ss_ro_init, void) {}
HLE_FORCE("_ZN2nn2ro12LookupSymbolEPmPKc", "Find a loaded guest symbol", Result, ss_ro_lookup, uintptr_t *out, const char *name) {
    void *symbol = guest_sym(name);
    *out = (uintptr_t)symbol;
    return symbol ? RES_OK : 0xa209u;
}
HLE_FORCE("_ZN2nn2ro18RegisterModuleInfoEPNS0_16RegistrationInfoEPKv", "NRO loading is not available", Result, ss_ro_register, void *info, const void *data) {
    (void)info; (void)data; return 0xa209u;
}
