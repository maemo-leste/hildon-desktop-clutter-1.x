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

#ifndef _HAVE_HD_ORIENTATION_LOCK_H
#define _HAVE_HD_ORIENTATION_LOCK_H

#include <glib-object.h>

G_BEGIN_DECLS

#define HD_TYPE_ORIENTATION_LOCK            (hd_orientation_lock_get_type ())
#define HD_ORIENTATION_LOCK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), HD_TYPE_ORIENTATION_LOCK, HdOrientationLock))
#define HD_IS_ORIENTATION_LOCKR(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HD_TYPE_ORIENTATION_LOCK))
#define HD_ORIENTATION_LOCK_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), HD_TYPE_ORIENTATION_LOCK, HdOrientationLockClass))
#define HD_IS_ORIENTATION_LOCK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), HD_TYPE_ORIENTATION_LOCK))
#define HD_ORIENTATION_LOCK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), HD_TYPE_ORIENTATION_LOCK, HdOrientationLockClass))

typedef struct _HdOrientationLock        HdOrientationLock;
typedef struct _HdOrientationLockPrivate HdOrientationLockPrivate;
typedef struct _HdOrientationLockClass   HdOrientationLockClass;

struct _HdOrientationLock
{
  GObject parent_instance;

  HdOrientationLockPrivate *priv;
};

struct _HdOrientationLockClass
{
  GObjectClass parent_class;
};

GType hd_orientation_lock_get_type (void) G_GNUC_CONST;

HdOrientationLock   *hd_orientation_lock_get (void);

gboolean hd_orientation_lock_is_enabled (void);
gboolean hd_orientation_lock_is_locked_to_landscape (void);
gboolean hd_orientation_lock_is_locked_to_portrait (void);

G_END_DECLS


#endif //_HAVE_HD_ORIENTATION_LOCK_H
