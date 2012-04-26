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

#ifndef PHOTOS_SELECTION_TOOLBAR_H
#define PHOTOS_SELECTION_TOOLBAR_H

#include <clutter/clutter.h>

G_BEGIN_DECLS

#define PHOTOS_TYPE_SELECTION_TOOLBAR (photos_selection_toolbar_get_type ())

#define PHOTOS_SELECTION_TOOLBAR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
   PHOTOS_TYPE_SELECTION_TOOLBAR, PhotosSelectionToolbar))

#define PHOTOS_SELECTION_TOOLBAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
   PHOTOS_TYPE_SELECTION_TOOLBAR, PhotosSelectionToolbarClass))

#define PHOTOS_IS_SELECTION_TOOLBAR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
   PHOTOS_TYPE_SELECTION_TOOLBAR))

#define PHOTOS_IS_SELECTION_TOOLBAR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
   PHOTOS_TYPE_SELECTION_TOOLBAR))

#define PHOTOS_SELECTION_TOOLBAR_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
   PHOTOS_TYPE_SELECTION_TOOLBAR, PhotosSelectionToolbarClass))

typedef struct _PhotosSelectionToolbar        PhotosSelectionToolbar;
typedef struct _PhotosSelectionToolbarClass   PhotosSelectionToolbarClass;
typedef struct _PhotosSelectionToolbarPrivate PhotosSelectionToolbarPrivate;

struct _PhotosSelectionToolbar
{
  GObject parent_instance;
  PhotosSelectionToolbarPrivate *priv;
};

struct _PhotosSelectionToolbarClass
{
  GObjectClass parent_class;
};

GType                     photos_selection_toolbar_get_type             (void) G_GNUC_CONST;

PhotosSelectionToolbar   *photos_selection_toolbar_new                  (void);

ClutterActor             *photos_selection_toolbar_get_actor            (PhotosSelectionToolbar *self);

G_END_DECLS

#endif /* PHOTOS_SELECTION_TOOLBAR_H */