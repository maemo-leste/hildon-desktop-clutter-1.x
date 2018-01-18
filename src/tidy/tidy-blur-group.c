/* Created by Gordon Williams <gordon.williams@collabora.co.uk>
 *
 * This class blurs all of its children, also changing saturation and lightness.
 * It renders its children into a half-size texture first, then blurs this into
 * another texture, finally rendering that to the screen. Because of this, when
 * the blurring doesn't change from frame to frame, children and NOT rendered,
 * making this pretty quick. */
#define CLUTTER_ENABLE_EXPERIMENTAL_API
#define COGL_ENABLE_EXPERIMENTAL_API

#include "tidy-blur-group.h"
#include "tidy-util.h"
#include "tidy-blur-effect.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include <string.h>
#include <locale.h>

#include "util/hd-transition.h"

#include <GL/gl.h>

/* #define it something sane */
#define TIDY_IS_SANE_BLUR_GROUP(obj)    ((obj) != NULL)

/* Chequer is 32x32 because that's the smallest SGX will do. If we go smaller
 * it just ends up getting put in a block that size anyway */
#define CHEQUER_SIZE (32)

struct _TidyBlurGroupPrivate
{
  /* Internal TidyBlurGroup stuff */
  CoglHandle tex_chequer; /* chequer texture used for dimming video overlays */

  float saturation; /* 0->1 how much colour there is */
  float brightness; /* 1=normal, 0=black */
  float zoom; /* amount to zoom. 1=normal, 0.5=out, 2=double-size */
  gboolean use_alpha; /* whether to use an alpha channel in our textures */
  gboolean use_mirror; /* whether to mirror the edge of teh blurred texture */
  gboolean chequer; /* whether to chequer pattern the contents -
                       for dimming video overlays */

  /* is the 'blurless desaturation' tweak enabled? */
  gboolean tweaks_blurless;
  /* saturation for blurless (0 no color, 1 full color) */
  float blurless_saturation;

  ClutterEffect *blur_effect;
  ClutterEffect *saturation_effect;
};

/**
 * SECTION:tidy-blur-group
 * @short_description: Pixel-shader modifier class
 *
 * #TidyBlurGroup Renders all of its children to an offscreen buffer,
 * and then renders this buffer to the screen using a pixel shader.
 *
 */

G_DEFINE_TYPE (TidyBlurGroup,
               tidy_blur_group,
               CLUTTER_TYPE_GROUP);

#ifdef UPSTREAM_DISABLED
/* Perform blur without a pixel shader */
static void
tidy_blur_group_fallback_blur(TidyBlurGroup *group, int tex_width, int tex_height)
{
  CoglColor    col;

  TidyBlurGroupPrivate *priv = group->priv;
  CoglHandle tex = priv->current_is_a ? priv->tex_a : priv->tex_b;
  gfloat diffx, diffy;

  diffx = 1.0f / tex_width;
  diffy = 1.0f / tex_height;
  glBlendFunc(GL_ONE, GL_ZERO);

  cogl_color_init_from_4ub(&col, 0xff, 0xff, 0xff, 0xff);

  /* FIXME - do we need to set scr texture for every draw?*/
  cogl_set_source_color (&col);
  cogl_set_source_texture (tex);
  cogl_rectangle_with_texture_coords (0, 0,
                                      tex_width,
                                      tex_height,
                                      -diffx, 0, 1.0-diffx, 1.0);
  glBlendFunc(GL_ONE, GL_ONE);

  cogl_set_source_texture (tex);
  cogl_rectangle_with_texture_coords (
                            0, 0,
                            tex_width,
                            tex_height,
                            0, diffy, 1.0+diffx, 1.0);
  cogl_set_source_texture (tex);
  cogl_rectangle_with_texture_coords (
                            0, 0,
                            tex_width,
                            tex_height,
                            0, -diffy, 1.0, 1.0-diffy);
  cogl_set_source_texture (tex);
  cogl_rectangle_with_texture_coords (
                            0, 0,
                            tex_width,
                            tex_height,
                            0, diffy, 1.0, 1.0+diffy);

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
#endif

/* If priv->chequer, draw a chequer pattern over the screen */
static void
tidy_blur_group_do_chequer(TidyBlurGroup *group, guint width, guint height)
{
  TidyBlurGroupPrivate *priv = group->priv;

  if (!priv->chequer)
    return;

  CoglContext *ctx =
    clutter_backend_get_cogl_context (clutter_get_default_backend ());

  CoglPipeline *pipeline = cogl_pipeline_new (ctx);

  cogl_pipeline_set_color4ub (pipeline, 0x00, 0x00, 0x00, 0xFF);
  cogl_pipeline_set_layer_texture(pipeline, 0, priv->tex_chequer);
  cogl_push_source (pipeline);
  cogl_rectangle_with_texture_coords (0, 0, width, height, 0, 0,
                                      1.0 * width/CHEQUER_SIZE,
                                      1.0 * height/CHEQUER_SIZE);
  cogl_pop_source ();

  g_object_unref (pipeline);
}

static void
tidy_blur_group_dispose (GObject *gobject)
{
  TidyBlurGroup *container = TIDY_BLUR_GROUP(gobject);
  TidyBlurGroupPrivate *priv = container->priv;

  if (priv->tex_chequer)
    {
      cogl_texture_unref(priv->tex_chequer);
      priv->tex_chequer = 0;
    }

  G_OBJECT_CLASS (tidy_blur_group_parent_class)->dispose (gobject);
}

static void
tidy_blur_group_class_init (TidyBlurGroupClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TidyBlurGroupPrivate));
  gobject_class->dispose = tidy_blur_group_dispose;
}

static void
tidy_blur_group_init (TidyBlurGroup *self)
{
  TidyBlurGroupPrivate *priv;
  gint i, x, y;
  guchar dither_data[CHEQUER_SIZE*CHEQUER_SIZE];

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   TIDY_TYPE_BLUR_GROUP,
                                                   TidyBlurGroupPrivate);
  priv->saturation = 1;
  priv->use_alpha = TRUE;
  priv->use_mirror = FALSE;

  priv->tweaks_blurless = hd_transition_get_int("thp_tweaks", "blurless", 0);
  priv->blurless_saturation = hd_transition_get_double("thp_tweaks", "blurless_saturation", 0);

  /* Dimming texture - a 32x32 chequer pattern */
  i=0;
  for (y=0;y<CHEQUER_SIZE;y++)
    for (x=0;x<CHEQUER_SIZE;x++)
      {
        /* A 50:50 chequer pattern:
         * dither_data[i++] = ((x&1) == (y&1)) ? 255 : 0;*/

        /* 25:75 pattern */
        gint d = x + y;
        dither_data[i++] = ((d&3) == 0) ? 0 : 255;
      }
  priv->tex_chequer = cogl_texture_new_from_data(
      CHEQUER_SIZE,
      CHEQUER_SIZE,
      COGL_TEXTURE_NO_AUTO_MIPMAP,
      COGL_PIXEL_FORMAT_A_8,
      COGL_PIXEL_FORMAT_A_8,
      CHEQUER_SIZE,
      dither_data);

  if (!hd_transition_get_int("blur", "turbo", 0))
    {
    if (priv->tweaks_blurless)
      {
      }
    else
      {
        priv->blur_effect = tidy_blur_effect_new();
        clutter_actor_add_effect_with_name (CLUTTER_ACTOR(self), "blur",
                                            priv->blur_effect);
      }
  }
}

/*
 * Public API
 */

/**
 * tidy_blur_group_new:
 *
 * Creates a new render container
 *
 * Return value: the newly created #TidyBlurGroup
 */
ClutterActor *
tidy_blur_group_new (void)
{
  return g_object_new (TIDY_TYPE_BLUR_GROUP, NULL);
}

/**
 * tidy_blur_group_set_chequer:
 *
 * Sets whether to chequer the contents with a 50:50 pattern of black dots
 */
void
tidy_blur_group_set_chequer(ClutterActor *blur_group, gboolean chequer)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (priv->chequer != chequer)
    {
      priv->chequer = chequer;
      if (clutter_actor_is_visible(blur_group))
        clutter_actor_queue_redraw(blur_group);
    }
}

/**
 * tidy_blur_group_set_blur:
 *
 * Sets the amount of blur (in pixels)
 */
void
tidy_blur_group_set_blur(ClutterActor *blur_group, float blur)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
      return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (!priv->blur_effect)
      return;

  tidy_blur_effect_set_blur(priv->blur_effect, blur);
}

/**
 * tidy_blur_group_set_saturation:
 *
 * Sets the saturation (1 = normal, 0=black and white)
 */
void
tidy_blur_group_set_saturation(ClutterActor *blur_group, float saturation)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
      return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (!priv->saturation_effect)
      return;

/*
  if (priv->saturation != saturation)
    {
      priv->saturation = saturation;
      if (clutter_actor_is_visible(blur_group))
        clutter_actor_queue_redraw(blur_group);
    }*/
}

/**
 * tidy_blur_group_set_brightness:
 *
 * Sets the brightness (1 = normal, 0=black)
 */
void
tidy_blur_group_set_brightness(ClutterActor *blur_group, float brightness)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  tidy_blur_effect_set_brigtness(priv->blur_effect, brightness);
}


/**
 * tidy_blur_group_set_zoom:
 *
 * Set how far to zoom in on what has been blurred
 * 1=normal, 0.5=out, 2=double-size
 */
void
tidy_blur_group_set_zoom(ClutterActor *blur_group, float zoom)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  tidy_blur_effect_set_zoom(priv->blur_effect, zoom);
}

/**
 * tidy_blur_group_get_zoom:
 *
 * Get how far to zoom in on what has been blurred
 * 1=normal, 0.5=out, 2=double-size
 */
float
tidy_blur_group_get_zoom(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return 1.0f;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  return tidy_blur_effect_get_zoom(priv->blur_effect);
}

/**
 * tidy_blur_group_set_use_alpha:
 *
 * Sets whether to use an alpha channel in the textures used for blurring.
 * Only useful if we're blurring something transparent
 */
void
tidy_blur_group_set_use_alpha(ClutterActor *blur_group, gboolean alpha)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  priv->use_alpha = alpha;
}

/**
 * tidy_blur_group_set_use_mirror:
 *
 * Sets whether to mirror the blurred texture when it is zoomed out, or just
 * leave the edges dark...
 */
void
tidy_blur_group_set_use_mirror(ClutterActor *blur_group, gboolean mirror)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  priv->use_mirror = mirror;
}

/**
 * tidy_blur_group_set_source_changed:
 *
 * Forces the blur group to update. Only needed at the moment because
 * actor_remove doesn't appear to send notify events
 */
void
tidy_blur_group_set_source_changed(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  clutter_actor_queue_redraw(blur_group);
}

/**
 * tidy_blur_group_hint_source_changed:
 *
 * Notifies the blur group that it needs to update next time it becomes
 * unblurred.
 */
void
tidy_blur_group_hint_source_changed(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;
/*
  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  priv->source_changed = TRUE;*/
}

void
tidy_blur_group_stop_progressing(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  /*priv->skip_progress = TRUE;*/
}

/**
 * tidy_blur_group_source_buffered:
 *
 * Return true if this blur group is currently buffering it's actors. Used
 * when this actually needs to blur or desaturate its children
 */
gboolean
tidy_blur_group_source_buffered(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_SANE_BLUR_GROUP(blur_group))
    return FALSE;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  return !(tidy_blur_effect_get_blur(priv->blur_effect) == 0
           && priv->saturation == 1 && priv->brightness == 1);
}
