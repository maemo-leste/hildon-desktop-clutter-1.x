/* Created by Gordon Williams <gordon.williams@collabora.co.uk>
 *
 * This class is able to render all of its children into a buffer, which
 * it can use to speed up rendering, or to continue showing images of its'
 * children after they have been destroyed. */

#include "tidy-cached-group.h"
#include "tidy-util.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <clutter/clutter.h>

#include <cogl/cogl.h>

#include <string.h>
#include <locale.h>

#define TIDY_CACHED_GROUP_DEFAULT_DOWNSAMPLING  2.0

struct _TidyCachedGroupPrivate
{
  /* Internal TidyCachedGroup stuff */
  CoglHandle tex;
  CoglHandle fbo;
  /* When we rendered to this texture, did we render rotated? */
  gboolean rotated;

  gboolean use_alpha; /* whether to use an alpha channel in our textures */

  /* 0 = render as normal ClutterGroup, 1 = render fully cached image */
  float cache_amount;
  /* if anything changed we need to recalculate preblur */
  gboolean source_changed;
  /* how much quality loss you can afford when rendering cached texture */
  float downsample;
};

G_DEFINE_TYPE (TidyCachedGroup,
               tidy_cached_group,
               CLUTTER_TYPE_GROUP);

/* An implementation for the ClutterGroup::paint() vfunc,
   painting all the child actors: */
static void
tidy_cached_group_paint (ClutterActor *actor)
{
  CoglColor    white;
  CoglColor    bgcol;
  CoglColor    col;
  ClutterActorBox box;
  gboolean        rotate_90;

  if (!TIDY_IS_CACHED_GROUP(actor))
    return;

  ClutterGroup *group = CLUTTER_GROUP(actor);
  TidyCachedGroup *container = TIDY_CACHED_GROUP(group);
  TidyCachedGroupPrivate *priv = container->priv;

  clutter_actor_get_allocation_box(actor, &box);

  int width = box.x2 - box.x1;
  int height = box.y2 - box.y1;

  /* If we are rendering normally, or for some reason the size has been
   * set so small we couldn't create a texture then shortcut all this, and
   * just render directly without the texture */
  if (priv->cache_amount < 0.01 ||
      width==0 || height==0)
    {
      /* render direct */
      CLUTTER_ACTOR_CLASS (tidy_cached_group_parent_class)->paint(actor);
      return;
    }

  int exp_width = width/priv->downsample;
  int exp_height = height/priv->downsample;
  int tex_width = 0;
  int tex_height = 0;

  /* check sizes */
  if (priv->tex)
    {
      tex_width = cogl_texture_get_width(priv->tex);
      tex_height = cogl_texture_get_height(priv->tex);
    }
#if RESIZE_TEXTURE
  /* free texture if the size is wrong */
  if (tex_width!=exp_width || tex_height!=exp_height) {
    if (priv->fbo)
      {
        cogl_offscreen_unref(priv->fbo);
        cogl_texture_unref(priv->tex);
        priv->fbo = 0;
        priv->tex = 0;
      }
    priv->source_changed = TRUE;
  }
#endif
  /* create the texture + offscreen buffer if they didn't exist. */
  if (!priv->tex)
    {
      tex_width = exp_width;
      tex_height = exp_height;

      priv->tex = cogl_texture_new_with_size(
                tex_width, tex_height, COGL_TEXTURE_NO_AUTO_MIPMAP,
                priv->use_alpha ? COGL_PIXEL_FORMAT_RGBA_8888 :
                                  COGL_PIXEL_FORMAT_RGB_565);
#ifdef UPSTREAM_DISABLED
      cogl_texture_set_filters(priv->tex, CGL_NEAREST, CGL_NEAREST);
#endif
      priv->fbo = cogl_offscreen_new_to_texture (priv->tex);
    }
  /* It may be that we have resized, but the texture has not.
   * If so, try and keep screen looking 'nice' by rotating so that
   * we don't have a texture that is totally the wrong aspect ratio */
  rotate_90 = (tex_width > tex_height) != (width > height);
  /* If rotation has changed, trigger a redraw */
  if (priv->rotated != rotate_90)
    {
      priv->rotated = rotate_90;
      priv->source_changed = TRUE;
    }

  /* Draw children into an offscreen buffer */
  if (priv->source_changed)
    {
      cogl_push_matrix();
      tidy_util_cogl_push_offscreen_buffer(priv->fbo);
      /* translate a bit to let bilinear filter smooth out intermediate pixels */
      cogl_translate(1.0/2,1.0/2,0);
      if (rotate_90) {
        cogl_scale(1.0*tex_width/height, 1.0*tex_height/width, 1.0); /* FIXME */
        cogl_translate(1.0*height/2, 1.0*width/2, 0); /* FIXME */
        cogl_rotate(90, 0, 0, 1);
        cogl_translate(-1.0*width/2, -1.0*height/2, 0);
      } else {
        cogl_scale(1.0*tex_width/width, 1.0*tex_height/height, 1.0);
      }

      cogl_color_init_from_4ub(&white, 0xff, 0xff, 0xff, 0xff);
      cogl_color_init_from_4ub(&bgcol, 0x00, 0x00, 0x00, 0xff);

      cogl_clear(&bgcol, COGL_BUFFER_BIT_COLOR);
      cogl_set_source_color (&white);

      CLUTTER_ACTOR_CLASS (tidy_cached_group_parent_class)->paint(actor);

      tidy_util_cogl_pop_offscreen_buffer();
      cogl_pop_matrix();

      priv->source_changed = FALSE;
    }

  /* Render what we've blurred to the screen */
  cogl_color_init_from_4ub(&col, 0xff, 0xff, 0xff,
                      clutter_actor_get_paint_opacity (actor));

  /* if cache_amount isn't 1, we merge the two images by rendering the
   * real one first, then rendering the other one after... */
  if (priv->cache_amount < 0.99)
    {
      cogl_set_source_color (&white);
      /* And we must render ourselves properly so we can render
       * the blur over the top */
      cogl_push_matrix();
      CLUTTER_ACTOR_CLASS (tidy_cached_group_parent_class)->paint(actor);
      cogl_pop_matrix();
      cogl_color_set_alpha(&col, priv->cache_amount);
    }

  /* Now we render the image we have... */
  cogl_set_source_color (&col);

  if (rotate_90)
    {
      cogl_push_matrix();
      cogl_translate(1.0*width/2, 1.0*height/2, 0);
      cogl_rotate(90, 0, 0, 1);
      cogl_scale(-1.0*height/width, -1.0*width/height, 1.0);
      cogl_translate(-1.0*width/2, -1.0*height/2, 0);
    }

  cogl_set_source_texture (priv->tex);
  cogl_rectangle_with_texture_coords  (
                          0, 0, width, height,
                          0, 0, 1.0, 1.0);
  if (rotate_90)
    {
      cogl_pop_matrix();
    }
}

static void
tidy_cached_group_dispose (GObject *gobject)
{
  TidyCachedGroup *container = TIDY_CACHED_GROUP(gobject);
  TidyCachedGroupPrivate *priv = container->priv;

  if (priv->fbo)
    {
      cogl_offscreen_unref(priv->fbo);
      cogl_texture_unref(priv->tex);
      priv->fbo = 0;
      priv->tex = 0;
    }

  G_OBJECT_CLASS (tidy_cached_group_parent_class)->dispose (gobject);
}

static void
tidy_cached_group_class_init (TidyCachedGroupClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TidyCachedGroupPrivate));

  gobject_class->dispose = tidy_cached_group_dispose;

  /* Provide implementations for ClutterActor vfuncs: */
  actor_class->paint = tidy_cached_group_paint;
}

static void
tidy_cached_group_init (TidyCachedGroup *self)
{
  TidyCachedGroupPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   TIDY_TYPE_CACHED_GROUP,
                                                   TidyCachedGroupPrivate);
  priv->cache_amount = 0;
  priv->downsample = TIDY_CACHED_GROUP_DEFAULT_DOWNSAMPLING;
  priv->use_alpha = FALSE;
  priv->source_changed = TRUE;

  priv->tex = 0;
  priv->fbo = 0;
}

/*
 * Public API
 */

/**
 * tidy_cached_group_new:
 *
 * Creates a new render container
 *
 * Return value: the newly created #TidyCachedGroup
 */
ClutterActor *
tidy_cached_group_new (void)
{
  return g_object_new (TIDY_TYPE_CACHED_GROUP, NULL);
}

/**
  Set how much of the cached image to render.
  0 == just render normally, without cacheing
  0.5 == render half cached image, half normal image
  1 == render entitely the cached image
 */
void tidy_cached_group_set_render_cache(ClutterActor *cached_group, float amount)
{
  TidyCachedGroupPrivate *priv;

  if (!TIDY_IS_CACHED_GROUP(cached_group))
    return;

  priv = TIDY_CACHED_GROUP(cached_group)->priv;

  if (amount<0) amount=0;
  if (amount>1) amount=1;

  if (priv->cache_amount != amount)
    {
      priv->cache_amount = amount;
      if (clutter_actor_is_visible(cached_group))
        clutter_actor_queue_redraw(cached_group);
    }
}

/* How much to downsample when caching.  Trades rendering speed for quality.
 * If $downsample is 0 resets the default. */
void tidy_cached_group_set_downsampling_factor(ClutterActor *cached_group,
                                               float downsample)
{
  g_return_if_fail(downsample >= 0);
  TIDY_CACHED_GROUP(cached_group)->priv->downsample =
    downsample ? : TIDY_CACHED_GROUP_DEFAULT_DOWNSAMPLING;
}

/**
 * Notifies the group that it needs to update what it has cached
 */
void tidy_cached_group_changed(ClutterActor *cached_group)
{
  TidyCachedGroupPrivate *priv;

  if (!TIDY_IS_CACHED_GROUP(cached_group))
    return;

  priv = TIDY_CACHED_GROUP(cached_group)->priv;
  priv->source_changed = TRUE;
}


