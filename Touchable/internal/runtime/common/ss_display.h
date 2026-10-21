#ifndef SS_DISPLAY_H
#define SS_DISPLAY_H
#include <stdint.h>

typedef struct { int width, height; } ss_display_size;
/* UIKit publishes the view size before the guest thread starts. */
void ss_display_set_view(double width, double height);
/* Output height in pixels: 720 unless SS_RENDER_HEIGHT selects more.
   The output width stays at 1920 or less. */
int ss_display_output_height(void);
ss_display_size ss_display_resolution(int requested_height);
#endif
