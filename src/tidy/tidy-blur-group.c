/* Created by Gordon Williams <gordon.williams@collabora.co.uk>
 *
 * This class blurs all of its children, also changing saturation and lightness.
 * It renders its children into a half-size texture first, then blurs this into
 * another texture, finally rendering that to the screen. Because of this, when
 * the blurring doesn't change from frame to frame, children and NOT rendered,
 * making this pretty quick. */

#include "tidy-blur-group.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <clutter/clutter-container.h>

#include <cogl/cogl.h>

#include <string.h>
#include <locale.h>


/* This fixes the bug where the SGX GLSL compiler uses the current locale for
 * numbers - so '1.0' in a shader will not work when the locale says that ','
 * is a decimal separator.
 */
#define GLSL_LOCALE_FIX 1

/* The OpenGL fragment shader used to do blur and desaturation.
 * We use 3 samples here arranged in a rough triangle. We need
 * 2 versions as GLES and GL use slightly different syntax */
#if CLUTTER_COGL_HAS_GLES
const char *BLUR_FRAGMENT_SHADER =
"precision lowp float;\n"
"varying lowp vec4      frag_color;\n"
"varying mediump vec2      tex_coord;\n"
"uniform lowp sampler2D tex;\n"
"uniform mediump float blur;\n"
"uniform lowp float saturation;\n"
"void main () {\n"
"  mediump vec2 diffa = vec2(0.0, -0.875 * blur); \n"
"  mediump vec2 diffb = vec2(0.75*blur, 0.25*blur); \n"
"  mediump vec2 diffc = vec2(-blur, 0.375*blur); \n"
"  lowp vec4 color =  texture2D (tex, tex_coord+diffa)*0.333 + "
" texture2D (tex, tex_coord+diffb)*0.333 + "
"texture2D (tex, tex_coord+diffc)*0.333;\n"
"  color = color * frag_color;\n"
// saturation
"  lowp float lightness = (color.r+color.g+color.b)*0.333*(1.0-saturation); \n"
"  gl_FragColor = vec4(\n"
"                      color.r*saturation + lightness,\n"
"                      color.g*saturation + lightness,\n"
"                      color.b*saturation + lightness,\n"
"                      color.a);\n"
"}\n";
#else
const char *BLUR_FRAGMENT_SHADER =
"uniform sampler2D tex;\n"
"uniform float blur;\n"
"uniform float saturation;\n"
"void main () {\n"
"  vec2 diffa = vec2(0.0, -0.875 * blur); \n"
"  vec2 diffb = vec2(0.75*blur, 0.25*blur); \n"
"  vec2 diffc = vec2(-blur, 0.375*blur); \n"
"  vec2 tex_coord = vec2(gl_TexCoord[0]); \n"
"  vec4 color =  texture2D (tex, tex_coord+diffa)*0.333 + "
" texture2D (tex, tex_coord+diffb)*0.333 + "
"texture2D (tex, tex_coord+diffc)*0.333;\n"
"  color = color * gl_Color;\n"
// saturation
"  float lightness = (color.r+color.g+color.b)*0.333*(1.0-saturation); \n"
"  gl_FragColor = vec4(\n"
"                      color.r*saturation + lightness,\n"
"                      color.g*saturation + lightness,\n"
"                      color.b*saturation + lightness,\n"
"                      color.a);\n"
"}\n";
#endif /* HAS_GLES */



struct _TidyBlurGroupPrivate
{
  /* Internal TidyBlurGroup stuff */
  ClutterShader *shader;
  CoglHandle tex_preblur;
  CoglHandle fbo_preblur;

  CoglHandle tex_postblur;
  CoglHandle fbo_postblur;

  gboolean use_shader;
  float saturation; /* 0->1 how much colour there is */
  float blur; /* amount of blur in pixels */
  float brightness; /* 1=normal, 0=black */
  float zoom; /* amount to zoom. 1=normal, 0.5=out, 2=double-size */
  gboolean use_alpha; /* whether to use an alpha channel in our textures */
  gboolean use_mirror; /* whether to mirror the edge of teh blurred texture */

  /* if anything changed we need to recalculate preblur */
  gboolean source_changed;
  /* if anything changed we need to recalculate postblur */
  gboolean blur_changed;
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

/* When the blur group's children are modified we need to
   re-paint to the source texture. When it is only us that
   has been modified child==NULL */
static
gboolean tidy_blur_group_notify_modified_real(ClutterActor          *actor,
                                              ClutterActor          *child)
{
  if (!TIDY_IS_BLUR_GROUP(actor))
    return TRUE;

  TidyBlurGroup *container = TIDY_BLUR_GROUP(actor);
  TidyBlurGroupPrivate *priv = container->priv;
  if (child != NULL)
    priv->source_changed = TRUE;
  return TRUE;
}

/* An implementation for the ClutterGroup::paint() vfunc,
   painting all the child actors: */
static void
tidy_blur_group_paint (ClutterActor *actor)
{
  ClutterColor    white = { 0xff, 0xff, 0xff, 0xff };
  ClutterColor    col = { 0xff, 0xff, 0xff, 0xff };
  ClutterColor    bgcol = { 0x00, 0x00, 0x00, 0x00 };
  gint            x_1, y_1, x_2, y_2;

  if (!TIDY_IS_BLUR_GROUP(actor))
    return;

  ClutterGroup *group = CLUTTER_GROUP(actor);
  TidyBlurGroup *container = TIDY_BLUR_GROUP(group);
  TidyBlurGroupPrivate *priv = container->priv;

  /* If we are rendering normally then shortcut all this, and
   just render directly without the texture */
  if (!tidy_blur_group_source_buffered(actor))
    {
      TIDY_BLUR_GROUP_GET_CLASS(actor)->overridden_paint(actor);
      return;
    }

  clutter_actor_get_allocation_coords (actor, &x_1, &y_1, &x_2, &y_2);

  int width = x_2 - x_1;
  int height = y_2 - y_1;
  int exp_width = width/2;
  int exp_height = height/2;
  int tex_width = 0;
  int tex_height = 0;

  /* check sizes */
  if (priv->tex_preblur)
    {
      tex_width = cogl_texture_get_width(priv->tex_preblur);
      tex_height = cogl_texture_get_height(priv->tex_preblur);
    }
  /* free texture if the size is wrong */
  if (tex_width!=exp_width || tex_height!=exp_height) {
    if (priv->fbo_preblur)
      {
        cogl_offscreen_unref(priv->fbo_preblur);
        cogl_texture_unref(priv->tex_preblur);
        priv->fbo_preblur = 0;
        priv->tex_preblur = 0;
      }
    if (priv->fbo_postblur)
      {
        cogl_offscreen_unref(priv->fbo_postblur);
        cogl_texture_unref(priv->tex_postblur);
        priv->fbo_postblur = 0;
        priv->tex_postblur = 0;
      }
  }
  /* create the texture + offscreen buffer if they didn't exist.
   * We can specify mipmapping here, but we don't need it */
  if (!priv->tex_preblur)
    {
      tex_width = exp_width;
      tex_height = exp_height;
      priv->tex_preblur = cogl_texture_new_with_size(
                tex_width, tex_height, 0, 0,
                priv->use_alpha ? COGL_PIXEL_FORMAT_RGBA_4444 :
                                  COGL_PIXEL_FORMAT_RGB_565);
      /* set nearest texture filter - this just takes a single sample */
      /* cogl_texture_set_filters(priv->tex_preblur, CGL_NEAREST, CGL_NEAREST); */
      priv->fbo_preblur = cogl_offscreen_new_to_texture (priv->tex_preblur);
      priv->source_changed = TRUE;
    }
  if (!priv->tex_postblur)
    {
      priv->tex_postblur = cogl_texture_new_with_size(
                tex_width, tex_height, 0, 0,
                priv->use_alpha ? COGL_PIXEL_FORMAT_RGBA_4444 :
                                  COGL_PIXEL_FORMAT_RGB_565);
      priv->fbo_postblur = cogl_offscreen_new_to_texture (priv->tex_postblur);
      priv->blur_changed = TRUE;
    }

  /* Draw children into an offscreen buffer */
  if (priv->source_changed)
    {
      cogl_draw_buffer(COGL_OFFSCREEN_BUFFER, priv->fbo_preblur);
      cogl_push_matrix();
      cogl_scale(CFX_ONE*tex_width/width, CFX_ONE*tex_height/height);

      cogl_paint_init(&bgcol);
      cogl_color (&white);
      TIDY_BLUR_GROUP_GET_CLASS(actor)->overridden_paint(actor);

      cogl_pop_matrix();
      priv->blur_changed = TRUE;
      priv->source_changed = FALSE;
    }

  /* if we have no shader, so attempt to create one */
  if (priv->use_shader && !priv->shader)
    {
      GError           *error = NULL;
      char             *old_locale;

#if GLSL_LOCALE_FIX
      old_locale = g_strdup( setlocale (LC_ALL, NULL) );
      setlocale (LC_NUMERIC, "C");
#endif

      priv->shader = clutter_shader_new();
      clutter_shader_set_fragment_source (priv->shader,
                                          BLUR_FRAGMENT_SHADER, -1);
      clutter_shader_compile (priv->shader, &error);

#if GLSL_LOCALE_FIX
      setlocale (LC_ALL, old_locale);
      g_free (old_locale);
#endif

      if (error)
        {
          g_warning ("unable to load shader: %s\n", error->message);
          g_error_free (error);
          priv->use_shader = FALSE;
        }
    }

  /* blur the pre_blur texture into the post_blur texture */
  if (priv->blur_changed)
    {
      cogl_draw_buffer(COGL_OFFSCREEN_BUFFER, priv->fbo_postblur);
      if (priv->use_shader && priv->shader)
        {
          clutter_shader_set_is_enabled (priv->shader, TRUE);
          clutter_shader_set_uniform_1f (priv->shader, "blur",
                                         priv->blur/width);
          clutter_shader_set_uniform_1f (priv->shader, "saturation",
                                         priv->saturation);
        }

      cogl_blend_func(CGL_ONE, CGL_ZERO);
      cogl_color (&white);
      cogl_texture_rectangle (priv->tex_preblur, 0, 0,
                              CLUTTER_INT_TO_FIXED (tex_width),
                              CLUTTER_INT_TO_FIXED (tex_height),
                              0, 0,
                              CFX_ONE,
                              CFX_ONE);
      cogl_blend_func(CGL_SRC_ALPHA, CGL_ONE_MINUS_SRC_ALPHA);

      if (priv->use_shader && priv->shader)
        clutter_shader_set_is_enabled (priv->shader, FALSE);
      priv->blur_changed = FALSE;
    }

  /* set our brightness here, so we don't have to re-render
   * the blur if it changes */
  col.red = (int)(priv->brightness*255);
  col.green = (int)(priv->brightness*255);
  col.blue = (int)(priv->brightness*255);
  col.alpha = clutter_actor_get_paint_opacity (actor);
  cogl_color (&col);

  /* Render the blurred texture to the screen */
  cogl_draw_buffer(COGL_WINDOW_BUFFER, 0);

  col.red = (int)(priv->brightness*255);
  col.green = (int)(priv->brightness*255);
  col.blue = (int)(priv->brightness*255);
  col.alpha = clutter_actor_get_paint_opacity (actor);
  cogl_color (&col);

  {
    ClutterFixed mx, my, zx, zy;
    mx = CLUTTER_INT_TO_FIXED (width) / 2;
    my = CLUTTER_INT_TO_FIXED (height) / 2;
    zx = CLUTTER_FLOAT_TO_FIXED(width*0.5f*priv->zoom);
    zy = CLUTTER_FLOAT_TO_FIXED(height*0.5f*priv->zoom);

    if ((priv->zoom >= 1) || !priv->use_mirror)
      {
        cogl_texture_rectangle (priv->tex_postblur,
                                mx-zx, my-zy,
                                mx+zx, my+zy,
                                0, 0, CFX_ONE, CFX_ONE);
      }
    else
      {
        gint vignette_amt;
        /* draw a 7x7 grid with 5x5 unmirrored, and the edges mirrored  */
#define TILINGF 7
#define TILING (TILINGF-2)

        CoglTextureVertex verts[6*(TILINGF*TILINGF)];
        CoglTextureVertex grid[(TILINGF+1)*(TILINGF+1)];
        CoglTextureVertex *v = grid;
        gint x,y;

        vignette_amt = (int)((1-priv->zoom)*2048);
        if (vignette_amt<0) vignette_amt = 0;
        if (vignette_amt>255) vignette_amt = 255;

        /* work out grid points */
        for (y=0;y<=TILINGF;y++)
          for (x=0;x<=TILINGF;x++)
            {
              ClutterFixed dx,dy,d;
              v->x = mx+(zx*(x*2-(TILING+2))/TILING);
              v->y = my+(zy*(y*2-(TILING+2))/TILING);
              v->z = 0;
              v->tx = (x-1) * CFX_ONE / TILING;
              v->ty = (y-1) * CFX_ONE / TILING;
              /* mirror edges */
              if (v->tx < 0)
                v->tx = -v->tx;
              if (v->tx > CFX_ONE)
                v->tx = CFX_ONE*2 - v->tx;
              if (v->ty < 0)
                v->ty = -v->ty;
              if (v->ty > CFX_ONE)
                v->ty = CFX_ONE*2 - v->ty;
              /* colour - work out distance from centre (squared) */
              dx = CFX_QDIV(v->x - mx, mx/2);
              dy = CFX_QDIV(v->y - my, my/2);
              d = CFX_QMUL(dx,dx) + CFX_MUL(dy,dy);
              /* Colour value... */
              /*gint c = (int)(priv->brightness*255) *
                       (255 - CLUTTER_FIXED_TO_INT((d-CFX_ONE)*vignette_amt/6))
                       / 256;
              if (c<0) c=0;
              if (c>255) c=255;*/
              gint c = 255;
              if ((x==1) || (y==1) || (x==TILINGF-1) || (y==TILINGF-1))
                c = 192;
              if ((x==0) || (y==0) || (x==TILINGF) || (y==TILINGF))
                c = 0;
            //  c = (int)(priv->brightness * (255 - vignette_amt*c/255));
              if (c<0) c=0;
              if (c>255) c=255;
              v->color.red = v->color.green = v->color.blue = c;
              v->color.alpha = col.alpha;
              /* next vertex */
              v++;
            }

        /* now work out actual vertices - join the grid points with
         * 2 triangles to make a quad */
        v = verts;
        for (y=0;y<TILINGF;y++)
          for (x=0;x<TILINGF;x++)
            {
              CoglTextureVertex *grid_pt = &grid[x + y*(TILINGF+1)];
              v[0] = grid_pt[0]; /* tri 1 */
              v[1] = grid_pt[1];
              v[2] = grid_pt[1+(TILINGF+1)];
              v[3] = grid_pt[0]; /* tri 2 */
              v[4] = grid_pt[1+(TILINGF+1)];
              v[5] = grid_pt[TILINGF+1];
              v+=6;
            }
        /* render! */
        cogl_texture_triangles (priv->tex_postblur,
                                6*(TILINGF*TILINGF),
                                verts,
                                TRUE);
      }
  }

}

static void
tidy_blur_group_dispose (GObject *gobject)
{
  TidyBlurGroup *container = TIDY_BLUR_GROUP(gobject);
  TidyBlurGroupPrivate *priv = container->priv;

  if (priv->fbo_preblur)
    {
      cogl_offscreen_unref(priv->fbo_preblur);
      cogl_texture_unref(priv->tex_preblur);
      priv->fbo_preblur = 0;
      priv->tex_preblur = 0;
    }
  if (priv->fbo_postblur)
    {
      cogl_offscreen_unref(priv->fbo_postblur);
      cogl_texture_unref(priv->tex_postblur);
      priv->fbo_postblur = 0;
      priv->tex_postblur = 0;
    }

  G_OBJECT_CLASS (tidy_blur_group_parent_class)->dispose (gobject);
}

static void
tidy_blur_group_class_init (TidyBlurGroupClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  g_type_class_add_private (klass, sizeof (TidyBlurGroupPrivate));

  gobject_class->dispose = tidy_blur_group_dispose;

  /* Provide implementations for ClutterActor vfuncs: */
  klass->overridden_paint = actor_class->paint;
  actor_class->paint = tidy_blur_group_paint;
  actor_class->notify_modified = tidy_blur_group_notify_modified_real;
}

static void
tidy_blur_group_init (TidyBlurGroup *self)
{
  TidyBlurGroupPrivate *priv;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
                                                   TIDY_TYPE_BLUR_GROUP,
                                                   TidyBlurGroupPrivate);
  priv->blur = 0;
  priv->saturation = 1;
  priv->brightness = 1;
  priv->zoom = 1;
  priv->use_alpha = TRUE;
  priv->use_mirror = FALSE;
  priv->blur_changed = TRUE;
  priv->source_changed = TRUE;

#if CLUTTER_COGL_HAS_GLES
  priv->use_shader = cogl_features_available(COGL_FEATURE_SHADERS_GLSL);
#else
  priv->use_shader = FALSE; /* For now, as Xephyr hates us */
#endif
  priv->tex_preblur = 0;
  priv->fbo_preblur = 0;
  priv->tex_postblur = 0;
  priv->fbo_postblur = 0;
  priv->shader = 0;
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
 * tidy_blur_group_set_blur:
 *
 * Sets the amount of blur (in pixels)
 */
void tidy_blur_group_set_blur(ClutterActor *blur_group, float blur)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (priv->blur != blur)
    {
      priv->blur_changed = TRUE;
      priv->blur = blur;
      if (CLUTTER_ACTOR_IS_VISIBLE(blur_group))
        clutter_actor_queue_redraw(blur_group);
    }
}

/**
 * tidy_blur_group_set_saturation:
 *
 * Sets the saturation (1 = normal, 0=black and white)
 */
void tidy_blur_group_set_saturation(ClutterActor *blur_group, float saturation)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (priv->saturation != saturation)
    {
      priv->blur_changed = TRUE;
      priv->saturation = saturation;
      if (CLUTTER_ACTOR_IS_VISIBLE(blur_group))
        clutter_actor_queue_redraw(blur_group);
    }
}

/**
 * tidy_blur_group_set_brightness:
 *
 * Sets the brightness (1 = normal, 0=black)
 */
void tidy_blur_group_set_brightness(ClutterActor *blur_group, float brightness)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (priv->brightness != brightness)
    {
      priv->brightness = brightness;
      if (CLUTTER_ACTOR_IS_VISIBLE(blur_group))
        clutter_actor_queue_redraw(blur_group);
    }
}


/**
 * tidy_blur_group_set_zoom:
 *
 * Set how far to zoom in on what has been blurred
 * 1=normal, 0.5=out, 2=double-size
 */
void tidy_blur_group_set_zoom(ClutterActor *blur_group, float zoom)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  if (priv->zoom != zoom)
    {
      priv->zoom = zoom;
      if (CLUTTER_ACTOR_IS_VISIBLE(blur_group))
        clutter_actor_queue_redraw(blur_group);
    }
}

/**
 * tidy_blur_group_get_zoom:
 *
 * Get how far to zoom in on what has been blurred
 * 1=normal, 0.5=out, 2=double-size
 */
float tidy_blur_group_get_zoom(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return 1.0f;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;

  return priv->zoom;
}

/**
 * tidy_blur_group_set_use_alpha:
 *
 * Sets whether to use an alpha channel in the textures used for blurring.
 * Only useful if we're blurring something transparent
 */
void tidy_blur_group_set_use_alpha(ClutterActor *blur_group, gboolean alpha)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
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
void tidy_blur_group_set_use_mirror(ClutterActor *blur_group, gboolean mirror)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
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
void tidy_blur_group_set_source_changed(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  priv->source_changed = TRUE;
}

/**
 * tidy_blur_group_source_buffered:
 *
 * Return true if this blur group is currently buffering it's actors. Used
 * when this actually needs to blur or desaturate its children
 */
gboolean tidy_blur_group_source_buffered(ClutterActor *blur_group)
{
  TidyBlurGroupPrivate *priv;

  if (!TIDY_IS_BLUR_GROUP(blur_group))
    return FALSE;

  priv = TIDY_BLUR_GROUP(blur_group)->priv;
  return !(priv->blur==0 && priv->saturation==1 && priv->brightness==1);
}
