/* The single-player host has no Nintendo network services. */
#include <hle_net.c>
HLE_FORCE("_ZN2nn6socket10InitializeEPvmmi", "Use host network error reporting", Result, ss_socket_init, void *pool, size_t size, size_t allocation, int count) {
    (void)pool; (void)size; (void)allocation; (void)count; return RES_OK;
}
