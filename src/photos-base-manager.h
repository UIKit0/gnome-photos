/*
 * Photos - access, organize and share your photos on GNOME
 * Copyright © 2012 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef PHOTOS_BASE_MANAGER_H
#define PHOTOS_BASE_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define PHOTOS_TYPE_BASE_MANAGER (photos_base_manager_get_type ())
#define PHOTOS_BASE_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
   PHOTOS_TYPE_BASE_MANAGER, PhotosBaseManager))

#define PHOTOS_BASE_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
   PHOTOS_TYPE_BASE_MANAGER, PhotosBaseManagerClass))

#define PHOTOS_IS_BASE_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
   PHOTOS_TYPE_BASE_MANAGER))

#define PHOTOS_IS_BASE_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
   PHOTOS_TYPE_BASE_MANAGER))

#define PHOTOS_BASE_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
   PHOTOS_TYPE_BASE_MANAGER, PhotosBaseManagerClass))

typedef struct _PhotosBaseManager        PhotosBaseManager;
typedef struct _PhotosBaseManagerClass   PhotosBaseManagerClass;
typedef struct _PhotosBaseManagerPrivate PhotosBaseManagerPrivate;

struct _PhotosBaseManager
{
  GObject parent_instance;
  PhotosBaseManagerPrivate *priv;
};

struct _PhotosBaseManagerClass
{
  GObjectClass parent_class;

  void (*active_changed) (PhotosBaseManager *self);
  void (*item_added)     (PhotosBaseManager *self);
  void (*item_removed)   (PhotosBaseManager *self);
};

GType               photos_base_manager_get_type           (void) G_GNUC_CONST;

PhotosBaseManager  *photos_base_manager_new                (void);

G_END_DECLS

#endif /* PHOTOS_BASE_MANAGER_H */