#ifndef SS_INDIRECT_BRANCH_H
#define SS_INDIRECT_BRANCH_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/* Experimental Darwin arm64 support for registered X18 instruction sites.
 * Configure this before guest threads start. Do not change the table while
 * guest code runs. Each address must point to BRK #0x6e18 in signed code.
 * The caller owns the pthread key for virtual x18 for the full runtime life.
 * Give each guest thread an alternate signal stack before it enters guest code.
 */
typedef enum { SS_BR_X18 = 1, SS_MOV_SP_X18 = 2, SS_BLR_X18 = 3 } ss_trap_kind;
typedef struct {
    uintptr_t address;
    uintptr_t resume; /* Original instruction + 4 for MOV SP, X18 or BLR X18. */
    ss_trap_kind kind;
} ss_trap_site;
int ss_indirect_install(const ss_trap_site *sites, size_t count,
                        pthread_key_t x18_key);
/* Call only after every guest thread has stopped. */
int ss_indirect_uninstall(void);

#endif
