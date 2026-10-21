/* Rhythm Heaven touch hints: which buttons the left half of the phone screen presses. */
#ifndef RH_TOUCH_H
#define RH_TOUCH_H
#include <stdint.h>

typedef struct { int layout, first, second, extra; } rh_touch_hint;   /* ss_touch.h layout, buttons or D-pad direction mask, SS_TOUCH_SHOW_* bits */
typedef struct { int active; unsigned keys; } rh_touch_state;  /* zero-initialize */

/* Feed one guest path the game opened. Returns 1 and fills *out when the hint changes. */
/* 暂停菜单关闭后把最近一次场景提示重新发布（rh_game.c 实现）。 */
void rh_touch_replay_last(void);

int rh_touch_path(rh_touch_state *state, const char *path, rh_touch_hint *out);
#endif
