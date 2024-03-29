/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Author:  Tomas Frydrych <tf@o-hand.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "hd-home-view.h"
#include "hd-home-view-container.h"
#include "hd-home-view-layout.h"
#include "hd-comp-mgr.h"
#include "hd-home.h"
#include "hd-util.h"
#include "hd-home-applet.h"
#include "hd-render-manager.h"
#include "hd-clutter-cache.h"
#include "hd-transition.h"

#include "hildon-desktop.h"
#include "../tidy/tidy-sub-texture.h"

#include <clutter/clutter.h>

#include <matchbox/core/mb-wm.h>

#include <glib/gstdio.h>
#include <gconf/gconf-client.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define BACKGROUND_COLOR {0, 0, 0, 0xff}
#define CACHED_BACKGROUND_IMAGE_FILE_PNG "%s/.backgrounds/background-%u.png"
#define CACHED_BACKGROUND_IMAGE_FILE_PVR "%s/.backgrounds/background-%u.pvr"
#define CACHED_BACKGROUND_IMAGE_FILE_PNG_PORTRAIT "%s/.backgrounds/background_portrait-%u.png"
#define CACHED_BACKGROUND_IMAGE_FILE_PVR_PORTRAIT "%s/.backgrounds/background_portrait-%u.pvr"

#define GCONF_KEY_POSITION "/apps/osso/hildon-desktop/applets/%s/position"
#define GCONF_KEY_MODIFIED "/apps/osso/hildon-desktop/applets/%s/modified"
#define GCONF_KEY_VIEW     "/apps/osso/hildon-desktop/applets/%s/view"
#define GCONF_KEY_POSITION_PORTRAIT "/apps/osso/hildon-desktop/applets/%s/position_portrait"

#define MAX_VIEWS 9

/* Maximal pixel movement for a tap (before it is a move) */
#define MAX_TAP_DISTANCE 20

#define HD_HOME_VIEW_PARALLAX_AMOUNT (hd_transition_get_double("home", "parallax", 1.3))

enum
{
  PROP_COMP_MGR = 1,
  PROP_HOME,
  PROP_ID,
  PROP_ACTIVE,
  PROP_CONTAINER
};

struct _HdHomeViewPrivate
{
  MBWMCompMgrClutter       *comp_mgr;
  HdHome                   *home;
  HdHomeViewContainer      *view_container;
  ClutterActor             *background_container;
  ClutterActor             *applets_container;

  ClutterActor             *background;
  TidySubTexture           *background_sub;
  ClutterActor             *background_temp_portrait;
  TidySubTexture           *background_sub_temp_portrait;
  ClutterActor             *background_temp;
  TidySubTexture           *background_sub_temp;
  MBWindowManagerClient    *live_bg;

  GHashTable               *applets;

  gboolean                  is_portrait;

  gfloat                    applet_motion_start_x;
  gfloat                    applet_motion_start_y;
  gfloat                    applet_motion_start_position_x;
  gfloat                    applet_motion_start_position_y;

  gboolean                  applet_motion_tap : 1;

  gboolean                  move_applet_left : 1;
  gboolean                  move_applet_right : 1;

  gfloat                    pan_gesture_start_x;
  gfloat                    pan_gesture_start_y;

  guint                     id;

  guint load_background_source;

  GConfClient *gconf_client;

  HdHomeViewLayout *layout;
};

static void hd_home_view_class_init (HdHomeViewClass *klass);
static void hd_home_view_init       (HdHomeView *self);
static void hd_home_view_dispose    (GObject *object);
static void hd_home_view_finalize   (GObject *object);

static void hd_home_view_set_property (GObject      *object,
				       guint         prop_id,
				       const GValue *value,
				       GParamSpec   *pspec);

static void hd_home_view_get_property (GObject      *object,
				       guint         prop_id,
				       GValue       *value,
				       GParamSpec   *pspec);

static void hd_home_view_constructed (GObject *object);

static void
hd_home_view_rotate_background(ClutterActor *actor, GParamSpec *unused,
                               ClutterActor *stage);

static void
hd_home_view_resize_applets_container(ClutterActor *actor, GParamSpec *unused,
                               ClutterActor *stage);

static void
hd_home_view_allocation_changed (HdHomeView    *home_view,
                                 GParamSpec *pspec,
                                 gpointer    user_data);

static void snap_widget_to_grid (ClutterActor *widget);

typedef struct _HdHomeViewAppletData HdHomeViewAppletData;

struct _HdHomeViewAppletData
{
  ClutterActor *actor;

  MBWMCompMgrClient *cc;

  guint press_cb;
  guint release_cb;
  guint touch_cb;
  guint resize_cb;
  guint motion_cb;

  ClutterActor *close_button;
  ClutterActor *configure_button;
};

static HdHomeViewAppletData *applet_data_new  (ClutterActor *actor);
static void                  applet_data_free (HdHomeViewAppletData *data);

G_DEFINE_TYPE (HdHomeView, hd_home_view, CLUTTER_TYPE_GROUP);

static void
hd_home_view_allocate (ClutterActor           *self,
                       const ClutterActorBox  *box,
                       ClutterAllocationFlags  flags)
{
#if 0
  FIXME unused remove
  HdHomeView        *view = HD_HOME_VIEW (actor);
  HdHomeViewPrivate *priv = view->priv;

  /* We've resized, refresh the background image to fit the new size */
  if ((CLUTTER_UNITS_TO_INT (box->x2 - box->x1) != priv->bg_image_dest_width) ||
      (CLUTTER_UNITS_TO_INT (box->y2 - box->y1) != priv->bg_image_dest_height))
    hd_home_view_refresh_bg (view,
                             priv->background_image_file);
#endif

  CLUTTER_ACTOR_CLASS (hd_home_view_parent_class)->allocate (self, box, flags);
}

static void
hd_home_view_class_init (HdHomeViewClass *klass)
{
  ClutterActorClass *actor_class  = CLUTTER_ACTOR_CLASS (klass);
  GObjectClass      *object_class = G_OBJECT_CLASS (klass);
  GParamSpec        *pspec;

  g_type_class_add_private (klass, sizeof (HdHomeViewPrivate));

  actor_class->allocate      = hd_home_view_allocate;

  object_class->dispose      = hd_home_view_dispose;
  object_class->finalize     = hd_home_view_finalize;
  object_class->set_property = hd_home_view_set_property;
  object_class->get_property = hd_home_view_get_property;
  object_class->constructed  = hd_home_view_constructed;

  pspec = g_param_spec_pointer ("comp-mgr",
				"Composite Manager",
				"MBWMCompMgrClutter Object",
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_COMP_MGR, pspec);

  pspec = g_param_spec_pointer ("home",
				"HdHome",
				"Parent HdHome object",
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_HOME, pspec);

  pspec = g_param_spec_int ("id",
			    "id",
			    "Numerical id for this view",
			    0, MAX_VIEWS - 1,
			    0,
			    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_property (object_class, PROP_ID, pspec);

  g_object_class_install_property (object_class,
                                   PROP_ACTIVE,
                                   g_param_spec_boolean ("active",
                                                         "Active",
                                                         "View is active",
                                                         TRUE,
                                                         G_PARAM_READABLE));

  g_object_class_install_property (object_class,
                                   PROP_CONTAINER,
                                   g_param_spec_object ("view-container",
                                                        "View Container",
                                                        "Views are embedded in that container",
                                                        HD_TYPE_HOME_VIEW_CONTAINER,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

/* applets_container is not a member of HdHomeView (it is in HdHome's 'front'
 * container. Hence we want to show/hide the applets container whenever the
 * home view itself is hidden */
static gboolean hd_home_view_shown(HdHomeView *view) {
  HdHomeViewPrivate        *priv = view->priv;
  clutter_actor_show(priv->applets_container);
  return FALSE;
}
/* applets_container is not a member of HdHomeView (it is in HdHome's 'front'
 * container. Hence we want to show/hide the applets container whenever the
 * home view itself is hidden */
static gboolean hd_home_view_hidden(HdHomeView *view) {
  HdHomeViewPrivate        *priv = view->priv;
  clutter_actor_hide(priv->applets_container);
  return FALSE;
}

static gboolean
is_button_press_in_gesture_start_area (ClutterEvent *event)
{
  g_debug ("%s. (%lf, %lf)",
           __FUNCTION__,
           event->button.x, event->button.y);

	if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
	  return event->button.x <= 15 || event->button.x >= HD_COMP_MGR_LANDSCAPE_WIDTH - 15;
	else
		return event->button.x <= 15 || event->button.x >= HD_COMP_MGR_PORTRAIT_WIDTH - 15;
}

static gboolean stop_pan_gesture (HdHomeView *view);

static gboolean
pan_gesture_motion (ClutterActor *actor,
                    ClutterEvent *event,
                    HdHomeView   *view)
{
  HdHomeViewPrivate *priv = view->priv;

  g_debug ("%s. (%lf, %lf)",
           __FUNCTION__,
           event->motion.x, event->motion.y);

  if (ABS (priv->pan_gesture_start_y - event->motion.y) > 25)
    {
      stop_pan_gesture (view);
    }
  else
    {
      if (ABS (priv->pan_gesture_start_x - event->motion.x) > 50)
        {
          stop_pan_gesture (view);

          if (priv->pan_gesture_start_x <= 15)
            hd_home_view_container_scroll_to_previous (HD_HOME_VIEW_CONTAINER (priv->view_container), 0);
          else
            hd_home_view_container_scroll_to_next (HD_HOME_VIEW_CONTAINER (priv->view_container), 0);
        }
    }

  return FALSE;
}

static gboolean
stop_pan_gesture (HdHomeView *view)
{
  g_debug ("%s", __FUNCTION__);

  clutter_ungrab_pointer ();

  g_signal_handlers_disconnect_by_func (view,
                                        pan_gesture_motion,
                                        view);
  g_signal_handlers_disconnect_by_func (view,
                                        stop_pan_gesture,
                                        view);

  return FALSE;
}

static gboolean
pressed_on_view (ClutterActor *actor,
                 ClutterEvent *event,
                 HdHomeView   *view)
{
  HdHomeViewPrivate *priv = view->priv;

  g_debug ("%s. (%lf, %lf)",
           __FUNCTION__,
           event->button.x, event->button.y);

  if (is_button_press_in_gesture_start_area (event))
    {
      priv->pan_gesture_start_x = event->button.x;
      priv->pan_gesture_start_y = event->button.y;

      clutter_grab_pointer (CLUTTER_ACTOR (view));
      g_signal_connect (view, "motion-event",
                        G_CALLBACK (pan_gesture_motion), view);
      g_signal_connect_swapped (view, "button-release-event",
                                G_CALLBACK (stop_pan_gesture), view);
    }

  return FALSE;
}

static void
hd_home_view_constructed (GObject *object)
{
  ClutterColor              clr = BACKGROUND_COLOR;
  HdHomeView               *self = HD_HOME_VIEW (object);
  HdHomeViewPrivate        *priv = self->priv;

  priv->applets = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                         NULL,
                                         (GDestroyNotify) applet_data_free);

  priv->background_container = clutter_group_new ();
  clutter_actor_set_name (priv->background_container, "HdHomeView::background-container");
#ifdef UPSTREAM_DISABLED
  clutter_actor_set_visibility_detect(priv->background_container, FALSE);
#endif
  clutter_actor_set_position (priv->background_container, 0, 0);
  clutter_actor_set_size (priv->background_container,
                          HD_COMP_MGR_LANDSCAPE_WIDTH,
                          HD_COMP_MGR_LANDSCAPE_HEIGHT);
  g_signal_connect_swapped(clutter_stage_get_default(), "notify::allocation",
                           G_CALLBACK(hd_home_view_rotate_background),
                           priv->background_container);
  clutter_actor_add_child (CLUTTER_ACTOR (object), priv->background_container);

  priv->applets_container = clutter_group_new ();
  clutter_actor_set_name (priv->applets_container, "HdHomeView::applets-container");
#ifdef UPSTREAM_DISABLED
  clutter_actor_set_visibility_detect(priv->applets_container, FALSE);
#endif
  clutter_actor_set_position (priv->applets_container, 0, 0);
  clutter_actor_set_size (priv->applets_container,
                          HD_COMP_MGR_LANDSCAPE_WIDTH,
                          HD_COMP_MGR_LANDSCAPE_HEIGHT);
  g_signal_connect_swapped(clutter_stage_get_default(), "notify::allocation",
                           G_CALLBACK(hd_home_view_resize_applets_container),
                           priv->applets_container);
  clutter_actor_add_child (CLUTTER_ACTOR (hd_home_get_front(priv->home)),
                           priv->applets_container);

  /* By default the background is a black rectangle */
  priv->background = clutter_rectangle_new_with_color (&clr);
  clutter_actor_set_name (priv->background, "HdHomeView::background");
  clutter_actor_set_size (priv->background,
                          HD_COMP_MGR_LANDSCAPE_WIDTH,
                          HD_COMP_MGR_LANDSCAPE_HEIGHT);
  clutter_actor_add_child (CLUTTER_ACTOR (priv->background_container),
                           priv->background);

  clutter_actor_set_reactive (CLUTTER_ACTOR (object), TRUE);

  g_signal_connect (object, "button-press-event",
                    G_CALLBACK (pressed_on_view), object);

  g_signal_connect (object, "notify::allocation",
                    G_CALLBACK (hd_home_view_allocation_changed),
                    object);

  g_signal_connect (object, "show",
                    G_CALLBACK (hd_home_view_shown), NULL);
  g_signal_connect (object, "hide",
                    G_CALLBACK (hd_home_view_hidden), NULL);
}

static void
hd_home_view_init (HdHomeView *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HD_TYPE_HOME_VIEW, HdHomeViewPrivate);

  clutter_actor_set_name(CLUTTER_ACTOR(self), "HdHomeView");
  /* Explicitly enable maemo-specific visibility detection to cut down
   * spurious paints */
#ifdef UPSTREAM_DISABLED
  clutter_actor_set_visibility_detect(CLUTTER_ACTOR(self), TRUE);
#endif

  self->priv->gconf_client = gconf_client_get_default ();

  self->priv->layout = hd_home_view_layout_new ();
}

static void
hd_home_view_dispose (GObject *object)
{
  HdHomeView         *self           = HD_HOME_VIEW (object);
  HdHomeViewPrivate  *priv	     = self->priv;

  /* Remove idle/timeout handlers */
  if (priv->load_background_source)
    priv->load_background_source = (g_source_remove (priv->load_background_source), 0);

  if (priv->gconf_client)
    priv->gconf_client = (g_object_unref (priv->gconf_client), NULL);

  if (priv->applets)
    priv->applets = (g_hash_table_destroy (priv->applets), NULL);

  if (priv->layout)
    priv->layout = (g_object_unref (priv->layout), NULL);

  G_OBJECT_CLASS (hd_home_view_parent_class)->dispose (object);
}

static void
hd_home_view_finalize (GObject *object)
{
  G_OBJECT_CLASS (hd_home_view_parent_class)->finalize (object);
}

static void
set_background_common (HdHomeView *hview, ClutterActor *new_bg)
{
  HdHomeViewPrivate *priv = hview->priv;
  //ClutterActor *actor = CLUTTER_ACTOR (hview);
  TidySubTexture *new_bg_sub = 0;
  ClutterColor clr = BACKGROUND_COLOR;

  if (!new_bg)
    {
      /* Add a black background */
      new_bg = clutter_rectangle_new_with_color (&clr);
      clutter_actor_set_size (new_bg,
                              HD_COMP_MGR_LANDSCAPE_WIDTH,
                              HD_COMP_MGR_LANDSCAPE_HEIGHT);
    }
  else
    {
      guint bg_width, bg_height;
      guint actual_width, actual_height;

      /* we could be started in portrait mode */
      if (HD_COMP_MGR_LANDSCAPE_WIDTH > HD_COMP_MGR_LANDSCAPE_HEIGHT)
        {
          bg_width = HD_COMP_MGR_LANDSCAPE_WIDTH;
          bg_height = HD_COMP_MGR_LANDSCAPE_HEIGHT;
        }
      else
        {
          bg_width = HD_COMP_MGR_LANDSCAPE_HEIGHT;
          bg_height = HD_COMP_MGR_LANDSCAPE_WIDTH;
        }

      actual_width = clutter_actor_get_width (new_bg);
      actual_height = clutter_actor_get_height (new_bg);
      /* It may be that we get a bigger texture than we need
       * (because PVR texture compression has to use 2^n width
       * and height). In this case we want to crop off the
       * bottom + right sides, which we can do more efficiently
       * with TidySubTexture than we can with set_clip.
       */
      if (bg_width != actual_width ||
          bg_height != actual_height)
        {
          ClutterGeometry region;
          region.x = 0;
          region.y = 0;
          region.width = actual_width > bg_width ? bg_width : actual_width;
          region.height = actual_height > bg_height ? bg_height : actual_height;

          new_bg_sub = tidy_sub_texture_new(CLUTTER_TEXTURE(new_bg));
          tidy_sub_texture_set_region(new_bg_sub, &region);
          clutter_actor_set_size(CLUTTER_ACTOR(new_bg_sub), bg_width, bg_height);
          clutter_actor_hide(new_bg);
          clutter_actor_show(CLUTTER_ACTOR(new_bg_sub));
        }
    }

  clutter_actor_set_name (new_bg, "HdHomeView::background");

  if(hd_home_is_portrait_wallpaper_enabled (priv->home))
    {
      if(priv->is_portrait)
        { /* Remember wallpaper for portrait mode */
          clutter_actor_destroy(priv->background_temp_portrait);
          clutter_actor_destroy(CLUTTER_ACTOR(priv->background_sub_temp_portrait));
          priv->background_temp_portrait = new_bg;
          priv->background_sub_temp_portrait = new_bg_sub;
        }
      else
        { /* Remember wallpaper for landscape mode and show it */
          clutter_actor_destroy(priv->background_temp);
          clutter_actor_destroy(CLUTTER_ACTOR(priv->background_sub_temp));
          priv->background_temp = new_bg;
          priv->background_sub_temp = new_bg_sub;
        }
    }

    if( ((hd_comp_mgr_is_portrait () && !priv->is_portrait) 
      || (!hd_comp_mgr_is_portrait () && priv->is_portrait)) 
      && hd_home_is_portrait_wallpaper_enabled (priv->home))
      return;

    /* Add new background to the background container */
    clutter_actor_add_child (CLUTTER_ACTOR(priv->background_container), new_bg);

    if (new_bg_sub)
      clutter_actor_add_child (CLUTTER_ACTOR(priv->background_container),
                               CLUTTER_ACTOR(new_bg_sub));

    /* Remove the old background (color or image) and the subtexture
     * that may have been used to make it smaller */
    if (priv->background_sub)
        clutter_actor_destroy (CLUTTER_ACTOR(priv->background_sub));
    if (priv->background)
        clutter_actor_destroy (priv->background);

    /* Only update blur if we're currently active */
    if (hd_home_view_container_get_current_view(priv->view_container) == priv->id)
        hd_render_manager_blurred_changed();

    priv->background = new_bg;
    priv->background_sub = new_bg_sub;
    
}

static gboolean
load_background_idle (gpointer data)
{
  HdHomeView *self = HD_HOME_VIEW (data);
  HdHomeViewPrivate *priv = self->priv;
  gchar *cached_background_image_file;
  ClutterActor *new_bg = 0;
  GError *error = NULL;
  GError *error_portrait = NULL;
  int i;
  int max_value;

  if (g_source_is_destroyed (g_main_current_source ()))
    return FALSE;

  if(hd_home_is_portrait_wallpaper_enabled (priv->home))
    max_value = 2;
  else
    max_value = 1;

  for(i = 0; i < max_value; i++){
    if(!i)
      {
        cached_background_image_file = g_strdup_printf (CACHED_BACKGROUND_IMAGE_FILE_PNG,
                                                        g_get_home_dir (),
                                                        priv->id + 1);
        priv->is_portrait = FALSE;
      }
    else
      {
        cached_background_image_file = g_strdup_printf (CACHED_BACKGROUND_IMAGE_FILE_PNG_PORTRAIT,
                                                        g_get_home_dir (),
                                                        priv->id + 1);
        priv->is_portrait = TRUE;
      }

    if (!g_file_test (cached_background_image_file,
                      G_FILE_TEST_EXISTS))
      {
        g_free (cached_background_image_file);
        if(!i) 
          {
            cached_background_image_file = g_strdup_printf (CACHED_BACKGROUND_IMAGE_FILE_PVR,
                                                            g_get_home_dir (),
                                                            priv->id + 1);

            new_bg = clutter_texture_new_from_file (cached_background_image_file,
                                                      &error);
          }
        else 
          {
            cached_background_image_file = g_strdup_printf (CACHED_BACKGROUND_IMAGE_FILE_PVR_PORTRAIT,
                                                            g_get_home_dir (),
                                                            priv->id + 1);

            new_bg = clutter_texture_new_from_file (cached_background_image_file,
                                                    &error_portrait);
          }
      }
    else
      {
#if defined(MAEMO_CHANGES) && defined(UPSTREAM_DISABLED)
        GdkPixbuf        *pixbuf;

        /* Load image directly. We actually want to dither it on the fly to
         * 16 bit, and clutter doesn't do this for us so we implement a very
         * quick dither here. */
        if(!i) 
          pixbuf = gdk_pixbuf_new_from_file (cached_background_image_file, &error);
        else
          pixbuf = gdk_pixbuf_new_from_file (cached_background_image_file, &error_portrait);
          
        if (pixbuf != NULL)
          {
            gboolean          has_alpha;
            gint              width;
            gint              height;
            gint              rowstride;
            gint              n_channels;
            guchar           *pixels;
            gushort          *out_pixels, *out;
            guint             lfsr = 1;
            gint x,y;

            /* Get pixbuf properties */
            has_alpha       = gdk_pixbuf_get_has_alpha (pixbuf);
            width           = gdk_pixbuf_get_width (pixbuf);
            height          = gdk_pixbuf_get_height (pixbuf);
            rowstride       = gdk_pixbuf_get_rowstride (pixbuf);
            n_channels      = gdk_pixbuf_get_n_channels (pixbuf);
            pixels          = gdk_pixbuf_get_pixels (pixbuf);

            if (gdk_pixbuf_get_bits_per_sample (pixbuf)==8 &&
                (n_channels==3 || n_channels==4))
              {
                out_pixels = g_malloc(width*height*2);
                out = out_pixels;
                for (y=0;y<height;y++) {
                  for (x=0;x<width;x++) {
                    /* http://en.wikipedia.org/wiki/Linear_feedback_shift_register */
                    lfsr = (lfsr >> 1) ^ (unsigned int)((0 - (lfsr & 1u)) & 0xd0000001u);
  
                    /* dither 565 - by adding random noise and then truncating
                     * (r>>8)*0xFF makes sure our bottom 8 bits are 0xFF if we
                     * overflow.
                     */
                    guint r,g,b;
                    r = pixels[0] + (lfsr&7);
                    r |= (r>>8)*0xFF;
                    g = pixels[1] + ((lfsr>>3)&3);
                    g |= (g>>8)*0xFF;
                    b = pixels[2] + ((lfsr>>5)&7);
                    b |= (b>>8)*0xFF;
                    *out = ((r<<8)&0xF800) |
                           ((g<<3)&0x07E0) |
                           ((b>>3)&0x001F);

                    pixels += n_channels;
                    out++;
                  }
                  pixels += rowstride - width*n_channels;
                }
                new_bg = clutter_texture_new();

                if(!i) 
                  {
                    clutter_texture_set_from_rgb_data(CLUTTER_TEXTURE(new_bg),
                          (guchar*)out_pixels, FALSE,
                          width, height, width*2, 2, CLUTTER_TEXTURE_NONE, &error);
                  }
                else 
                  {
                    clutter_texture_set_from_rgb_data(CLUTTER_TEXTURE(new_bg),
                          (guchar*)out_pixels, FALSE,
                          width, height, width*2, 2, CLUTTER_TEXTURE_NONE, &error_portrait);
                  }

                g_free(out_pixels);
              }
            g_object_unref (pixbuf);
          }
#else
      new_bg = clutter_texture_new_from_file (cached_background_image_file, &error);
#endif
      }

    if(!i) 
      {
        if (!new_bg)
          {
            g_warning ("Error loading cached background image %s. %s",
                       cached_background_image_file,
                       error?error->message:"");
            if (error)
              g_error_free (error);
          }
      }
    else
      {
        if (!new_bg)
          {
            g_warning ("Error loading cached portrait background image %s. %s",
                       cached_background_image_file,
                       error_portrait?error_portrait->message:"");
            if (error_portrait)
              g_error_free (error_portrait);
          }
      }
  
    g_free (cached_background_image_file);

    set_background_common (self, new_bg);  
    priv->load_background_source = 0;
  }

  priv->is_portrait = FALSE;

  return FALSE;
}

/* Use Window as background, mostly copied from above.
 * 1) client != NULL means setting live-bg for this view.
 * 2) client == NULL means unsetting the live-bg for this view. */
void
hd_home_view_set_live_bg (HdHomeView *view,
                          MBWindowManagerClient *client,
                          gboolean above_applets)
{
  HdHomeViewPrivate *priv = view->priv;
  ClutterActor *new_bg = 0;
  MBWMCompMgrClutterClient *cclient;

  if (priv->load_background_source && !above_applets)
    {
      /* cancel ongoing background loading job unless we have transparent
       * live background */
      g_source_remove (priv->load_background_source);
      priv->load_background_source = 0;
    }

  if (client) 
    {
      if (priv->live_bg)
        /* remove the old one */
        hd_home_view_set_live_bg (view, NULL, FALSE);

      cclient = MB_WM_COMP_MGR_CLUTTER_CLIENT (client->cm_client);
      new_bg = mb_wm_comp_mgr_clutter_client_get_actor (cclient);
      clutter_actor_set_reactive (new_bg, FALSE);
      priv->live_bg = client;
      /* the actor is already parented to something, so reparent */
      if (above_applets)
        clutter_actor_reparent (new_bg, priv->applets_container);
      else
        clutter_actor_reparent (new_bg, priv->background_container);

      if (!priv->background && above_applets)
        /* use normal background below the live-bg */
        hd_home_view_load_background (view);
    }
  else
    {
      /* remove it */
      client = priv->live_bg;
      if (client)
        {
          cclient = MB_WM_COMP_MGR_CLUTTER_CLIENT (client->cm_client);
          new_bg = mb_wm_comp_mgr_clutter_client_get_actor (cclient);
          /* it is in either container */
          clutter_actor_remove_child (priv->background_container, new_bg);
          clutter_actor_remove_child (CLUTTER_ACTOR (priv->applets_container),
                                      new_bg);
          clutter_actor_hide (new_bg);
        }
      priv->live_bg = NULL;
      /* create black background to replace priv->background */
      new_bg = priv->background = NULL;
    }

  if (!client || !above_applets)
    set_background_common (view, new_bg);
}

void
hd_home_view_load_background (HdHomeView *view)
{
  HdHomeViewPrivate *priv;
  gint priority = G_PRIORITY_DEFAULT_IDLE;
  g_return_if_fail (HD_IS_HOME_VIEW (view));

  priv = view->priv;

  /* Check current home view and increase priority if this is the current one */
  if (hd_home_view_container_get_current_view (priv->view_container) == priv->id)
    priority = G_PRIORITY_HIGH_IDLE;

  priv->load_background_source = g_idle_add_full (priority,
                                                  load_background_idle,
                                                  view,
                                                  NULL);
}

static void
hd_home_view_set_property (GObject       *object,
			   guint         prop_id,
			   const GValue *value,
			   GParamSpec   *pspec)
{
  HdHomeView        *self = HD_HOME_VIEW (object);
  HdHomeViewPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_COMP_MGR:
      priv->comp_mgr = g_value_get_pointer (value);
      break;
    case PROP_HOME:
      priv->home = g_value_get_pointer (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_int (value);
      break;
    case PROP_CONTAINER:
      priv->view_container = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
hd_home_view_get_property (GObject      *object,
			   guint         prop_id,
			   GValue       *value,
			   GParamSpec   *pspec)
{
  HdHomeView *view = HD_HOME_VIEW (object);
  HdHomeViewPrivate *priv = view->priv;

  switch (prop_id)
    {
    case PROP_COMP_MGR:
      g_value_set_pointer (value, priv->comp_mgr);
      break;
    case PROP_HOME:
      g_value_set_pointer (value, priv->home);
      break;
    case PROP_ID:
      g_value_set_int (value, priv->id);
      break;
    case PROP_ACTIVE:
      g_value_set_boolean (value, hd_home_view_container_get_active (priv->view_container,
                                                                     priv->id));
      break;
    case PROP_CONTAINER:
      g_value_set_object (value, priv->view_container);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

guint
hd_home_view_get_view_id (HdHomeView *view)
{
  HdHomeViewPrivate *priv = view->priv;

  return priv->id;
}

static void
hd_home_view_applet_resize (ClutterActor *applet,
                            gpointer unused, HdHomeView *view)
{
  HdHomeViewAppletData *data;

  if (!(data = g_hash_table_lookup (view->priv->applets, applet)))
    return;
  clutter_actor_set_position (data->close_button,
                         clutter_actor_get_width (applet)
                         - clutter_actor_get_width (data->close_button),
                         0);
  if (data->configure_button)
    clutter_actor_set_position (data->configure_button, 0,
                       clutter_actor_get_height (applet)
                       - clutter_actor_get_height (data->close_button));
}

static gboolean
_hd_home_view_applet_motion (ClutterActor       *applet,
                             gfloat event_x, gfloat event_y,
                             HdHomeView         *view)
{
  HdHomeViewPrivate *priv = view->priv;
  gfloat x, y, w, h;

  /* Check if it is still a tap or already a move */
  if (priv->applet_motion_tap)
    {
      if (ABS (priv->applet_motion_start_x - event_x) > MAX_TAP_DISTANCE ||
          ABS (priv->applet_motion_start_y - event_y) > MAX_TAP_DISTANCE)
        priv->applet_motion_tap = FALSE;
      else
        return FALSE;
    }

  hd_home_show_edge_indication (priv->home);

  /* New position of applet actor based on movement */
  x = priv->applet_motion_start_position_x + event_x - priv->applet_motion_start_x;
  y = priv->applet_motion_start_position_y + event_y - priv->applet_motion_start_y;

  /* Get size of home view and applet actor */
  clutter_actor_get_size (applet, &w, &h);

  /* Restrict new applet actor position to allowed values */
  if (!hd_home_view_container_get_previous_view (HD_HOME_VIEW_CONTAINER (priv->view_container)) ||
      !hd_home_view_container_get_next_view (HD_HOME_VIEW_CONTAINER (priv->view_container)))
    {
      if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
        x = MAX (MIN (x, (gint) HD_COMP_MGR_LANDSCAPE_WIDTH - ((gint) w)), 0);
      else
        x = MAX (MIN (x, (gint) HD_COMP_MGR_PORTRAIT_WIDTH - ((gint) w)), 0);
    }
  if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
    {
      y = MAX (MIN (y, (gint) HD_COMP_MGR_LANDSCAPE_HEIGHT - ((gint) h)),
               HD_COMP_MGR_TOP_MARGIN);
    }
  else
    {
      y = MAX (MIN (y, (gint) HD_COMP_MGR_PORTRAIT_HEIGHT - ((gint) h)),
               HD_COMP_MGR_TOP_MARGIN);
    }

  /* Update applet actor position */
  clutter_actor_set_position (applet, x, y);
  if (hd_transition_get_int ("edit_mode",
                             "snap_to_grid_while_move",
                             1))
    snap_widget_to_grid (applet);

  /* Check if this is the only active Home view */
  if (!hd_home_view_container_get_previous_view (HD_HOME_VIEW_CONTAINER (priv->view_container)) ||
      !hd_home_view_container_get_next_view (HD_HOME_VIEW_CONTAINER (priv->view_container)))
    {
      return FALSE;
    }

  /*
   * If the "drag cursor" entered the left/right indication area, highlight the indication.
   */
  priv->move_applet_left = FALSE;
  priv->move_applet_right = FALSE;

  if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
    {
      if (event_x < HD_EDGE_INDICATION_WIDTH)
        priv->move_applet_left = TRUE;
      else if (event_x > HD_COMP_MGR_LANDSCAPE_WIDTH - HD_EDGE_INDICATION_WIDTH)
        priv->move_applet_right = TRUE;
    }
  else
    {
      if(hd_home_get_vertical_scrolling (priv->home))
        {
          if (event_y < HD_EDGE_INDICATION_WIDTH + HD_COMP_MGR_TOP_MARGIN)
            priv->move_applet_left = TRUE;
          else if (event_y > HD_COMP_MGR_PORTRAIT_HEIGHT - HD_EDGE_INDICATION_WIDTH)
            priv->move_applet_right = TRUE;
        }
      else
        {
          if (event_x < HD_EDGE_INDICATION_WIDTH)
            priv->move_applet_left = TRUE;
          else if (event_x > HD_COMP_MGR_PORTRAIT_WIDTH - HD_EDGE_INDICATION_WIDTH)
            priv->move_applet_right = TRUE;
        }
    }

  hd_home_highlight_edge_indication (priv->home, priv->move_applet_left, priv->move_applet_right);

  return FALSE;
}

static gboolean
hd_home_view_applet_motion (ClutterActor       *applet,
                            ClutterMotionEvent *event,
                            HdHomeView         *view)
{
  return _hd_home_view_applet_motion (applet, event->x, event->y, view);
}

static gboolean
_hd_home_view_applet_press (ClutterActor       *applet,
                            gfloat event_x, gfloat event_y,
                            HdHomeView         *view)
{
  HdHomeViewPrivate *priv = view->priv;
  gchar *modified_key, *modified;
  HdHomeApplet *wm_applet;
  MBWindowManagerClient *desktop_client;
  HdHomeViewAppletData *data;
  GError *error = NULL;

  /* Get all pointer events */
  clutter_grab_pointer (applet);

  desktop_client = hd_comp_mgr_get_desktop_client (HD_COMP_MGR (priv->comp_mgr));

  data = g_hash_table_lookup (priv->applets, applet);

  wm_applet = HD_HOME_APPLET (data->cc->wm_client);

  data->motion_cb = g_signal_connect (applet, "motion-event",
                                      G_CALLBACK (hd_home_view_applet_motion),
                                      view);

  /* Raise the applet */
  clutter_actor_raise_top (applet);

  /* Store the modifed time of the applet */
  time (&wm_applet->modified);

  modified = g_strdup_printf ("%ld", wm_applet->modified);

  modified_key = g_strdup_printf (GCONF_KEY_MODIFIED, wm_applet->applet_id);

  gconf_client_set_string (priv->gconf_client,
                           modified_key,
                           modified,
                           &error);
  if (G_UNLIKELY (error))
    {
      g_warning ("%s. Could not set GConf key/value. %s",
                 __FUNCTION__,
                 error->message);
      g_clear_error (&error);
    }
  g_free (modified);
  g_free (modified_key);

  gconf_client_suggest_sync (priv->gconf_client,
                             &error);
  if (G_UNLIKELY (error))
    {
      g_warning ("%s. Could not sync GConf. %s",
                 __FUNCTION__,
                 error->message);
      g_clear_error (&error);
    }


  mb_wm_client_stacking_mark_dirty (desktop_client);

  priv->applet_motion_start_x = event_x;
  priv->applet_motion_start_y = event_y;

  clutter_actor_get_position (applet,
                              &priv->applet_motion_start_position_x,
                              &priv->applet_motion_start_position_y);

  priv->applet_motion_tap = TRUE;

  priv->move_applet_left = FALSE;
  priv->move_applet_right = FALSE;

  return FALSE;
}

static gboolean
hd_home_view_applet_press (ClutterActor       *applet,
                           ClutterButtonEvent *event,
                           HdHomeView         *view)
{
  return _hd_home_view_applet_press(applet, event->x, event->y, view);
}

#define SNAP_GRID_SIZE_DEFAULT 4

static gint
snap_coordinate_to_grid (gint coordinate)
{
  gint snap_grid_size, offset;
 
  snap_grid_size  = hd_transition_get_int ("edit_mode",
                                           "snap_grid_size",
                                           SNAP_GRID_SIZE_DEFAULT);

  /* Avoid hildon-desktop crash, when snap_grid_size == 0 */
  if(snap_grid_size < 1)
    snap_grid_size = SNAP_GRID_SIZE_DEFAULT;

  offset = coordinate % snap_grid_size;

  if (offset > snap_grid_size / 2)
    return coordinate - offset + snap_grid_size;
  else
    return coordinate - offset;
}

static void
snap_widget_to_grid (ClutterActor *widget)
{
  ClutterGeometry c_geom;

  /* Get applet size and position */
  clutter_actor_get_geometry (widget, &c_geom);

  c_geom.x = snap_coordinate_to_grid (c_geom.x);
  c_geom.y = snap_coordinate_to_grid (c_geom.y);

  clutter_actor_set_position (widget, c_geom.x, c_geom.y);
}

static void
hd_home_view_store_applet_position (HdHomeView   *view,
                                    ClutterActor *applet,
                                    gint          old_x,
                                    gint          old_y)
{
  HdHomeViewPrivate *priv = view->priv;
  HdHomeViewAppletData *data;
  ClutterGeometry c_geom;
  MBGeometry mb_geom;

  data = g_hash_table_lookup (priv->applets, applet);

  snap_widget_to_grid (applet);

  /* Get applet size and position */
  clutter_actor_get_geometry (applet, &c_geom);

  /* Move into allowed area */
	if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
	{
 		c_geom.x = MAX (MIN (c_geom.x,
  	                    (gint) HD_COMP_MGR_LANDSCAPE_WIDTH - ((gint) c_geom.width)),
                  	0);
  	c_geom.y = MAX (MIN (c_geom.y,
                       	(gint) HD_COMP_MGR_LANDSCAPE_HEIGHT - ((gint) c_geom.height)),
                  	HD_COMP_MGR_TOP_MARGIN);
	}
	else
	{
  	c_geom.x = MAX (MIN (c_geom.x,
                       	(gint) HD_COMP_MGR_PORTRAIT_WIDTH - ((gint) c_geom.width)),
                  	0);
  	c_geom.y = MAX (MIN (c_geom.y,
                       	(gint) HD_COMP_MGR_PORTRAIT_HEIGHT - ((gint) c_geom.height)),
                  	HD_COMP_MGR_TOP_MARGIN);
	}

  clutter_actor_set_position (applet, c_geom.x, c_geom.y);

  /* Move the underlying window to match the actor's position */
  mb_geom.x = c_geom.x;
  mb_geom.y = c_geom.y;
  mb_geom.width = c_geom.width;
  mb_geom.height = c_geom.height;

  mb_wm_client_request_geometry (data->cc->wm_client,
                                 &mb_geom,
                                 MBWMClientReqGeomIsViaUserAction);

  if (old_x != c_geom.x ||
      old_y != c_geom.y)
    {
      const gchar *applet_id;
      gchar *position_key;
      GSList *position_value;
      GError *error = NULL;

      applet_id = HD_HOME_APPLET (data->cc->wm_client)->applet_id;

			if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
			{
	      position_key = g_strdup_printf (GCONF_KEY_POSITION, applet_id);
			}
			else
			{
				position_key = g_strdup_printf (GCONF_KEY_POSITION_PORTRAIT, applet_id);
			}

      position_value = g_slist_prepend (g_slist_prepend (NULL,
                                                         GINT_TO_POINTER (c_geom.y)),
                                        GINT_TO_POINTER (c_geom.x));
      gconf_client_set_list (priv->gconf_client,
                             position_key,
                             GCONF_VALUE_INT,
                             position_value,
                             &error);
      if (G_UNLIKELY (error))
        {
          g_warning ("Could not store new applet position for applet %s to GConf. %s",
                     applet_id,
                     error->message);
          g_clear_error (&error);
        }

      gconf_client_suggest_sync (priv->gconf_client,
                                 &error);
      if (G_UNLIKELY (error))
        {
          g_warning ("%s. Could not sync GConf. %s",
                     __FUNCTION__,
                     error->message);
          g_clear_error (&error);
        }

      g_free (position_key);
      g_slist_free (position_value);
    }
}

static gboolean
_hd_home_view_applet_release (ClutterActor       *applet,
                              gfloat event_x, gfloat event_y,
                              HdHomeView         *view)
{
  HdHomeViewPrivate *priv = view->priv;
  HdHomeViewAppletData *data;

/*  g_debug ("%s: %d, %d", __FUNCTION__, event_x, event_y); */

  /* Get all pointer events */
  clutter_ungrab_pointer ();

  data = g_hash_table_lookup (priv->applets, applet);

  if (data->motion_cb)
    {
      g_signal_handler_disconnect (applet, data->motion_cb);
      data->motion_cb = 0;
    }

  /*
   * If this was a simple press/release, with no intervening pointer motion,
   * emit the applet-clicked signal.
   */
  if (!priv->applet_motion_tap)
    {
      /* Hide switching edges */
      hd_home_hide_edge_indication (priv->home);

      if (priv->move_applet_left || priv->move_applet_right)
        {
          /* Applet should be moved to another view */
          ClutterActor *new_view;

          if (priv->move_applet_left)
            new_view = hd_home_view_container_get_previous_view (
                            HD_HOME_VIEW_CONTAINER (priv->view_container));
          else
            new_view = hd_home_view_container_get_next_view (
                            HD_HOME_VIEW_CONTAINER (priv->view_container));

          if (new_view)
            hd_home_view_move_applet (view, HD_HOME_VIEW (new_view), applet);
          else
            g_warning ("%s: new_view is NULL", __func__);

          if (priv->move_applet_left)
            hd_home_view_container_scroll_to_previous (
                            HD_HOME_VIEW_CONTAINER (priv->view_container), 0);
          else
            hd_home_view_container_scroll_to_next (
                            HD_HOME_VIEW_CONTAINER (priv->view_container), 0);
        }
      else
        {
          /*
           * Applet should be moved in this view
           * Move the underlying window to match the actor's position
           */
          hd_home_view_store_applet_position (view,
                                              applet,
                                              -1,
                                              -1);
          hd_home_view_layout_reset (priv->layout);
        }
    }

  return TRUE;
}

static gboolean
hd_home_view_applet_release (ClutterActor       *applet,
                             ClutterButtonEvent *event,
                             HdHomeView         *view)
{
  return _hd_home_view_applet_release(applet, event->x, event->y, view);
}

static gboolean
hd_home_view_applet_touched (ClutterActor      *applet,
                             ClutterTouchEvent *event,
                             HdHomeView        *view)
{
    switch(event->type)
      {
        case CLUTTER_TOUCH_BEGIN:
          return _hd_home_view_applet_press(applet, event->x, event->y, view);
        case CLUTTER_TOUCH_UPDATE:
          return _hd_home_view_applet_motion(applet, event->x, event->y, view);
        case CLUTTER_TOUCH_END:
          return _hd_home_view_applet_release(applet, event->x, event->y, view);
        default:
            break;
      }

    return CLUTTER_EVENT_PROPAGATE;
}


static gint
cmp_applet_modified (gconstpointer a,
                     gconstpointer b)
{
  const MBWMCompMgrClient *cc_a = a;
  const MBWMCompMgrClient *cc_b = b;

  return HD_HOME_APPLET (cc_a->wm_client)->modified
          - HD_HOME_APPLET (cc_b->wm_client)->modified;
}

/* Return the list of CompMgrClients of the applets this homeview
 * manages sorted by their last modification time.   It's up to you
 * to free the list. */
GSList *
hd_home_view_get_all_applets (HdHomeView *view)
{
  HdHomeViewPrivate *priv = view->priv;
  GSList *sorted;
  GHashTableIter iter;
  gpointer tmp;

  /* Get a list of all applets sorted by modified time */
  sorted = NULL;
  g_hash_table_iter_init (&iter, priv->applets);
  while (g_hash_table_iter_next (&iter, NULL, &tmp))
  {
    HdHomeViewAppletData *value = tmp;
    sorted = g_slist_insert_sorted (sorted, value->cc, cmp_applet_modified);
  }
  return sorted;
}

static void
hd_home_view_restack_applets (HdHomeView *view)
{
  GSList *sorted = NULL, *s;

  /* Get a list of all applets sorted by modified time
   * and raise them in the order of the list. */
  sorted = hd_home_view_get_all_applets (view);
  for (s = sorted; s; s = s->next)
    {
      MBWMCompMgrClutterClient *cc = s->data;
      ClutterActor *actor = mb_wm_comp_mgr_clutter_client_get_actor (cc);

      clutter_actor_raise_top (actor);
    }
  g_slist_free (sorted);
}

static void
hd_home_view_load_applet_position (HdHomeView           *view,
                                   ClutterActor         *applet,
                                   HdHomeViewAppletData *data,
                                   gboolean              force_arrange,
                                   gint                 *old_x,
                                   gint                 *old_y)
{
  HdHomeViewPrivate *priv = view->priv;
  const gchar *applet_id;
  gchar *position_key;
  GSList *position;

  applet_id = HD_HOME_APPLET (data->cc->wm_client)->applet_id;
	
	if(!STATE_IS_PORTRAIT (hd_render_manager_get_state())){
	  position_key = g_strdup_printf (GCONF_KEY_POSITION, applet_id);
	}
	else
	{
		position_key = g_strdup_printf (GCONF_KEY_POSITION_PORTRAIT, applet_id);
	}

  position = gconf_client_get_list (priv->gconf_client,
                                    position_key,
                                    GCONF_VALUE_INT,
                                    NULL);

  if (!force_arrange && position && position->next)
    {
      clutter_actor_set_position (applet,
                                  GPOINTER_TO_INT (position->data),
                                  GPOINTER_TO_INT (position->next->data));

      if (old_x)
        *old_x = GPOINTER_TO_INT (position->data);

      if (old_y)
        *old_y = GPOINTER_TO_INT (position->next->data);

      hd_home_view_layout_reset (priv->layout);
    }
  else
    {
      GSList *applets = NULL;
      GHashTableIter iter;
      gpointer tmp;

      /* Get a list of all applets */
      g_hash_table_iter_init (&iter, priv->applets);
      while (g_hash_table_iter_next (&iter, NULL, &tmp))
        {
          HdHomeViewAppletData *value = tmp;
          applets = g_slist_prepend (applets, value->actor);
        }

      hd_home_view_layout_arrange_applet (priv->layout,
                                          applets,
                                          applet);

      g_slist_free (applets);
    }

  g_free (position_key);
  g_slist_free (position);
}

static void
close_applet (HdHomeView *view, HdHomeViewAppletData *data)
{
  HdHomeViewPrivate *priv = view->priv;
  const gchar *applet_id;
  gchar *applet_key;

  /* Hide clutter actor */
  clutter_actor_hide (data->actor);

  /* Unset GConf configuration */
  applet_id = HD_HOME_APPLET (data->cc->wm_client)->applet_id;

  applet_key = g_strdup_printf ("/apps/osso/hildon-desktop/applets/%s", applet_id);
  gconf_client_recursive_unset (priv->gconf_client, applet_key, 0, NULL);
  g_free (applet_key);

  mb_wm_client_deliver_delete (data->cc->wm_client);
}

static gboolean
close_button_pressed (ClutterActor       *button,
                     ClutterButtonEvent *event,
                     HdHomeView         *view)
{
  HdHomeViewPrivate *priv = view->priv;

  priv->applet_motion_start_x = event->x;
  priv->applet_motion_start_y = event->y;

  return TRUE;
}

static gboolean
close_button_released (ClutterActor       *button,
                       ClutterButtonEvent *event,
                       HdHomeView         *view)
{
  HdHomeViewPrivate *priv = view->priv;
  ClutterActor *applet;
  HdHomeViewAppletData *data;

  if (ABS (priv->applet_motion_start_x - event->x) > MAX_TAP_DISTANCE ||
      ABS (priv->applet_motion_start_y - event->y) > MAX_TAP_DISTANCE)
    return TRUE;

  applet = clutter_actor_get_parent (button);

  data = g_hash_table_lookup (priv->applets, applet);

  close_applet (view, data);

  return TRUE;
}

static gboolean
close_button_touched (ClutterActor       *button,
                      ClutterButtonEvent *event,
                      HdHomeView         *view)
{
    switch (event->type) {
        case CLUTTER_TOUCH_BEGIN:
            return close_button_pressed(button, event, view);
        case CLUTTER_TOUCH_END:
            return close_button_released(button, event, view);
        default:
            break;
    }

    return CLUTTER_EVENT_PROPAGATE;
}

static gboolean
configure_button_clicked (ClutterActor       *button,
                          ClutterButtonEvent *event,
                          HdHomeView         *view)
{
  HdHomeViewPrivate *priv = view->priv;
  ClutterActor *applet;
  HdHomeViewAppletData *data;
  HdHomeApplet *wm_applet;

  applet = clutter_actor_get_parent (button);

  data = g_hash_table_lookup (priv->applets, applet);

  wm_applet = HD_HOME_APPLET (data->cc->wm_client);

  if (wm_applet->settings)
    {
      HdCompMgr *hmgr = HD_COMP_MGR (priv->comp_mgr);

      mb_wm_client_deliver_message (data->cc->wm_client,
                                    hd_comp_mgr_get_atom (hmgr, HD_ATOM_HILDON_APPLET_SHOW_SETTINGS),
                                    0, 0, 0, 0, 0);
    }

  return TRUE;
}

void
hd_home_view_add_applet (HdHomeView   *view,
                         ClutterActor *applet,
                         gboolean      force_arrange)
{
  HdHomeViewPrivate *priv = view->priv;
  HdHomeViewAppletData *data;
  ClutterActor *close_button;
  MBWindowManagerClient *desktop;
  gint old_x = -1, old_y = -1;

  /*
   * Reparent the applet to ourselves; note that this automatically
   * gets us the correct position within the view.
   */
  clutter_actor_reparent (applet, priv->applets_container);
  clutter_actor_set_reactive (applet, TRUE);

  data = applet_data_new (applet);

  /* Add close button */
  close_button = hd_clutter_cache_get_texture ("AppletCloseButton.png", TRUE);
  clutter_actor_add_child (CLUTTER_ACTOR (applet), close_button);

  clutter_actor_set_reactive (close_button, TRUE);
  clutter_actor_raise_top (close_button);
  if (!STATE_IN_EDIT_MODE (hd_render_manager_get_state ()))
    clutter_actor_hide (close_button);
  g_signal_connect (close_button, "button-press-event",
                    G_CALLBACK (close_button_pressed), view);
  g_signal_connect (close_button, "button-release-event",
                    G_CALLBACK (close_button_released), view);
  g_signal_connect (close_button, "touch-event",
                    G_CALLBACK (close_button_touched), view);
  data->close_button = close_button;

  /* Add configure button */
  if (HD_HOME_APPLET (data->cc->wm_client)->settings)
    {
      ClutterActor *configure_button;

      configure_button = hd_clutter_cache_get_texture ("AppletConfigureButton.png", TRUE);
      clutter_actor_add_child (CLUTTER_ACTOR (applet), configure_button);

      clutter_actor_set_reactive (configure_button, TRUE);
      clutter_actor_raise_top (configure_button);
      if (!STATE_IN_EDIT_MODE (hd_render_manager_get_state ()))
        clutter_actor_hide (configure_button);
      g_signal_connect (configure_button, "button-press-event",
                        G_CALLBACK (configure_button_clicked), view);
      data->configure_button = configure_button;
    }

  data->release_cb = g_signal_connect (applet, "button-release-event",
                                       G_CALLBACK (hd_home_view_applet_release), view);
  data->press_cb = g_signal_connect (applet, "button-press-event",
                                     G_CALLBACK (hd_home_view_applet_press), view);

  data->touch_cb = g_signal_connect (applet, "touch-event",
                                     G_CALLBACK (hd_home_view_applet_touched), view);

  data->resize_cb = g_signal_connect (applet, "notify::allocation",
                                      G_CALLBACK (hd_home_view_applet_resize), view);

  g_object_set_data (G_OBJECT (applet), "HD-HomeView", view);

  hd_home_view_load_applet_position (view,
                                     applet,
                                     data,
                                     force_arrange,
                                     &old_x,
                                     &old_y);

  g_hash_table_insert (priv->applets,
                       applet,
                       data);

  hd_home_view_store_applet_position (view,
                                      applet,
                                      old_x,
                                      old_y);
  hd_home_view_applet_resize (applet, NULL, view);

  desktop = hd_comp_mgr_get_desktop_client (HD_COMP_MGR (priv->comp_mgr));
  if (desktop)
    { /* Synchronize here, we may not come from clutter_x11_event_filter()
       * at all. */
      mb_wm_client_stacking_mark_dirty (desktop);
      mb_wm_sync (MB_WM_COMP_MGR (priv->comp_mgr)->wm);
    }
  hd_home_view_restack_applets (view);
}

void
hd_home_view_change_applets_position (HdHomeView *view)
{
  HdHomeViewPrivate *priv;
  GHashTableIter iter;
  gpointer value, key;
	gint x, y;

  g_return_if_fail (HD_IS_HOME_VIEW (view));

  priv = view->priv;

  /* Iterate over all applets */
  g_hash_table_iter_init (&iter, priv->applets);

  while (g_hash_table_iter_next (&iter, &key, &value))
    {
			x = -1;
			y = -1;
 		  hd_home_view_load_applet_position (view,
		                                     (ClutterActor *) key,
		                                     (HdHomeViewAppletData *) value,
		                                     FALSE,
		                                     &x,
		                                     &y);

		  hd_home_view_store_applet_position (view,
		                                      (ClutterActor *) key,
		                                      x,
		                                      y);			
    }
 }

void
hd_home_view_unregister_applet (HdHomeView *view, ClutterActor *applet)
{
  HdHomeViewPrivate *priv = view->priv;

  g_hash_table_remove (priv->applets, applet);

  hd_home_view_layout_reset (priv->layout);
}

void
hd_home_view_move_applet (HdHomeView   *view,
			  HdHomeView   *new_view,
			  ClutterActor *applet)
{
  HdHomeViewPrivate *priv = view->priv;
  HdHomeViewAppletData *data;
  HdHomeApplet *wm_applet;
  gchar *position_key, *view_key;
  GError *error = NULL;
  MBWindowManagerClient *desktop_client;

  data = g_hash_table_lookup (priv->applets, applet);

  /* Update view for WM window */
  wm_applet = HD_HOME_APPLET (data->cc->wm_client);
  wm_applet->view_id = hd_home_view_get_view_id (new_view);

  /* Reset position in GConf*/
	if(!STATE_IS_PORTRAIT (hd_render_manager_get_state()))
	  position_key = g_strdup_printf (GCONF_KEY_POSITION, wm_applet->applet_id);
	else
		position_key = g_strdup_printf (GCONF_KEY_POSITION_PORTRAIT, wm_applet->applet_id);

  gconf_client_unset (priv->gconf_client, position_key, &error);
  if (G_UNLIKELY (error))
    {
      g_warning ("Could not unset GConf key %s. %s", position_key, error->message);
      error = (g_error_free (error), NULL);
    }
  g_free (position_key);

  /* Update view in GConf */
	view_key = g_strdup_printf (GCONF_KEY_VIEW, wm_applet->applet_id);

  gconf_client_set_int (priv->gconf_client,
                        view_key,
                        wm_applet->view_id + 1,
                        &error);
  if (G_UNLIKELY (error))
    {
      g_warning ("%s. Could not set GConf key/value. %s",
                 __FUNCTION__,
                 error->message);
      g_clear_error (&error);
    }
  g_free (view_key);

  gconf_client_suggest_sync (priv->gconf_client,
                             &error);
  if (G_UNLIKELY (error))
    {
      g_warning ("%s. Could not sync GConf. %s",
                 __FUNCTION__,
                 error->message);
      g_clear_error (&error);
    }

  /* Unregister from old view */
  hd_home_view_unregister_applet (view, applet);

  /* Add applet to the new view */
  hd_home_view_add_applet (new_view, applet, TRUE);

  /* Mark desktop for restacking (because the wm window was moved) */
  desktop_client = hd_comp_mgr_get_desktop_client (HD_COMP_MGR (priv->comp_mgr));
  mb_wm_client_stacking_mark_dirty (desktop_client);
}

MBWindowManagerClient *
hd_home_view_get_live_bg (HdHomeView *view)
{
  HdHomeViewPrivate *priv = view->priv;

  return priv->live_bg;
}

gboolean
hd_home_view_get_active (HdHomeView *view)
{
  g_return_val_if_fail (HD_IS_HOME_VIEW (view), FALSE);

  return hd_home_view_container_get_active (view->priv->view_container,
                                            view->priv->id);
}

void
hd_home_view_close_all_applets (HdHomeView *view)
{
  HdHomeViewPrivate *priv;
  GHashTableIter iter;
  gpointer value;

  g_return_if_fail (HD_IS_HOME_VIEW (view));

  priv = view->priv;

  /* Iterate over all applets */
  g_hash_table_iter_init (&iter, priv->applets);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      close_applet (view, (HdHomeViewAppletData *) value);
    }
 }

void
hd_home_view_update_state (HdHomeView *view)
{
  HdHomeViewPrivate *priv;
  GHashTableIter iter;
  gpointer value;

  g_return_if_fail (HD_IS_HOME_VIEW (view));

  priv = view->priv;

  /* Iterate over all applets */
  g_hash_table_iter_init (&iter, priv->applets);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      HdHomeViewAppletData *data = value;

      if (STATE_IN_EDIT_MODE (hd_render_manager_get_state ()))
        {
          if (data->close_button)
            clutter_actor_show (data->close_button);
          if (data->configure_button)
            clutter_actor_show (data->configure_button);
        }
      else
        {
          if (data->close_button)
            clutter_actor_hide (data->close_button);
          if (data->configure_button)
            clutter_actor_hide (data->configure_button);
        }
    }
}

static HdHomeViewAppletData *
applet_data_new (ClutterActor *actor)
{
  HdHomeViewAppletData *data;

  data = g_slice_new0 (HdHomeViewAppletData);

  data->actor = actor;
  data->cc = g_object_get_data (G_OBJECT (actor), "HD-MBWMCompMgrClutterClient");

  return data;
}

static void
applet_data_free (HdHomeViewAppletData *data)
{
  if (G_UNLIKELY (!data))
    return;

  if (data->press_cb)
    data->press_cb = (g_signal_handler_disconnect (data->actor, data->press_cb), 0);
  if (data->touch_cb)
    data->touch_cb = (g_signal_handler_disconnect (data->actor, data->touch_cb), 0);
  if (data->release_cb)
    data->release_cb = (g_signal_handler_disconnect (data->actor, data->release_cb), 0);
  if (data->resize_cb)
    data->resize_cb = (g_signal_handler_disconnect (data->actor, data->resize_cb), 0);
  if (data->motion_cb)
    data->motion_cb = (g_signal_handler_disconnect (data->actor, data->motion_cb), 0);

  data->actor = NULL;
  data->cc = NULL;

  if (data->close_button)
    data->close_button = (clutter_actor_destroy (data->close_button), NULL);
  if (data->configure_button)
    data->configure_button = (clutter_actor_destroy (data->configure_button), NULL);

  g_slice_free (HdHomeViewAppletData, data);
}


static void
hd_home_view_allocation_changed (HdHomeView    *view,
                               GParamSpec *pspec,
                               gpointer    user_data)
{
  HdHomeViewPrivate *priv = view->priv;
  ClutterGeometry geom;

  /* We need to update the position of the applets container,
   * as it is not a child of ours. Rather than just setting
   * the X and Y to that of ourselves, we'll modify the offset
   * so that panning of the applets is non-linear relative to
   * the background. */
  clutter_actor_get_allocation_geometry (CLUTTER_ACTOR(view), &geom);

  /* For scrolling either way from the home position, make the applets
   * move faster than the background to produce the parallax effect. */
  if( STATE_IS_PORTRAIT(hd_render_manager_get_state ())
      && hd_home_get_vertical_scrolling(priv->home))
    {
      if (geom.y > -HD_COMP_MGR_LANDSCAPE_WIDTH && geom.y < 0)
        {
          geom.y = (int)(geom.y * HD_HOME_VIEW_PARALLAX_AMOUNT);
          if (geom.y < -HD_COMP_MGR_LANDSCAPE_WIDTH)
            geom.y = -HD_COMP_MGR_LANDSCAPE_WIDTH;
        }
      if (geom.y < HD_COMP_MGR_LANDSCAPE_WIDTH && geom.y > 0)
        {
          geom.y = (int)(geom.y * HD_HOME_VIEW_PARALLAX_AMOUNT);
          if (geom.y > HD_COMP_MGR_LANDSCAPE_WIDTH)
            geom.y = HD_COMP_MGR_LANDSCAPE_WIDTH;
        }
    }
  else
    {
      if (geom.x > -HD_COMP_MGR_LANDSCAPE_WIDTH && geom.x < 0)
        {
          geom.x = (int)(geom.x * HD_HOME_VIEW_PARALLAX_AMOUNT);
          if (geom.x < -HD_COMP_MGR_LANDSCAPE_WIDTH)
            geom.x = -HD_COMP_MGR_LANDSCAPE_WIDTH;
        }
      if (geom.x < HD_COMP_MGR_LANDSCAPE_WIDTH && geom.x > 0)
        {
          geom.x = (int)(geom.x * HD_HOME_VIEW_PARALLAX_AMOUNT);
          if (geom.x > HD_COMP_MGR_LANDSCAPE_WIDTH)
            geom.x = HD_COMP_MGR_LANDSCAPE_WIDTH;
        }
    }

  clutter_actor_set_position(priv->applets_container, geom.x, geom.y);
}

/* ClutterStage::notify::allocation handler to rotate a background
 * container when we're going to or coming from portrait mode. */
static void
hd_home_view_rotate_background(ClutterActor *actor, GParamSpec *unused,
                               ClutterActor *stage)
{
  gfloat w, h;

  clutter_actor_get_size (stage, &w, &h);

  if (w < h)
    { /* -> portrait */
      clutter_actor_set_pivot_point(actor, 0, 1);
      clutter_actor_set_translation(actor, w - h, -h, 0);
      clutter_actor_set_rotation_angle (actor, CLUTTER_Z_AXIS, 90);
    }
  else
    { /* -> landscape */
      clutter_actor_set_pivot_point(actor, 0, 0);
      clutter_actor_set_translation(actor, 0, 0, 0);
      clutter_actor_set_rotation_angle (actor, CLUTTER_Z_AXIS, 0);
    }

  clutter_actor_set_size(actor, w, h);
}

static void
hd_home_view_resize_applets_container(ClutterActor *actor, GParamSpec *unused,
                               ClutterActor *stage)
{

  clutter_actor_set_size(actor,
                         hd_comp_mgr_get_current_screen_width (),
                         hd_comp_mgr_get_current_screen_height ());
}

void
hd_home_view_change_wallpaper(HdHomeView *view)
{
  HdHomeViewPrivate *priv = view->priv;
  TidySubTexture *new_bg_sub = 0;
  ClutterActor *new_bg;

  if(priv->background_temp_portrait == NULL ||
      priv->background_temp == NULL)
      return;


  if(STATE_IS_PORTRAIT(hd_render_manager_get_state ()))
    {
      new_bg = priv->background_temp_portrait;
      new_bg_sub = priv->background_sub_temp_portrait;
    }
  else
    {
      new_bg = priv->background_temp;
      new_bg_sub = priv->background_sub_temp;
    }

  clutter_actor_set_name (new_bg, "HdHomeView::background");

  g_object_ref(priv->background);
  g_object_ref(priv->background_sub);

  /* Remove old background and add new to the background container */
  clutter_actor_remove_child (CLUTTER_ACTOR (priv->background_container),
                              priv->background);
  if (priv->background_sub)
    {
      clutter_actor_remove_child (CLUTTER_ACTOR (priv->background_container),
                                  CLUTTER_ACTOR(priv->background_sub));
    }

  clutter_actor_add_child (CLUTTER_ACTOR (priv->background_container), new_bg);

  if (new_bg_sub)
    clutter_actor_add_child (CLUTTER_ACTOR (priv->background_container),
                             CLUTTER_ACTOR(new_bg_sub));

  /* Only update blur if we're currently active */
  if (hd_home_view_container_get_current_view(priv->view_container) == priv->id)
    hd_render_manager_blurred_changed();

  priv->background = new_bg;
  priv->background_sub = new_bg_sub;
}
