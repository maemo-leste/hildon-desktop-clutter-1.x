#ifndef _TIDY_UTIL
#define _TIDY_UTIL

#include <clutter/clutter.h>

/* To handle the problem where we might be doing nested writes to
 * offscreen buffers */
void tidy_util_cogl_push_offscreen_buffer(CoglHandle fbo);
void tidy_util_cogl_pop_offscreen_buffer(void);

void tidy_set_cogl_color(CoglColor *c, guint8 r, guint8 g, guint8 b, guint8 a);
void tidy_set_cogl_from_clutter_color(CoglColor *c, const ClutterColor *cl);
#endif
