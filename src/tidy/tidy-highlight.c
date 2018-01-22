/* Created by Gordon Williams <gordon.williams@collabora.co.uk>
 *
 * This blurs part of a texture */

#define CLUTTER_ENABLE_EXPERIMENTAL_API
#define COGL_ENABLE_EXPERIMENTAL_API

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "tidy-highlight.h"
#include <clutter/clutter.h>

#include "cogl/cogl.h"

enum
{
  PROP_0,
  PROP_PARENT_TEXTURE,
};

struct _TidyHighlight
{
  ClutterActor          parent;

  /*< priv >*/
  TidyHighlightPrivate *priv;
};

struct _TidyHighlightClass
{
  ClutterActorClass parent_class;

  CoglPipeline *base_pipeline;
};


struct _TidyHighlightPrivate
{
  ClutterTexture *texture;
  CoglPipeline   *pipeline;
  gint            blurx_uniform;
  gint            blury_uniform;

  float           amount;
  ClutterColor    color;
};

G_DEFINE_TYPE (TidyHighlight, tidy_highlight, CLUTTER_TYPE_ACTOR);

#define CLUTTER_HIGHLIGHT_GET_PRIVATE(obj) \
(G_TYPE_INSTANCE_GET_PRIVATE ((obj), TIDY_TYPE_HIGHLIGHT, TidyHighlightPrivate))

static const gchar *tidy_highlight_glsl_declarations =
  "#ifdef GL_ES\n"
  "precision lowp float;\n"
  "#define MEDIUMP mediump\n"
  "#define LOWP lowp\n"
  "#else\n"
  "#define MEDIUMP\n"
  "#define LOWP\n"
  "#endif\n"

  "#define tex cogl_sampler\n"
  "#define tex_coord cogl_tex_coord.st\n"

  "uniform MEDIUMP float blurx;\n"
  "uniform MEDIUMP float blury;\n";

static const gchar *tidy_highlight_glsl_shader =
  "  LOWP float alpha =\n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*1.0000, tex_coord.y + blury*0.0000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.8660, tex_coord.y + blury*0.5000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.5000, tex_coord.y + blury*0.8660)).a *0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.0000, tex_coord.y + blury*1.0000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*-0.5000, tex_coord.y + blury*0.8660)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*-0.8660, tex_coord.y + blury*0.5000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*-1.0000, tex_coord.y + blury*0.0000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*-0.8660, tex_coord.y + blury*-0.5000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*-0.5000, tex_coord.y + blury*-0.8660)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*-0.0000, tex_coord.y + blury*-1.0000)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.5000, tex_coord.y + blury*-0.8661)).a * 0.0675 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.8660, tex_coord.y + blury*-0.5000)).a * 0.0675; + \n"
  "    texture2D (tex, vec2(tex_coord.x - blurx*0.3, tex_coord.y - blury*0.3)).a * 0.125 + \n"
  "    texture2D (tex, vec2(tex_coord.x - blurx*0.3, tex_coord.y + blury*0.3)).a * 0.125 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.3, tex_coord.y + blury*0.3)).a * 0.125 + \n"
  "    texture2D (tex, vec2(tex_coord.x + blurx*0.3, tex_coord.y - blury*0.3)).a * 0.125; \n"

  "  cogl_texel= alpha * vec4(4.0);\n";

static void
tidy_highlight_get_preferred_width (ClutterActor *self, gfloat for_height,
                                    gfloat *min_width_p,
                                    gfloat *natural_width_p)
{
  TidyHighlightPrivate *priv = TIDY_HIGHLIGHT(self)->priv;
  ClutterActor *texture = CLUTTER_ACTOR (priv->texture);
  ClutterActorClass *texture_class;

  /* Note that by calling the get_width_request virtual method directly
   * and skipping the clutter_actor_get_preferred_width() wrapper, we
   * are ignoring any size request override set on the parent texture
   * and just getting the normal size of the parent texture.
   */
  if (!texture)
    {
      if (min_width_p)
        *min_width_p = 0;

      if (natural_width_p)
        *natural_width_p = 0;

      return;
    }

  texture_class = CLUTTER_ACTOR_GET_CLASS (texture);
  texture_class->get_preferred_width (texture, for_height, min_width_p,
                                      natural_width_p);
}

static void
tidy_highlight_get_preferred_height (ClutterActor *self, gfloat for_width,
                                     gfloat *min_height_p,
                                     gfloat *natural_height_p)
{
  TidyHighlightPrivate *priv = TIDY_HIGHLIGHT (self)->priv;
  ClutterActor *texture = CLUTTER_ACTOR (priv->texture);
  ClutterActorClass *texture_class;

  /* Note that by calling the get_height_request virtual method directly
   * and skipping the clutter_actor_get_preferred_height() wrapper, we
   * are ignoring any size request override set on the parent texture and
   * just getting the normal size of the parent texture.
   */
  if (!texture)
    {
      if (min_height_p)
        *min_height_p = 0;

      if (natural_height_p)
        *natural_height_p = 0;

      return;
    }

  texture_class = CLUTTER_ACTOR_GET_CLASS (texture);
  texture_class->get_preferred_height (texture, for_width, min_height_p,
                                       natural_height_p);
}

static void
set_texture (TidyHighlight *self, ClutterTexture *texture)
{
  TidyHighlightPrivate *priv = self->priv;
  ClutterActor *actor = CLUTTER_ACTOR (self);
  gboolean was_visible = clutter_actor_is_visible (CLUTTER_ACTOR(self));

  cogl_pipeline_set_layer_null_texture (priv->pipeline,
                                        0, /* layer number */
                                        COGL_TEXTURE_TYPE_2D);
  if (priv->texture)
    {
      g_object_unref (priv->texture);
      priv->texture = NULL;

      if (was_visible)
        clutter_actor_hide (actor);
    }

  if (texture)
    {
      priv->texture = g_object_ref (texture);
      cogl_pipeline_set_layer_texture (
            priv->pipeline, 0, clutter_texture_get_cogl_texture(priv->texture));

      /* queue a redraw if the subd texture is already visible */
      if (clutter_actor_is_visible (CLUTTER_ACTOR(priv->texture)) &&
          was_visible)
        {
          clutter_actor_show (actor);
          clutter_actor_queue_redraw (actor);
        }

      clutter_actor_queue_relayout (actor);
    }
}

static void
tidy_highlight_dispose (GObject *object)
{
  TidyHighlight *self = TIDY_HIGHLIGHT(object);
  TidyHighlightPrivate *priv = self->priv;

  if (priv->texture)
    {
      g_object_unref (priv->texture);
      priv->texture = NULL;
    }

  if (priv->pipeline != NULL)
    {
      cogl_object_unref (priv->pipeline);
      priv->pipeline = NULL;
    }

  G_OBJECT_CLASS (tidy_highlight_parent_class)->dispose (object);
}

static void
tidy_highlight_finalize (GObject *object)
{
  G_OBJECT_CLASS (tidy_highlight_parent_class)->finalize (object);
}

static void
tidy_highlight_set_property (GObject *object, guint prop_id,
                             const GValue *value, GParamSpec *pspec)
{
  TidyHighlight *ctexture = TIDY_HIGHLIGHT (object);

  switch (prop_id)
    {
      case PROP_PARENT_TEXTURE:
          set_texture (ctexture, g_value_get_object (value));
          break;
      default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
    }
}

static void
tidy_highlight_get_property (GObject *object, guint prop_id, GValue *value,
                             GParamSpec *pspec)
{
  TidyHighlight *ctexture = TIDY_HIGHLIGHT (object);

  switch (prop_id)
    {
    case PROP_PARENT_TEXTURE:
      g_value_set_object (value, ctexture->priv->texture);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
tidy_highlight_paint_node (ClutterActor *actor, ClutterPaintNode *root)
{
  ClutterPaintNode *shader_node;
  ClutterActorBox box, tex_box;
  gfloat width, height;
  gfloat blurx, blury;
  gfloat alpha = (gfloat)clutter_actor_get_paint_opacity (actor) / 255.0;
  TidyHighlightPrivate *priv = TIDY_HIGHLIGHT(actor)->priv;
  CoglHandle tex = cogl_pipeline_get_layer_texture(priv->pipeline, 0);
  CoglColor color;

  clutter_actor_get_allocation_box (actor, &box);
  clutter_actor_get_allocation_box (CLUTTER_ACTOR(priv->texture), &tex_box);

  width = cogl_texture_get_width(tex);
  height = cogl_texture_get_height(tex);
  blurx = priv->amount / width;
  blury = priv->amount / height;

  cogl_pipeline_set_uniform_1f (priv->pipeline, priv->blurx_uniform,
                                blurx / 128.0);
  cogl_pipeline_set_uniform_1f (priv->pipeline, priv->blury_uniform,
                                blury / 128.0);

  box.x1 = tex_box.x1 - box.x1;
  box.y1 = tex_box.y1 - box.y1;
  box.x2 = box.x1 + width;
  box.y2 = box.y1 + height;

  cogl_color_init_from_4ub (&color, priv->color.red, priv->color.green,
                            priv->color.blue, priv->color.alpha * alpha);

  cogl_pipeline_set_color(priv->pipeline, &color);

  shader_node = clutter_pipeline_node_new (priv->pipeline);
  clutter_paint_node_add_rectangle (shader_node, &box);
  clutter_paint_node_add_child (root, shader_node);
  clutter_paint_node_unref (shader_node);
}

static void
tidy_highlight_class_init (TidyHighlightClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->get_preferred_width = tidy_highlight_get_preferred_width;
  actor_class->get_preferred_height = tidy_highlight_get_preferred_height;
  actor_class->paint_node = tidy_highlight_paint_node;

  gobject_class->finalize     = tidy_highlight_finalize;
  gobject_class->dispose      = tidy_highlight_dispose;
  gobject_class->set_property = tidy_highlight_set_property;
  gobject_class->get_property = tidy_highlight_get_property;

  g_object_class_install_property(gobject_class, PROP_PARENT_TEXTURE,
                               g_param_spec_object ("parent-texture",
                                                    "Parent Texture",
                                                    "The parent texture to sub",
                                                    CLUTTER_TYPE_TEXTURE,
                                                    G_PARAM_READABLE |
                                                    G_PARAM_WRITABLE |
                                                    G_PARAM_STATIC_NAME |
                                                    G_PARAM_STATIC_NICK |
                                                    G_PARAM_STATIC_BLURB));

  g_type_class_add_private (gobject_class, sizeof (TidyHighlightPrivate));
}

static void
tidy_highlight_init (TidyHighlight *self)
{
  TidyHighlightClass *klass = TIDY_HIGHLIGHT_GET_CLASS (self);
  TidyHighlightPrivate *priv;
  ClutterColor white = {0xFF, 0xFF, 0xFF, 0xFF};

  self->priv = priv = CLUTTER_HIGHLIGHT_GET_PRIVATE (self);
  priv->texture = NULL;
  priv->amount = 0;
  priv->color = white;

  if (G_UNLIKELY (klass->base_pipeline == NULL))
    {
      CoglSnippet *snippet;
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      klass->base_pipeline = cogl_pipeline_new (ctx);

      /* texture shader */
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  tidy_highlight_glsl_declarations,
                                  NULL);
      cogl_snippet_set_replace (snippet, tidy_highlight_glsl_shader);

      cogl_pipeline_add_layer_snippet (klass->base_pipeline, 0, snippet);
      cogl_object_unref (snippet);

      cogl_pipeline_set_layer_null_texture (klass->base_pipeline,
                                            0, /* layer number */
                                            COGL_TEXTURE_TYPE_2D);
      cogl_pipeline_set_layer_filters(klass->base_pipeline,
                                      0, /* layer number */
                                      COGL_PIPELINE_FILTER_LINEAR,
                                      COGL_PIPELINE_FILTER_LINEAR);
      cogl_pipeline_set_layer_wrap_mode(klass->base_pipeline,
                                        0, /* layer number */
                                        COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
    }

  priv->pipeline = cogl_pipeline_copy (klass->base_pipeline);

  priv->blurx_uniform =
      cogl_pipeline_get_uniform_location (priv->pipeline, "blurx");
  priv->blury_uniform =
    cogl_pipeline_get_uniform_location (priv->pipeline, "blury");
}

/**
 * tidy_highlight_new:
 * @texture: a #ClutterTexture, or %NULL
 *
 * Creates an efficient 'sub' of a pre-existing texture with which it
 * shares the underlying pixbuf data.
 *
 * You can use tidy_highlight_set_texture() to change the
 * subd texture.
 *
 * Return value: the newly created #TidyHighlight
 */
TidyHighlight *
tidy_highlight_new (ClutterTexture *texture)
{
  g_return_val_if_fail (texture == NULL || CLUTTER_IS_TEXTURE (texture), NULL);

  return g_object_new (TIDY_TYPE_HIGHLIGHT, "parent-texture", texture, NULL);
}

void
tidy_highlight_set_amount (TidyHighlight *self, float amount)
{
  g_return_if_fail (TIDY_IS_HIGHLIGHT (self));

  if (amount != self->priv->amount)
    {
      self->priv->amount = amount;
      clutter_actor_queue_redraw(CLUTTER_ACTOR(self));
    }
}

void
tidy_highlight_set_color (TidyHighlight *self, ClutterColor *col)
{
  g_return_if_fail (TIDY_IS_HIGHLIGHT (self));

  self->priv->color = *col;
  clutter_actor_queue_redraw(CLUTTER_ACTOR(self));
}
