#include "rh_menu_logic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    rh_map_tap state = {0};
    /* An already selected level still needs the first touch to arm entry. */
    assert(!rh_map_tap_activate(&state, 1, 7, 1));
    assert(rh_map_tap_activate(&state, 1, 7, 1));
    assert(!rh_map_tap_activate(&state, 1, 7, 1));
    assert(!rh_map_tap_activate(&state, 1, 8, 0));
    assert(rh_map_tap_activate(&state, 1, 8, 1));
    assert(!rh_map_tap_activate(&state, 1, 9, 0));
    /* A controller move or a different scene must not start a level. */
    assert(!rh_map_tap_activate(&state, 1, 9, 0));
    assert(!rh_map_tap_activate(&state, 2, 9, 1));
    state.armed = 0;  /* drag or cancellation */
    assert(!rh_map_tap_activate(&state, 2, 9, 1));
    assert(rh_map_tap_activate(&state, 2, 9, 1));
    /* Start-menu limits use the observed 9 items, 13 slots and 84-pixel spacing. */
    assert(rh_menu_scroll_limit(-500, 9, 13, -2, 2, 84) == -336);
    assert(rh_menu_scroll_limit(500, 9, 13, -2, 2, 84) == 0);
    assert(rh_menu_scroll_limit(-123, 9, 13, -2, 2, 84) == -123);
    assert(isfinite(rh_menu_scroll_limit(NAN, 9, 13, -2, 2, 84)));
    /* The archive category list has 8 items, 13 slots and 3 visible rows. */
    assert(rh_menu_scroll_limit(-1000, 8, 13, -1, 1, 84) == -420);
    assert(rh_menu_scroll_limit(1000, 8, 13, -1, 1, 84) == 0);
    assert(rh_menu_scroll_limit(-252, 8, 13, -1, 1, 84) == -252);
    puts("test_menu: ok");
}
