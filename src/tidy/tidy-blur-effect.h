#ifndef TIDYBLUREFFECT_H
#define TIDYBLUREFFECT_H

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define TIDY_TYPE_BLUR_EFFECT         (tidy_blur_effect_get_type ())
#define TIDY_BLUR_EFFECT(obj)        (G_TYPE_CHECK_INSTANCE_CAST ((obj), TIDY_TYPE_BLUR_EFFECT , TidyBlurEffect))
#define TIDY_IS_BLUR_EFFECT(obj)     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TIDY_TYPE_BLUR_EFFECT ))

typedef struct _TidyBlurEffect       TidyBlurEffect;
typedef struct _TidyBlurEffectClass  TidyBlurEffectClass;

GType tidy_blur_effect_get_type (void) G_GNUC_CONST;

ClutterEffect *tidy_blur_effect_new (void);

void tidy_blur_effect_set_blur(ClutterEffect *self, guint blur);
guint tidy_blur_effect_get_blur(ClutterEffect *self);
void tidy_blur_effect_set_zoom(ClutterEffect *self, gfloat zoom);
gfloat tidy_blur_effect_get_zoom(ClutterEffect *self);
void tidy_blur_effect_set_brigtness(ClutterEffect *self, gfloat brigtness);
gfloat tidy_blur_effect_get_brigtness(ClutterEffect *self);

G_END_DECLS

#endif /* TIDYBLUREFFECT_H */
