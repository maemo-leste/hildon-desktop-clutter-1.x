#ifndef TIDYMULTIBLUREFFECT_H
#define TIDYMULTIBLUREFFECT_H

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define TIDY_TYPE_MULTI_BLUR_EFFECT         (tidy_multi_blur_effect_get_type ())
#define TIDY_MULTI_BLUR_EFFECT(obj)        (G_TYPE_CHECK_INSTANCE_CAST ((obj), TIDY_TYPE_MULTI_BLUR_EFFECT , TidyMultiBlurEffect))
#define TIDY_IS_MULTI_BLUR_EFFECT(obj)     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TIDY_TYPE_MULTI_BLUR_EFFECT ))

typedef struct _TidyMultiBlurEffect       TidyMultiBlurEffect;
typedef struct _TidyMultiBlurEffectClass  TidyMultiBlurEffectClass;

GType tidy_multi_blur_effect_get_type (void) G_GNUC_CONST;

ClutterEffect *tidy_multi_blur_effect_new (void);

void tidy_multi_blur_effect_set_blur(ClutterEffect *self, guint blur);
guint tidy_multi_blur_effect_get_blur(ClutterEffect *self);
void tidy_multi_blur_effect_set_zoom(ClutterEffect *self, gfloat zoom);
gfloat tidy_multi_blur_effect_get_zoom(ClutterEffect *self);

G_END_DECLS

#endif /* TIDYMULTIBLUREFFECT_H */
