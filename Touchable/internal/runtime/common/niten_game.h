#ifndef NITEN_GAME_H
#define NITEN_GAME_H
#include <stdint.h>
/* Each game supplies these hooks. They run on a guest thread. */
void niten_game_install(void);
int niten_game_host_call(int number, uint64_t *registers);
#endif
