/* 
 * Copyright (C) 2012 Tomasz Pieniążek <t.pieniazek@gazeta.pl>
 * Based on tidy-blur-group.c by Gordon Williams <gordon.williams@collabora.co.uk>
 *
 * This class desaturates all of its children. It renders its children into a 
 * half-size texture first, then desaturates this into  another texture, 
 * finally rendering that to the screen. 
 */

#include "tidy-desaturation-group.h"
#include "tidy-util.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <clutter/clutter.h>

#include <cogl/cogl.h>

#include <string.h>
#include <locale.h>

#include "util/hd-transition.h"

/* #define it something sane */
#define TIDY_IS_SANE_DESATURATION_GROUP(obj)    ((obj) != NULL)

/* This fixes the bug where the SGX GLSL compiler uses the current locale for
 * numbers - so '1.0' in a shader will not work when the locale says that ','
 * is a decimal separator.
 */
#define GLSL_LOCALE_FIX 1

/* The OpenGL fragment shader used to do desaturation. */
#if CLUTTER_COGL_HAS_GLES
const char *DESATURATE_VERTEX_SHADER =
  "/* Per vertex attributes */\n"
    "attribute vec4     vertex_attrib;\n"
    "attribute vec4     tex_coord_attrib;\n"
    "attribute vec4     color_attrib;\n"
    "\n"
    "/* Transformation matrices */\n"
    "uniform mat4       modelview_matrix;\n"
    "uniform mat4       mvp_matrix; /* combined modelview and projection matrix */\n"
    "uniform mat4       texture_matrix;\n"
    "uniform mediump float blurx;\n"
    "uniform mediump float blury;\n"
    "\n"
    "/* Outputs to the fragment shader */\n"
    "varying lowp vec4       frag_color;\n"
    "varying mediump vec2    tex_coord;\n"
    "varying mediump vec2    tex_coord_a;\n"
    "varying mediump vec2    tex_coord_b;\n"
    "\n"
    "void\n"
    "main (void)\n"
    "{\n"
    "  gl_Position = mvp_matrix * vertex_attrib;\n"
    "  vec4 transformed_tex_coord = texture_matrix * tex_coord_attrib;\n"
    "  tex_coord = transformed_tex_coord.st / transformed_tex_coord.q;\n"
    "  tex_coord_a = tex_coord - vec2(blurx, blury);\n"
    "  tex_coord_b = tex_coord + vec2(blurx, blury);\n"
    "  frag_color = color_attrib;\n"
  "}\n";
const char *DESATURATE_SATURATE_FRAGMENT_SHADER =
"precision lowp float;\n"
"varying lowp    vec4  frag_color;\n"
"varying mediump vec2  tex_coord;\n"
"uniform lowp sampler2D tex;\n"
"uniform lowp float saturation;\n"
"void main () {\n"
"  lowp vec4 color = frag_color * texture2D (tex, tex_coord);\n"
"  lowp float lightness = (color.r+color.g+color.b)*0.333*(1.0-saturation); \n"
"  gl_FragColor = vec4(\n"
"                      color.r*saturation + lightness,\n"
"                      color.g*saturation + lightness,\n"
"                      color.b*saturation + lightness,\n"
"                      color.a);\n"
"}\n";
#else
const char *DESATURATE_VERTEX_SHADER = "";
const char *DESATURATE_SATURATE_FRAGMENT_SHADER = "";
#endif /* HAS_GLES */



struct _TidyDesaturationGroupPrivate
{
  /* Internal TidyDesaturationGroup stuff */
  ClutterShader *shader_saturate;
  CoglHandle tex_a;
  CoglHandle fbo_a;

  gboolean use_shader;
  gboolean undo_desaturation;

  int desaturation_step;
  int current_desaturation_step;

  /* if anything changed we need to recalculate desaturation */
  gboolean source_changed;
};

/**
 * SECTION:tidy-desaturation-group
 * @short_description: Pixel-shader modifier class
 *
 * #TidyDesaturationGroup Renders all of its children to an offscreen buffer,
 * and then renders this buffer to the screen using a pixel shader.
 *
 */

G_DEFINE_TYPE (TidyDesaturationGroup,
               tidy_desaturation_group,
               CLUTTER_TYPE_GROUP);

/* When the desaturation group's children are modified we need to
   re-paint to the source texture. When it is only us that
   has been modified child==NULL */
static
gboolean tidy_desaturation_group_notify_modified_real(ClutterActor          *actor,
                                              ClutterActor          *child)
{
  if (!TIDY_IS_SANE_DESATURATION_GROUP(actor))
    return TRUE;

  TidyDesaturationGroup *container = TIDY_DESATURATION_GROUP(actor);
  TidyDesaturationGroupPrivate *priv = container->priv;
  if (child != NULL) {
    priv->source_changed = TRUE;
    priv->current_desaturation_step = 0;
    clutter_actor_queue_redraw(CLUTTER_ACTOR(container));
  }

  return TRUE;
}

static void tidy_desaturation_group_check_shader(TidyDesaturationGroup *group,
                                         ClutterShader **shader,
                                         const char *fragment_source,
                                         const char *vertex_source)
{
  TidyDesaturationGroupPrivate *priv = group->priv;

  if (priv->use_shader && !*shader)
   {
     GError *error = NULL;
     char   *old_locale;

#if GLSL_LOCALE_FIX
      old_locale = g_strdup (setlocale (LC_ALL, NULL));
      setlocale (LC_NUMERIC, "C");
#endif

      *shader = clutter_shader_new();
      if (fragment_source)
        clutter_shader_set_fragment_source (*shader, fragment_source, -1);
      if (vertex_source)
        clutter_shader_set_vertex_source (*shader, vertex_source, -1);
      clutter_shader_compile (*shader, &error);

      if (error)
      {
        g_warning ("unable to load shader: %s\n", error->message);
        g_error_free (error);
        priv->use_shader = FALSE;
      }

#if GLSL_LOCALE_FIX
      setlocale (LC_ALL, old_locale);
      g_free (old_locale);
#endif
   }
}

static void
tidy_desaturation_group_allocate_textures (TidyDesaturationGroup *self)
{
  TidyDesaturationGroupPrivate *priv = self->priv;
  gfloat tex_width, tex_height;

#ifdef __i386__
  if (!cogl_features_available(COGL_FEATURE_OFFSCREEN))
    /* Don't try to allocate FBOs. */
    return;
#endif

  /* Free the texture. */
  if (priv->fbo_a)
    {
      cogl_offscreen_unref(priv->fbo_a);
      cogl_texture_unref(priv->tex_a);
      priv->fbo_a = 0;
      priv->tex_a = 0;
    }

  /* (Re)create the texture and offscreen buffer.
   * We can specify mipmapping here, but we don't need it. */
  clutter_actor_get_size(CLUTTER_ACTOR(self), &tex_width, &tex_height);

  priv->tex_a = cogl_texture_new_with_size(
            tex_width, tex_height, COGL_TEXTURE_NO_AUTO_MIPMAP,
            COGL_PIXEL_FORMAT_RGBA_8888);
#ifdef UPSTREAM_DISABLED
  cogl_texture_set_filters(priv->tex_a, CGL_NEAREST, CGL_NEAREST);
#endif
  priv->fbo_a = cogl_offscreen_new_to_texture(priv->tex_a);

  priv->current_desaturation_step = 0;
  priv->source_changed = TRUE;
}

static gboolean
tidy_desaturation_group_children_visible(ClutterGroup *group)
{
  gint i;
  ClutterActor *actor;

  for (i = 0, actor = clutter_group_get_nth_child(group, 0);
       actor; actor = clutter_group_get_nth_child(group, ++i))
    {
      if (CLUTTER_IS_GROUP(actor))
        {
          if (tidy_desaturation_group_children_visible(CLUTTER_GROUP(actor)))
            return TRUE;
        }
      else
        {
          if (clutter_actor_is_visible(actor))
            return TRUE;
        }
    }
  return FALSE;
}

/* Recursively set texture filtering state on this actor and children, and
 * save the old state in the object. */
static void
recursive_set_linear_texture_filter(ClutterActor *actor, GArray *filters)
{
  if (CLUTTER_IS_CONTAINER(actor))
    clutter_container_foreach(CLUTTER_CONTAINER(actor),
                   (ClutterCallback)recursive_set_linear_texture_filter,
                   filters);
  else if (CLUTTER_IS_TEXTURE(actor))
    {
      ClutterTexture *tex = CLUTTER_TEXTURE(actor);
      ClutterTextureQuality quality;

      quality = clutter_texture_get_filter_quality(tex);
      g_array_append_val(filters, quality);
      clutter_texture_set_filter_quality(tex, CLUTTER_TEXTURE_QUALITY_LOW);
    }
}

/* Recursively set texture filtering state on this actor and children, and
 * save the old state in the object. */
static void
recursive_reset_texture_filter(ClutterActor *actor,
                               const ClutterTextureQuality **filtersp)
{
  if (CLUTTER_IS_CONTAINER(actor))
    clutter_container_foreach(CLUTTER_CONTAINER(actor),
                        (ClutterCallback)recursive_reset_texture_filter,
                        filtersp);
  else if (CLUTTER_IS_TEXTURE(actor))
    {
      clutter_texture_set_filter_quality(CLUTTER_TEXTURE(actor),
                                         **filtersp);
      (*filtersp)++;
    }
}

static void
tidy_desaturation_group_paint (ClutterActor *actor)
{
  CoglColor white = { 0xff, 0xff, 0xff, 0xff };
  CoglColor bgcol = { 0x00, 0x00, 0x00, 0xff };
  ClutterGroup *group         = CLUTTER_GROUP(actor);
  TidyDesaturationGroup *container    = TIDY_DESATURATION_GROUP(group);
  TidyDesaturationGroupPrivate *priv  = container->priv;
  ClutterActorBox              box;
  gint                         width, height, tex_width, tex_height;
  CoglColor                 col;
  GArray                      *filters;
  const ClutterTextureQuality *filters_array;

  if (!TIDY_IS_SANE_DESATURATION_GROUP(actor))
    return;

  clutter_actor_get_allocation_box(actor, &box);
  width  = box.x2 - box.x1; /* FIXME */
  height = box.y2 - box.y1;

  /* If we are rendering normally then shortcut all this, and
   just render directly without the texture */
  if (!tidy_desaturation_group_source_buffered(actor) ||
      !tidy_desaturation_group_children_visible(group))
    {
      /* set our buffer as damaged, so next time it gets re-created */
      priv->current_desaturation_step = 0;
      priv->source_changed = TRUE;
      CLUTTER_ACTOR_CLASS(tidy_desaturation_group_parent_class)->paint(actor);
      return;
    }

#ifdef __i386__
  if (!cogl_features_available(COGL_FEATURE_OFFSCREEN))
      return;
#endif

  tex_width  = cogl_texture_get_width(priv->tex_a);
  tex_height = cogl_texture_get_height(priv->tex_a);

  /* Draw children into an offscreen buffer */
  if (priv->source_changed && priv->current_desaturation_step==0)
    {
      cogl_push_matrix();
      tidy_util_cogl_push_offscreen_buffer(priv->fbo_a);

      cogl_scale(1.0*tex_width/width, 1.0*tex_height/height, 1.0); /* FIXME */
      /* FIXME - do nit init every time */
      cogl_color_init_from_4ub(&white, 0xff, 0xff, 0xff, 0xff);
      cogl_color_init_from_4ub(&bgcol, 0x00, 0x00, 0x00, 0xff);

      cogl_clear(&bgcol, COGL_BUFFER_BIT_COLOR);
      cogl_set_source_color (&white);

      /* Actually do the drawing of the children, but ensure that they are
       * all linear sampled so they are smoothly interpolated. Restore after. */
      filters = g_array_new(FALSE, FALSE, sizeof(ClutterTextureQuality));
      recursive_set_linear_texture_filter(actor, filters);
      CLUTTER_ACTOR_CLASS(tidy_desaturation_group_parent_class)->paint(actor);
      filters_array = (void *)filters->data;
      recursive_reset_texture_filter(actor, &filters_array);
      g_array_free(filters, TRUE);

      tidy_util_cogl_pop_offscreen_buffer();
      cogl_pop_matrix();

      priv->source_changed = FALSE;
      priv->current_desaturation_step = 0;
    }

  if (priv->current_desaturation_step != priv->desaturation_step)
    clutter_actor_queue_redraw(actor);

  gfloat mx, my, zx, zy;
  mx = width / 2.0;
  my = height / 2.0;
  zx = width * 0.5f;
  zy = height * 0.5f;

  /* Render what we've desaturated to the screen */
  /* Now we render the image we have, with a desaturation pixel
   * shader */
  if (priv->use_shader && priv->shader_saturate)
    {

      clutter_shader_set_is_enabled (priv->shader_saturate, !priv->undo_desaturation);
      if (!priv->undo_desaturation)
        {
          GValue v = G_VALUE_INIT;

          g_value_init(&v, G_TYPE_FLOAT);
          g_value_set_float(&v, 0.0f);

          clutter_shader_set_uniform (priv->shader_saturate, "saturation", &v);
        }
    }

  cogl_color_init_from_4ub(&col, 0xff, 0xff, 0xff,
                      clutter_actor_get_paint_opacity (actor));
  cogl_set_source_color (&col);

  /* Set the desaturation texture to linear interpolation - so we draw it smoothly
   * Onto the screen */
#ifdef UPSTREAM_DISABLED
  cogl_texture_set_filters(priv->tex_a, CGL_LINEAR, CGL_LINEAR);
#endif
  cogl_set_source_texture (priv->tex_a);
  cogl_rectangle_with_texture_coords (
                          mx-zx, my-zy,
                          mx+zx, my+zy,
                          0, 0, 1.0, 1.0);

  /* Reset the filters on the tex_a texture ready for normal desaturating */
#ifdef UPSTREAM_DISABLED
  cogl_texture_set_filters(priv->tex_a, CGL_NEAREST, CGL_NEAREST);
#endif

  if (priv->use_shader && priv->shader_saturate && !priv->undo_desaturation)
    clutter_shader_set_is_enabled (priv->shader_saturate, FALSE);

}

static void
tidy_desaturation_group_dispose (GObject *gobject)
{
  TidyDesaturationGroup *container = TIDY_DESATURATION_GROUP(gobject);
  TidyDesaturationGroupPrivate *priv = container->priv;

  if (priv->fbo_a)
    {
      cogl_offscreen_unref(priv->fbo_a);
      cogl_texture_unref(priv->tex_a);
      priv->fbo_a = 0;
      priv->tex_a = 0;
    }

  G_OBJECT_CLASS (tidy_desaturation_group_parent_class)->dispose (gobject);
}

static void
tidy_desaturation_group_class_init (TidyDesaturationGroupClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TidyDesaturationGroupPrivate));

  gobject_class->dispose = tidy_desaturation_group_dispose;

  actor_class->paint = tidy_desaturation_group_paint;
#ifdef UPSTREAM_DISABLED
  actor_class->notify_modified = tidy_desaturation_group_notify_modified_real;
#endif
}

static void
tidy_desaturation_group_init (TidyDesaturationGroup *self)
{
  TidyDesaturationGroupPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   TIDY_TYPE_DESATURATION_GROUP,
                                                   TidyDesaturationGroupPrivate);
  priv->desaturation_step = 0;
  priv->current_desaturation_step = 0;
  priv->source_changed = TRUE;
  priv->undo_desaturation = FALSE;

#if CLUTTER_COGL_HAS_GLES
  priv->use_shader = cogl_features_available(COGL_FEATURE_SHADERS_GLSL);
#else
  priv->use_shader = FALSE; /* For now, as Xephyr hates us */
#endif
  priv->shader_saturate = 0;

  priv->tex_a = 0;
  priv->fbo_a = 0;

  tidy_desaturation_group_check_shader(self, &priv->shader_saturate,
                               DESATURATE_SATURATE_FRAGMENT_SHADER, 0);

  g_signal_connect(self, "notify::allocation",
                   G_CALLBACK(tidy_desaturation_group_allocate_textures), NULL);
}

/*
 * Public API
 */

/**
 * tidy_desaturation_group_new:
 *
 * Creates a new render container
 *
 * Return value: the newly created #TidyDesaturationGroup
 */
ClutterActor *
tidy_desaturation_group_new (void)
{
  return g_object_new (TIDY_TYPE_DESATURATION_GROUP, NULL);
}

/**
 * tidy_desaturation_group_desaturate:
 *
 * Sets desaturated texture
 */
void tidy_desaturation_group_desaturate(ClutterActor *desaturation_group)
{
  TidyDesaturationGroupPrivate *priv;

  if (!TIDY_IS_SANE_DESATURATION_GROUP(desaturation_group))
    return;

  priv = TIDY_DESATURATION_GROUP(desaturation_group)->priv;

  tidy_desaturation_group_allocate_textures(TIDY_DESATURATION_GROUP(desaturation_group));

  priv->undo_desaturation = FALSE;
  priv->desaturation_step = 1;
  clutter_actor_queue_redraw(desaturation_group);
}

/**
 * tidy_desaturation_group_undo_desaturation:
 *
 * Removes desaturated texture
 */
void tidy_desaturation_group_undo_desaturate(ClutterActor *desaturation_group)
{
  TidyDesaturationGroupPrivate *priv;

  if (!TIDY_IS_SANE_DESATURATION_GROUP(desaturation_group))
    return;

  priv = TIDY_DESATURATION_GROUP(desaturation_group)->priv;

  priv->undo_desaturation = TRUE;
  priv->source_changed = TRUE;
  priv->current_desaturation_step = 0;
  priv->desaturation_step = 0;
  clutter_actor_queue_redraw(desaturation_group);
}

/**
 * tidy_desaturation_group_source_buffered:
 *
 * Return true if this desaturation group is currently buffering it's actors. Used
 * when this actually needs to desaturate its children
 */
gboolean tidy_desaturation_group_source_buffered(ClutterActor *desaturation_group)
{
  TidyDesaturationGroupPrivate *priv;

  if (!TIDY_IS_SANE_DESATURATION_GROUP(desaturation_group))
    return FALSE;

  priv = TIDY_DESATURATION_GROUP(desaturation_group)->priv;
  return !(priv->desaturation_step==0);
}
