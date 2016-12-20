#define CLUTTER_ENABLE_EXPERIMENTAL_API
#define COGL_ENABLE_EXPERIMENTAL_API

#include <clutter/clutter.h>
#include <cogl/cogl.h>

#include "tidy-multi-blur-effect.h"

#include <string.h>

#define TIDY_MULTI_BLUR_EFFECT_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), TIDY_TYPE_MULTI_BLUR_EFFECT, TidyMultiBlurEffectClass))
#define TIDY_IS_MULTI_BLUR_EFFECT_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), TIDY_TYPE_MULTI_BLUR_EFFECT))
#define TIDY_MULTI_BLUR_EFFECT_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), TIDY_TYPE_MULTI_BLUR_EFFECT, TidyMultiBlurEffectClass))

static const gchar *blur_glsl_vertex_declarations =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#define MEDIUMP mediump\n"
    "#define LOWP lowp\n"
    "#else\n"
    "#define MEDIUMP\n"
    "#define LOWP\n"
    "#endif\n"
    "uniform vec2 blur;\n"
    "\n"
    "/* Outputs to the fragment shader */\n"
    "varying MEDIUMP vec2 tex_coord;\n"
    "varying MEDIUMP vec2 tex_coord_a;\n"
    "varying MEDIUMP vec2 tex_coord_b;\n";

static const gchar *blur_glsl_vertext_shader =
    "tex_coord = cogl_tex_coord0_out.st / cogl_tex_coord0_out.q;\n"
    "tex_coord_a = tex_coord - blur;\n"
    "tex_coord_b = tex_coord + blur;\n";

static const gchar *blur_glsl_texture_declarations =
    "#ifdef GL_ES\n"
    "precision mediump float;\n"
    "#define MEDIUMP mediump\n"
    "#define LOWP lowp\n"
    "#else\n"
    "#define MEDIUMP\n"
    "#define LOWP\n"
    "#endif\n"
    "varying MEDIUMP vec2 tex_coord;\n"
    "varying MEDIUMP vec2 tex_coord_a;\n"
    "varying MEDIUMP vec2 tex_coord_b;\n";

static const gchar *blur_glsl_texture_shader =
    "LOWP vec4 color =\n"
    "       texture2D (cogl_sampler, vec2(tex_coord_a.x, tex_coord_a.y)) * 0.125 + \n"
    "       texture2D (cogl_sampler, vec2(tex_coord_a.x, tex_coord_b.y)) * 0.125 + \n"
    "       texture2D (cogl_sampler, vec2(tex_coord_b.x, tex_coord_b.y)) * 0.125 + \n"
    "       texture2D (cogl_sampler, vec2(tex_coord_b.x, tex_coord_a.y)) * 0.125 + \n"
    "       texture2D (cogl_sampler, vec2(cogl_tex_coord0_in.x, cogl_tex_coord0_in.y)) * 0.5; \n"
    "cogl_texel = color;\n";

struct _TidyMultiBlurEffect
{
  ClutterOffscreenEffect parent_instance;

  /* a back pointer to our actor, so that we can query it */
  ClutterActor *actor;

  gint tex_width;
  gint tex_height;

  CoglPipeline *pipeline;

  CoglPipeline *shader_pipeline;

  gint blur_uniform;

  CoglHandle tex[2];
  CoglHandle fb[2];
  int fb_index;

  guint blur;
  guint current_blur;
  guint max_blur;
  gfloat zoom;
};

struct _TidyMultiBlurEffectClass
{
  ClutterOffscreenEffectClass parent_class;

  CoglPipeline *base_pipeline;
  CoglPipeline *shader_pipeline;
};

G_DEFINE_TYPE (TidyMultiBlurEffect,
               tidy_multi_blur_effect,
               CLUTTER_TYPE_OFFSCREEN_EFFECT)

static void
tidy_multi_blur_effect_create_textures(ClutterOffscreenEffect *effect,
                                       gfloat width, gfloat height)
{
  TidyMultiBlurEffect *self = TIDY_MULTI_BLUR_EFFECT (effect);

  if (self->fb[0])
    {
      cogl_object_unref(self->fb[0]);
      cogl_object_unref(self->tex[0]);
    }
  if (self->fb[1])
    {
      cogl_object_unref(self->fb[1]);
      cogl_object_unref(self->tex[1]);
    }

  self->tex[0] =
      clutter_offscreen_effect_create_texture (effect, width, height);
  self->tex[1] =
      clutter_offscreen_effect_create_texture (effect, width, height);
  self->fb[0] = cogl_offscreen_new_with_texture (self->tex[0]);
  self->fb[1] = cogl_offscreen_new_with_texture (self->tex[1]);
}


static gboolean
tidy_multi_blur_effect_pre_paint (ClutterEffect *effect)
{
  TidyMultiBlurEffect *self = TIDY_MULTI_BLUR_EFFECT (effect);
  ClutterEffectClass *parent_class;

  if (!clutter_actor_meta_get_enabled (CLUTTER_ACTOR_META (effect)))
    return FALSE;

  self->actor = clutter_actor_meta_get_actor (CLUTTER_ACTOR_META (effect));
  if (self->actor == NULL)
    return FALSE;

  if (!clutter_feature_available (CLUTTER_FEATURE_SHADERS_GLSL))
    {
      /* if we don't have support for GLSL shaders then we
       * forcibly disable the ActorMeta
       */
      g_warning ("Unable to use the ShaderEffect: the graphics hardware "
                 "or the current GL driver does not implement support "
                 "for the GLSL shading language.");
      clutter_actor_meta_set_enabled (CLUTTER_ACTOR_META (effect), FALSE);
      return FALSE;
    }

  parent_class = CLUTTER_EFFECT_CLASS (tidy_multi_blur_effect_parent_class);
  if (parent_class->pre_paint (effect))
    {
      ClutterOffscreenEffect *offscreen_effect =
          CLUTTER_OFFSCREEN_EFFECT (effect);
      CoglHandle texture;
      gfloat blur[2];
      gfloat tex_width, tex_height;

      texture = clutter_offscreen_effect_get_texture (offscreen_effect);
      tex_width = cogl_texture_get_width (texture);
      tex_height = cogl_texture_get_height (texture);

      if (!self->tex[0] || self->tex_width != tex_width ||
          self->tex_height != tex_height)
        {
          tidy_multi_blur_effect_create_textures(offscreen_effect,
                                                    tex_width / 2,
                                                    tex_height / 2);
        }

      self->tex_width = tex_width;
      self->tex_height = tex_height;

      blur[0] = 1.0f / self->tex_width;
      blur[1] = 1.0f / self->tex_height;

      cogl_pipeline_set_uniform_float (self->shader_pipeline,
                                       self->blur_uniform,
                                       2, /* n_components */
                                       1, /* count */
                                       blur);

      cogl_pipeline_set_layer_texture (self->pipeline, 0, texture);

      return TRUE;
    }
  else
    return FALSE;
}

static void
tidy_multi_blur_effect_do_blur(ClutterOffscreenEffect *effect, gint steps)
{
  TidyMultiBlurEffect *self = TIDY_MULTI_BLUR_EFFECT (effect);
  gboolean invert = steps % 2;
  gfloat x2 = invert ? 1.0 : -1.0;
  gfloat y2 = invert ? -1.0 : 1.0;

  while (steps--)
    {
      cogl_framebuffer_draw_rectangle (self->fb[self->fb_index],
                                       self->shader_pipeline,
                                        -1.0, x2,
                                        1.0, y2);

      cogl_pipeline_set_layer_texture (self->shader_pipeline, 0,
                                       self->tex[self->fb_index]);
      self->fb_index = (self->fb_index + 1) % 2;
    }
}

static void
tidy_multi_blur_effect_vignette(gfloat width, gfloat height, gint opacity,
                                gfloat zoom)
{
  gfloat t = (1.0f - zoom) / (2.0f * zoom);
  gfloat w = t * width;
  gfloat h = t * height;
  CoglColor white;
  CoglColor fade;
  CoglTextureVertex vertices[6];
  size_t i;

  for (i = 0; i < sizeof(vertices); i++)
      vertices[0].z = 0.0f;

  cogl_color_init_from_4f (&white, 1.0, 1.0, 1.0, opacity / 255.0);
  cogl_color_premultiply (&white);
  cogl_color_init_from_4f (&fade, 1.0, 1.0, 1.0, 0.0);
  cogl_color_premultiply (&fade);

  /* If you feel brave, make a loop out me */
  vertices[0].x = 0.0f; vertices[0].y = height;
  vertices[0].tx = 0.0f; vertices[0].ty = 1.0f;
  vertices[0].color = white;

  vertices[1].x = width; vertices[1].y = height;
  vertices[1].tx = 1.0f; vertices[1].ty = 1.0f;
  vertices[1].color = white;

  vertices[2].x = width + w; vertices[2].y = height + h;
  vertices[2].tx = 1.0f + t; vertices[2].ty = 1.0f + t;
  vertices[2].color = fade;

  vertices[3].x = -w; vertices[3].y = height + h;
  vertices[3].tx = -t; vertices[3].ty = 1 + t;
  vertices[3].color = fade;

  vertices[4].x = -w; vertices[4].y = -h;
  vertices[4].tx = -t; vertices[4].ty = -t;
  vertices[4].color = fade;

  vertices[5].x = 0.0f; vertices[5].y = 0.0f;
  vertices[5].tx = 0.0f; vertices[5].ty = 0.0f;
  vertices[5].color = white;

  cogl_polygon (vertices, 6, TRUE);

  vertices[0].x = width; vertices[0].y = 0.0f;
  vertices[0].tx = 1.0f; vertices[0].ty = 0.0f;
  vertices[0].color = white;

  vertices[1].x = 0.0f; vertices[1].y = 0.0f;
  vertices[1].tx = 0.0f; vertices[1].ty = 0.0f;
  vertices[1].color = white;

  vertices[2].x = -w; vertices[2].y = -h;
  vertices[2].tx = -t; vertices[2].ty = -t;
  vertices[2].color = fade;

  vertices[3].x = width + w; vertices[3].y = -h;
  vertices[3].tx = 1.0f + t; vertices[3].ty = -t;
  vertices[3].color = fade;

  vertices[4].x = width + w; vertices[4].y = height + h;
  vertices[4].tx = 1.0f + t; vertices[4].ty = 1.0f + t;
  vertices[4].color = fade;

  vertices[5].x = width; vertices[5].y = height;
  vertices[5].tx = 1.0f; vertices[5].ty = 1.0f;
  vertices[5].color = white;

  cogl_polygon (vertices, 6, TRUE);
}

static void
tidy_multi_blur_effect_paint_target (ClutterOffscreenEffect *effect)
{
  TidyMultiBlurEffect *self = TIDY_MULTI_BLUR_EFFECT (effect);
  guint8 paint_opacity = clutter_actor_get_paint_opacity (self->actor);
  CoglPipeline *pipeline;

  if (self->blur)
    {
      if (self->blur > self->current_blur)
        {
          guint steps = self->blur - self->current_blur;

          if (!self->current_blur)
            {
              cogl_pipeline_set_layer_texture (self->shader_pipeline, 0,
                                               self->tex[self->fb_index]);

              /* draw offscreen texture to fb */
              cogl_framebuffer_draw_rectangle (self->fb[self->fb_index],
                                               self->pipeline,
                                               -1.0, 1.0,
                                               1.0, -1.0);
              steps--;
              self->fb_index = (self->fb_index + 1) % 2;
            }

          tidy_multi_blur_effect_do_blur(effect, steps);
          self->current_blur = self->blur;
        }

      pipeline = self->shader_pipeline;
    }
  else
    pipeline = self->pipeline;

  ClutterActorBox box;

  gfloat width = self->tex_width;
  gfloat height = self->tex_height;

  gfloat zoom_factor = (1.0f - self->zoom) / 2.0f;

  clutter_actor_get_allocation_box(self->actor, &box);

  cogl_pipeline_set_color4ub (pipeline, paint_opacity, paint_opacity,
                              paint_opacity, paint_opacity);
  cogl_push_matrix();

  cogl_translate(width * zoom_factor, height * zoom_factor, 0);
  cogl_scale(self->zoom, self->zoom, 0.0f);

  cogl_clip_push_window_rectangle(box.x1, box.y1,
                                  box.x2 - box.x1, box.y2 - box.y1);

  cogl_push_source (pipeline);
  cogl_rectangle_with_texture_coords(0, 0, width, height, 0, 0, 1, 1);

  /* If we're zooming less than 1, we want to re-render everything
   * mirrored around each edge.
   */
  if (self->zoom < 1.0f)
      tidy_multi_blur_effect_vignette(width, height, paint_opacity,
                                      self->zoom);

  cogl_pop_source ();
  cogl_clip_pop();
  cogl_pop_matrix();
}

static void
tidy_multi_blur_effect_dispose (GObject *gobject)
{
  TidyMultiBlurEffect *self = TIDY_MULTI_BLUR_EFFECT (gobject);

  if (self->pipeline != NULL)
    {
      cogl_object_unref (self->pipeline);
      self->pipeline = NULL;
    }

  if (self->shader_pipeline != NULL)
    {
      cogl_object_unref (self->shader_pipeline);
      self->shader_pipeline = NULL;
    }

  if (self->fb[0])
    {
      cogl_object_unref(self->fb[0]);
      cogl_object_unref(self->tex[0]);
      self->fb[0] = NULL;
      self->tex[0] = NULL;
    }
  if (self->fb[1])
    {
      cogl_object_unref(self->fb[1]);
      cogl_object_unref(self->tex[1]);
      self->fb[1] = NULL;
      self->tex[1] = NULL;
    }

  G_OBJECT_CLASS (tidy_multi_blur_effect_parent_class)->dispose (gobject);
}

static void
tidy_multi_blur_effect_class_init (TidyMultiBlurEffectClass *klass)
{
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterOffscreenEffectClass *offscreen_class;

  gobject_class->dispose = tidy_multi_blur_effect_dispose;

  effect_class->pre_paint = tidy_multi_blur_effect_pre_paint;

  offscreen_class = CLUTTER_OFFSCREEN_EFFECT_CLASS (klass);
  offscreen_class->paint_target = tidy_multi_blur_effect_paint_target;
}

static void
tidy_multi_blur_effect_init (TidyMultiBlurEffect *self)
{
  TidyMultiBlurEffectClass *klass =
      TIDY_MULTI_BLUR_EFFECT_GET_CLASS (self);

  if (G_UNLIKELY (klass->base_pipeline == NULL))
    {
      CoglSnippet *snippet;
      CoglContext *ctx =
        clutter_backend_get_cogl_context (clutter_get_default_backend ());

      /* pipeline with blur shaders */
      klass->shader_pipeline = cogl_pipeline_new (ctx);

      /* vertex shader */
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_VERTEX,
                                  blur_glsl_vertex_declarations,
                                  blur_glsl_vertext_shader);
      cogl_pipeline_add_snippet (klass->shader_pipeline, snippet);
      cogl_object_unref (snippet);

      /* texture lookup shader */
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  blur_glsl_texture_declarations,
                                  NULL);

      cogl_snippet_set_replace (snippet, blur_glsl_texture_shader);

      cogl_pipeline_add_layer_snippet (klass->shader_pipeline, 0, snippet);
      cogl_object_unref (snippet);

      cogl_pipeline_set_layer_null_texture (klass->shader_pipeline,
                                            0, /* layer number */
                                            COGL_TEXTURE_TYPE_2D);
      cogl_pipeline_set_layer_filters (klass->shader_pipeline,
                                       0, /* layer_index */
                                       COGL_MATERIAL_FILTER_LINEAR,
                                       COGL_MATERIAL_FILTER_LINEAR);
      cogl_pipeline_set_layer_wrap_mode (klass->shader_pipeline, 0,
                                         COGL_PIPELINE_WRAP_MODE_MIRRORED_REPEAT);

      /* pipeline with no shaders */
      klass->base_pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_null_texture (klass->base_pipeline,
                                            0, /* layer number */
                                            COGL_TEXTURE_TYPE_2D);
      cogl_pipeline_set_layer_wrap_mode (klass->base_pipeline, 0,
                                         COGL_PIPELINE_WRAP_MODE_MIRRORED_REPEAT);
    }

  self->pipeline = cogl_pipeline_copy (klass->base_pipeline);

  self->shader_pipeline = cogl_pipeline_copy (klass->shader_pipeline);
  self->blur_uniform =
      cogl_pipeline_get_uniform_location (self->shader_pipeline, "blur");

  self->blur = 0;
  self->current_blur = 0;
  self->zoom = 1.0f;
}

ClutterEffect *
tidy_multi_blur_effect_new (void)
{
  return g_object_new (TIDY_TYPE_MULTI_BLUR_EFFECT, NULL);
}

void
tidy_multi_blur_effect_set_blur(ClutterEffect *effect, guint blur)
{
  TidyMultiBlurEffect *self;

  if (!TIDY_IS_MULTI_BLUR_EFFECT(effect))
    return;

  self = TIDY_MULTI_BLUR_EFFECT(effect);

  if (self->blur != blur)
    {
      if (self->blur > blur)
          self->current_blur = 0;

      self->blur = blur;
      clutter_effect_queue_repaint (effect);
    }
}

guint
tidy_multi_blur_effect_get_blur(ClutterEffect *self)
{
  if (!TIDY_IS_MULTI_BLUR_EFFECT(self))
      return 0;

  return TIDY_MULTI_BLUR_EFFECT(self)->blur;
}

void
tidy_multi_blur_effect_set_zoom(ClutterEffect *self, gfloat zoom)
{
  if (!TIDY_IS_MULTI_BLUR_EFFECT(self))
    return;

  if (TIDY_MULTI_BLUR_EFFECT(self)->zoom != zoom)
    {
      TIDY_MULTI_BLUR_EFFECT(self)->zoom = zoom;
      clutter_effect_queue_repaint (self);
    }
}

gfloat
tidy_multi_blur_effect_get_zoom(ClutterEffect *self)
{
  if (!TIDY_IS_MULTI_BLUR_EFFECT(self))
      return 0;

  return TIDY_MULTI_BLUR_EFFECT(self)->zoom;
}
