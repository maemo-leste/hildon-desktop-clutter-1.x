/*
 * This file is part of hildon-desktop
 *
 * Copyright (C) 2013 Tomasz Pieniążek.
 *
 * Author:  Tomasz Pieniążek <t.pieniazek@gazeta.pl>
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

#include "hd-orientation-lock.h"
#include "hd-render-manager.h"

#include <gconf/gconf-client.h>

struct _HdOrientationLockPrivate
{
  /* GConf client. */
  GConfClient *gconf_client;

  gboolean orientation_lock_enabled;
  /* If TRUE, lock the window in landscape. Otherwise try to lock in portrait mode. */
  gboolean orientation_lock_landscape;
};

#define HD_ORIENTATION_LOCK_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                     HD_TYPE_ORIENTATION_LOCK, HdOrientationLockPrivate))

G_DEFINE_TYPE (HdOrientationLock, hd_orientation_lock, G_TYPE_OBJECT);

#define GCONF_KEY_ORIENTATION_LOCK "/apps/osso/hildon-desktop/orientation_lock"
/* TRUE - Portrait, FALSE - landscape. */
#define GCONF_KEY_ORIENTATION_LOCK_POSITION "/apps/osso/hildon-desktop/orientation_lock_position"
#define GCONF_OSSO_HILDON_DESKTOP_DIR "/apps/osso/hildon-desktop"

/* The HdOrientationLock singleton */
static HdOrientationLock *the_orientation_lock = NULL;


/* Forward declarations */
static void hd_orientation_lock_dispose (GObject *gobject);
static void hd_orientation_lock_gconf_value_changed (GConfClient *client,
                                                      guint cnxn_id,
                                                      GConfEntry *entry,
                                                      gpointer user_data);



HdOrientationLock *
hd_orientation_lock_get (void)
{
  if (G_UNLIKELY (!the_orientation_lock))
    { /* "Protect" against reentrancy. */
      static gboolean under_construction;

      g_assert(!under_construction);
      under_construction = TRUE;
      the_orientation_lock = g_object_new (HD_TYPE_ORIENTATION_LOCK, NULL);
    }
  return the_orientation_lock;
}

static void
hd_orientation_lock_class_init (HdOrientationLockClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (HdOrientationLockPrivate));

  gobject_class->dispose = hd_orientation_lock_dispose;
}

static void
hd_orientation_lock_init (HdOrientationLock *self)
{
  HdOrientationLockPrivate *priv;

  self->priv = priv = HD_ORIENTATION_LOCK_GET_PRIVATE (self);

  /* Initialize the GConf client. */
  priv->gconf_client = gconf_client_get_default ();
  if (priv->gconf_client)
    {
      priv->orientation_lock_enabled = gconf_client_get_bool (priv->gconf_client,
                                                                GCONF_KEY_ORIENTATION_LOCK,
                                                                NULL);
      priv->orientation_lock_landscape = gconf_client_get_bool (priv->gconf_client,
                                                                  GCONF_KEY_ORIENTATION_LOCK_POSITION,
                                                                  NULL);
      gconf_client_add_dir (priv->gconf_client, GCONF_OSSO_HILDON_DESKTOP_DIR,
                             GCONF_CLIENT_PRELOAD_NONE, NULL);
      gconf_client_notify_add (priv->gconf_client, GCONF_KEY_ORIENTATION_LOCK,
                               hd_orientation_lock_gconf_value_changed,
                               (gpointer) self,
                               NULL, NULL);
    }
}

static void
hd_orientation_lock_dispose (GObject *gobject)
{
  HdOrientationLock *self = HD_ORIENTATION_LOCK (gobject);
  HdOrientationLockPrivate *priv = HD_ORIENTATION_LOCK_GET_PRIVATE (self);

  if (priv->gconf_client)
    {
      gconf_client_remove_dir (priv->gconf_client, GCONF_OSSO_HILDON_DESKTOP_DIR, NULL);
      g_object_unref (G_OBJECT (priv->gconf_client));
      priv->gconf_client = NULL;
    }

  G_OBJECT_CLASS (hd_orientation_lock_parent_class)->dispose (gobject);
}

static void
hd_orientation_lock_gconf_value_changed (GConfClient *client,
                                          guint cnxn_id,
                                          GConfEntry *entry,
                                          gpointer user_data)
{
  HdOrientationLock *self = HD_ORIENTATION_LOCK (user_data);
  HdOrientationLockPrivate *priv = HD_ORIENTATION_LOCK_GET_PRIVATE (self);
  GConfValue *gvalue;
  gboolean value = FALSE;

  if (!entry)
    return;

  gvalue = gconf_entry_get_value (entry);

  if (gvalue->type == GCONF_VALUE_BOOL)
    value = gconf_value_get_bool (gvalue);

  if (!g_strcmp0 (gconf_entry_get_key (entry), GCONF_KEY_ORIENTATION_LOCK))
    {
      priv->orientation_lock_enabled = value;

      if (value)
        {
          /* Set appropriate value to the orientation_lock_landscape variable. */
          if (STATE_IS_PORTRAIT (hd_render_manager_get_state ()))
            {
              /* We are in portrait mode, so try to lock to portrait if possible. */
              priv->orientation_lock_landscape = FALSE;
            }
          else
            {
              /* We are in landscape mode, so try to lock to landscape. */
              priv->orientation_lock_landscape = TRUE;
            }

          /* Store the priv->orientation_lock_landscape variable in the GConf key. */
          gconf_client_set_bool (priv->gconf_client, GCONF_KEY_ORIENTATION_LOCK_POSITION,
                                 priv->orientation_lock_landscape, NULL);
        }

      /* Check if h-d needs to track the orientation. When we lock window
       * to portrait mode, the priv->portrait variable is set to TRUE. */
      if (STATE_IS_PORTRAIT (hd_render_manager_get_state ()) && !value)
        {
          hd_app_mgr_mce_activate_accel_if_needed (FALSE);
          hd_app_mgr_update_orientation ();
        }
  }
}

/* Returns TRUE if orientation lock is enabled. */
gboolean
hd_orientation_lock_is_enabled (void)
{
    return HD_ORIENTATION_LOCK_GET_PRIVATE (hd_orientation_lock_get ())->orientation_lock_enabled;
}

/* Returns TRUE if window should be locked to landscape mode. */
gboolean
hd_orientation_lock_is_locked_to_landscape (void)
{
  HdOrientationLockPrivate *priv = HD_ORIENTATION_LOCK_GET_PRIVATE (hd_orientation_lock_get ());
  gboolean is_enabled = priv->orientation_lock_enabled;
  gboolean to_landscape = priv->orientation_lock_landscape;

  return is_enabled && to_landscape;
}

/* Returns TRUE if window should be locked to portrait mode. */
gboolean
hd_orientation_lock_is_locked_to_portrait (void)
{
  HdOrientationLockPrivate *priv = HD_ORIENTATION_LOCK_GET_PRIVATE (hd_orientation_lock_get ());
  gboolean is_enabled = priv->orientation_lock_enabled;
  gboolean to_landscape = priv->orientation_lock_landscape;

  return is_enabled && !to_landscape;
}
