#ifndef TOUCH_LAYOUT_H
#define TOUCH_LAYOUT_H
/* Positions of the Switch-style touch controls, in view points.
   This is plain C, so a Mac test can check the layout. */
#include <math.h>

/* The order matches the button list in App.m. */
enum { SS_PAD_A, SS_PAD_B, SS_PAD_X, SS_PAD_Y, SS_PAD_L, SS_PAD_R, SS_PAD_ZL, SS_PAD_ZR,
       SS_PAD_PLUS, SS_PAD_MINUS, SS_PAD_BUTTONS };

typedef struct { double x, y, size; } ss_pad_circle;   /* center and diameter */
typedef struct {
    int tablet;
    ss_pad_circle dpad, buttons[SS_PAD_BUTTONS];
    /* Menu touch mode hides the pad. It shows Back (B) in the place of − and a pad toggle next to +. */
    ss_pad_circle back, pad_toggle;
    double hit_margin, dpad_margin;   /* touch area outside each drawn circle */
    double dpad_center;               /* a touch this near the D-pad center selects no direction */
} ss_pad_layout;

/* A phone keeps the controls near the lower corners. A tablet raises them a little
   toward the resting thumbs and makes them 1.2 times larger. It also puts them near the
   side edges, so they cover less of the game. On an iPad mini the D-pad center is
   174 points above the lower edge and 119 points from the left edge. A small iPad window uses
   the phone layout. The shoulder buttons sit above their side, below the game HUD
   in the upper-left corner. ZL and ZR take the outer places, because ZR is the
   default dash and the outer place is nearer the thumb. − and + take the upper corners.
   WINDOW_TOP is the top inset below the iPadOS window controls in the upper-left corner
   (the safe area with vertical corner adaptation). A phone passes SAFE_TOP. */
static inline ss_pad_layout ss_pad_layout_make(double width, double height, double safe_left, double safe_right,
                                               double safe_top, double safe_bottom, double window_top, int pad_idiom) {
    ss_pad_layout p = {0};
    p.tablet = pad_idiom && fmin(width, height) >= 600;
    double k = p.tablet ? 1.2 : 1, left, right, bottom;
    if (p.tablet) {
        left = safe_left + 16;
        right = width - safe_right - 16;
        bottom = height - fmax(safe_bottom + 70, 90);
    } else {
        left = fmax(safe_left + 12, 28);
        right = width - fmax(safe_right + 12, 28);
        bottom = height - fmax(safe_bottom + 18, 36);
    }
    /* The D-pad is as wide as the face-button diamond. Its center keeps the height of the
       140-point D-pad in build 17, so the D-pad grows around the resting thumb. */
    double face = 54 * k, small = 46 * k, d = 57 * k, dpad = 2 * d + face;
    double sx = left + dpad / 2 + 2 * k, sy = bottom - 70 * k;
    double fx = right - face / 2 - d - 2 * k, fy = bottom - face / 2 - d;
    p.hit_margin = 8 * k;
    p.dpad_margin = 24 * k;
    p.dpad_center = 0.12 * dpad;
    /* On a tablet the D-pad is near the edge. ZL and ZR keep 8 points from the edge. */
    double zl = sx - d, zr = fx + d;
    if (p.tablet) {
        zl = fmax(zl, safe_left + 8 + small / 2);
        zr = fmin(zr, width - safe_right - 8 - small / 2);
    }
    /* The shoulder row rises when necessary. It keeps 2 points between the touch areas of the D-pad and of ZL and L. */
    double clear = dpad / 2 + p.dpad_margin + small / 2 + p.hit_margin + 2, near = fmin(sx - zl, d);
    double row = fmin(fy - 1.75 * d, sy - sqrt(clear * clear - near * near)), top = fmax(safe_top, 0) + 32;
    p.dpad = (ss_pad_circle){sx, sy, dpad};
    p.buttons[SS_PAD_A] = (ss_pad_circle){fx + d, fy, face};
    p.buttons[SS_PAD_B] = (ss_pad_circle){fx, fy + d, face};
    p.buttons[SS_PAD_X] = (ss_pad_circle){fx, fy - d, face};
    p.buttons[SS_PAD_Y] = (ss_pad_circle){fx - d, fy, face};
    p.buttons[SS_PAD_L] = (ss_pad_circle){sx + d, row, small};
    p.buttons[SS_PAD_R] = (ss_pad_circle){fx - d, row, small};
    p.buttons[SS_PAD_ZL] = (ss_pad_circle){zl, row, small};
    p.buttons[SS_PAD_ZR] = (ss_pad_circle){zr, row, small};
    /* On a tablet the window controls take the upper-left corner. − and + go below them,
       8 points from the side edges. They are 40 points there, so − stays clear of the larger game HUD. */
    double menu = p.tablet ? 40 : small;
    double mx = p.tablet ? safe_left + 8 + menu / 2 : left + menu / 2;
    double px = p.tablet ? width - safe_right - 8 - menu / 2 : right - menu / 2;
    double my = p.tablet ? fmax(window_top, safe_top) + 4 + menu / 2 : top;
    p.buttons[SS_PAD_MINUS] = (ss_pad_circle){mx, my, menu};
    p.buttons[SS_PAD_PLUS] = (ss_pad_circle){px, my, menu};
    /* Back and − never show together. The pad toggle also shows with the pad, so it keeps clear of +. */
    p.back = p.buttons[SS_PAD_MINUS];
    p.pad_toggle = (ss_pad_circle){px - menu - 24 * k, my, menu};
    return p;
}

/* D-pad directions. The bit order matches SS_BUTTON_LEFT to SS_BUTTON_DOWN (buttons 12 to 15). */
enum { SS_DPAD_LEFT = 1, SS_DPAD_UP = 2, SS_DPAD_RIGHT = 4, SS_DPAD_DOWN = 8 };
#define SS_DPAD_HOLD (5.0 / 45)   /* a held direction keeps 5 more degrees on each side */

/* Return the directions for a touch at (x, y) from the D-pad center. y grows down.
   Each of the 8 directions takes a 45-degree sector. The diagonals hold two directions.
   PREVIOUS keeps its sector a little wider, so a thumb on a sector edge does not flicker. */
static inline unsigned ss_dpad_directions(double x, double y, double center, unsigned previous) {
    static const unsigned sectors[8] = {
        SS_DPAD_RIGHT, SS_DPAD_RIGHT | SS_DPAD_UP, SS_DPAD_UP, SS_DPAD_UP | SS_DPAD_LEFT,
        SS_DPAD_LEFT, SS_DPAD_LEFT | SS_DPAD_DOWN, SS_DPAD_DOWN, SS_DPAD_DOWN | SS_DPAD_RIGHT};
    if (hypot(x, y) <= center) return 0;
    double sector = atan2(-y, x) / atan(1.0);   /* 0 right, 2 up, 4 left, 6 down */
    for (int i = 0; i < 8; ++i)
        if (previous == sectors[i] && fabs(remainder(sector - i, 8.0)) <= 0.5 + SS_DPAD_HOLD) return previous;
    return sectors[(int)floor(sector + 0.5) & 7];
}
#endif
