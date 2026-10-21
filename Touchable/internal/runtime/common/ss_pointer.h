/* Screen coordinates use the displayed 1920 x 1080 game picture. */
#ifndef SS_POINTER_H
#define SS_POINTER_H
#include <stdint.h>

#define SS_POINTER_MAX_TARGETS 192
enum { SS_POINTER_NONE, SS_POINTER_MENU, SS_POINTER_MAP, SS_POINTER_HINTS };
enum { SS_POINTER_DOWN, SS_POINTER_MOVE, SS_POINTER_UP, SS_POINTER_CANCEL };
enum { SS_POINTER_TAP = 1, SS_POINTER_PAN = 2, SS_POINTER_FINISH = 4 };
enum { SS_POINTER_SELECTED = 1, SS_POINTER_BUTTON = 2 };

typedef struct { float x, y, width, height; } ss_pointer_rect;
typedef struct {
    uint64_t id;
    ss_pointer_rect rect;
    unsigned flags;
    int button;
} ss_pointer_target;
typedef struct {
    uint64_t owner, token;
    /* Available tutorial actions. This mask does not follow label visibility or animation. */
    uint64_t tutorial_buttons;
    int mode, count;
    ss_pointer_rect scroll;
    ss_pointer_target targets[SS_POINTER_MAX_TARGETS];
} ss_pointer_frame;
typedef struct { uint64_t token; int type; float x, y; } ss_pointer_event;
typedef struct {
    uint64_t token;
    float start_x, start_y, last_x, last_y;
    int down, dragging;
} ss_pointer_gesture;
typedef struct { int flags; float x, y, dx, dy; } ss_pointer_action;

/* A game publishes a frame on its input thread. The phone reads a copy. */
uint64_t ss_pointer_publish(const ss_pointer_frame *frame);
int ss_pointer_read(ss_pointer_frame *frame);
void ss_pointer_clear(void);
void ss_pointer_push(ss_pointer_event event);
int ss_pointer_take(uint64_t token, ss_pointer_event *event);
int ss_pointer_hit(const ss_pointer_frame *frame, float x, float y);
int ss_pointer_contains(ss_pointer_rect rect, float x, float y);
int ss_pointer_location(double width, double height, double x, double y, float *game_x, float *game_y);
ss_pointer_action ss_pointer_track(ss_pointer_gesture *gesture, ss_pointer_event event);
#endif
