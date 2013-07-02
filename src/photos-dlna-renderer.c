/*
 * Photos - access, organize and share your photos on GNOME
 * Copyright © 2013 Intel Corporation. All rights reserved.
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

#include "config.h"

#include "photos-dlna-renderer.h"


typedef enum
{
  INIT_NOTHING   = 0,
  INIT_DEVICE    = 1 << 0,
  INIT_PUSH_HOST = 1 << 1,
  INIT_PLAYER    = 1 << 2,
  INIT_READY     = INIT_DEVICE | INIT_PUSH_HOST | INIT_PLAYER
} InitStatus;


struct _PhotosDlnaRendererPrivate
{
  GBusType bus_type;
  GDBusProxyFlags flags;
  gchar *object_path;
  gchar *well_known_name;

  GTask *init_task;
  GError *init_error;

  DleynaRendererDevice *device;
  DleynaPushHost *push_host;
  MprisPlayer *player;

  InitStatus init_status;

  GHashTable *urls_to_item;
};


enum
{
  PROP_0,
  PROP_BUS_TYPE,
  PROP_WELL_KNOWN_NAME,
  PROP_FLAGS,
  PROP_OBJECT_PATH,
  PROP_SHARED_COUNT,
};


static void photos_dlna_renderer_async_initable_iface_init (GAsyncInitableIface *iface);


G_DEFINE_TYPE_WITH_CODE (PhotosDlnaRenderer, photos_dlna_renderer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, photos_dlna_renderer_async_initable_iface_init));


static void
photos_dlna_renderer_init_cleanup (PhotosDlnaRenderer *self)
{
  PhotosDlnaRendererPrivate *priv = self->priv;

  g_clear_error (&priv->init_error);
  g_clear_object (&priv->init_task);
}


static void
photos_dlna_renderer_dispose (GObject *object)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (object);
  PhotosDlnaRendererPrivate *priv = self->priv;

  g_clear_object (&priv->device);
  g_clear_object (&priv->push_host);
  g_clear_object (&priv->player);
  g_clear_pointer (&priv->urls_to_item, g_hash_table_unref);

  photos_dlna_renderer_init_cleanup (self);

  G_OBJECT_CLASS (photos_dlna_renderer_parent_class)->dispose (object);
}


static void
photos_dlna_renderer_finalize (GObject *object)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (object);
  PhotosDlnaRendererPrivate *priv = self->priv;

  g_free (priv->well_known_name);
  g_free (priv->object_path);

  G_OBJECT_CLASS (photos_dlna_renderer_parent_class)->finalize (object);
}


static void
photos_dlna_renderer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (object);

  switch (prop_id)
    {
    case PROP_SHARED_COUNT:
      g_value_set_uint (value, photos_dlna_renderer_get_shared_count (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
photos_dlna_renderer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (object);
  PhotosDlnaRendererPrivate *priv = self->priv;

  switch (prop_id)
    {
    case PROP_FLAGS:
      priv->flags = g_value_get_flags (value);
      break;

    case PROP_BUS_TYPE:
      priv->bus_type = g_value_get_enum (value);
      break;

    case PROP_WELL_KNOWN_NAME:
      priv->well_known_name = g_value_dup_string (value);
      break;

    case PROP_OBJECT_PATH:
      priv->object_path = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}


static void
photos_dlna_renderer_init (PhotosDlnaRenderer *self)
{
  PhotosDlnaRendererPrivate *priv;

  self->priv = priv = G_TYPE_INSTANCE_GET_PRIVATE (self, PHOTOS_TYPE_DLNA_RENDERER,
                                                   PhotosDlnaRendererPrivate);

  priv->urls_to_item = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}


static void
photos_dlna_renderer_class_init (PhotosDlnaRendererClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  gobject_class->dispose = photos_dlna_renderer_dispose;
  gobject_class->finalize = photos_dlna_renderer_finalize;
  gobject_class->get_property = photos_dlna_renderer_get_property;
  gobject_class->set_property = photos_dlna_renderer_set_property;

  g_object_class_install_property (gobject_class,
                                   PROP_FLAGS,
                                   g_param_spec_flags ("flags",
                                                       "Flags",
                                                       "Proxy flags",
                                                       G_TYPE_DBUS_PROXY_FLAGS,
                                                       G_DBUS_PROXY_FLAGS_NONE,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_WRITABLE |
                                                       G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_NAME |
                                                       G_PARAM_STATIC_BLURB |
                                                       G_PARAM_STATIC_NICK));

  g_object_class_install_property (gobject_class,
                                   PROP_BUS_TYPE,
                                   g_param_spec_enum ("bus-type",
                                                      "Bus Type",
                                                      "The bus to connect to, defaults to the session one",
                                                      G_TYPE_BUS_TYPE,
                                                      G_BUS_TYPE_SESSION,
                                                      G_PARAM_WRITABLE |
                                                      G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_STATIC_NICK));

  g_object_class_install_property (gobject_class,
                                   PROP_WELL_KNOWN_NAME,
                                   g_param_spec_string ("well-known-name",
                                                        "Well-Known Name",
                                                        "The well-known name of the service",
                                                        NULL,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  g_object_class_install_property (gobject_class,
                                   PROP_OBJECT_PATH,
                                   g_param_spec_string ("object-path",
                                                        "Object Path",
                                                        "The object path the proxy is for",
                                                        NULL,
                                                        G_PARAM_WRITABLE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_NAME |
                                                        G_PARAM_STATIC_BLURB |
                                                        G_PARAM_STATIC_NICK));

  g_object_class_install_property (gobject_class,
                                   PROP_SHARED_COUNT,
                                   g_param_spec_uint ("shared-count",
                                                      "Shared Count",
                                                      "The number of shared items",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_NAME |
                                                      G_PARAM_STATIC_BLURB |
                                                      G_PARAM_STATIC_NICK));

  g_type_class_add_private (class, sizeof (PhotosDlnaRendererPrivate));
}


void
photos_dlna_renderer_new_for_bus (GBusType             bus_type,
                                  GDBusProxyFlags      flags,
                                  const gchar         *well_known_name,
                                  const gchar         *object_path,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_async_initable_new_async (PHOTOS_TYPE_DLNA_RENDERER, G_PRIORITY_DEFAULT, cancellable,
                              callback, user_data, "flags", flags, "bus-type", bus_type,
                              "well-known-name", well_known_name,
                              "object-path", object_path, NULL);
}


/* photos_dlna_renderer_init_check_complete:
 *
 * Checks that all the async subtasks have completed and return the result.
 *
 * Note that if the caller does not take ownership of the object (eg. by not
 * calling g_task_propagate_pointer() or in case of errors) this may lead to
 * the object being disposed.
 */
static gboolean
photos_dlna_renderer_init_check_complete (PhotosDlnaRenderer *self,
                                          GError             *error)
{
  PhotosDlnaRendererPrivate *priv = self->priv;

  g_return_val_if_fail (g_task_is_valid (priv->init_task, self), TRUE);

  /* save the first error and then wait for all tasks to be completed
   * (succeeding or failing) before returning it */
  if (error != NULL && priv->init_error == NULL)
    priv->init_error = g_error_copy (error);

  if (priv->init_status != INIT_READY)
    return FALSE;

  /* make sure that `self` gets NULLified if g_object_unref() is
   * called inside g_task_return_pointer() to prevent calling
   * photos_dlna_renderer_init_cleanup() on an already freed object */
  g_object_add_weak_pointer (G_OBJECT (self), (gpointer *)&self);

  if (priv->init_error != NULL)
    {
      g_task_return_error (priv->init_task, priv->init_error);
      priv->init_error = NULL;
    }
  else
    g_task_return_pointer (priv->init_task, self, g_object_unref);

  if (self != NULL)
    photos_dlna_renderer_init_cleanup (self);

  g_object_remove_weak_pointer (G_OBJECT (self), (gpointer *)&self);

  return TRUE;
}


static void
photos_dlna_renderer_device_proxy_new_cb (GObject      *source_object,
                                          GAsyncResult *res,
                                          gpointer      user_data)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (user_data);
  PhotosDlnaRendererPrivate *priv = self->priv;
  GError *error = NULL;

  priv->init_status |= INIT_DEVICE;
  priv->device = dleyna_renderer_device_proxy_new_for_bus_finish (res, &error);

  if (error != NULL)
    g_warning ("Unable to load the RendererDevice interface: %s", error->message);

  photos_dlna_renderer_init_check_complete (self, error);

  g_clear_error (&error);
}


static void
photos_dlna_renderer_push_host_proxy_new_cb (GObject      *source_object,
                                             GAsyncResult *res,
                                             gpointer      user_data)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (user_data);
  PhotosDlnaRendererPrivate *priv = self->priv;
  GError *error = NULL;

  priv->init_status |= INIT_PUSH_HOST;
  priv->push_host = dleyna_push_host_proxy_new_for_bus_finish (res, &error);

  if (error != NULL)
    g_warning ("Unable to load the PushHost interface: %s", error->message);

  photos_dlna_renderer_init_check_complete (self, error);

  g_clear_error (&error);
}


static void
photos_dlna_renderer_player_proxy_new_cb (GObject      *source_object,
                                          GAsyncResult *res,
                                          gpointer      user_data)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (user_data);
  PhotosDlnaRendererPrivate *priv = self->priv;
  GError *error = NULL;

  priv->init_status |= INIT_PLAYER;
  priv->player = mpris_player_proxy_new_for_bus_finish (res, &error);

  if (error != NULL)
    g_warning ("Unable to load the Player interface: %s", error->message);

  photos_dlna_renderer_init_check_complete (self, error);

  g_clear_error (&error);
}


static void
photos_dlna_renderer_init_async (GAsyncInitable      *initable,
                                 int                  io_priority,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (initable);
  PhotosDlnaRendererPrivate *priv = self->priv;

  priv->init_task = g_task_new (initable, cancellable, callback, user_data);
  g_task_set_priority (priv->init_task, io_priority);

  /* Load all three DBus interface proxies asynchronously and then complete
   * the initialization when all of them are ready */
  dleyna_renderer_device_proxy_new_for_bus (priv->bus_type, priv->flags, priv->well_known_name,
                                            priv->object_path, cancellable,
                                            photos_dlna_renderer_device_proxy_new_cb,
                                            self);
  dleyna_push_host_proxy_new_for_bus (priv->bus_type, priv->flags, priv->well_known_name,
                                      priv->object_path, cancellable,
                                      photos_dlna_renderer_push_host_proxy_new_cb,
                                      self);
  mpris_player_proxy_new_for_bus (priv->bus_type, priv->flags, priv->well_known_name,
                                  priv->object_path, cancellable,
                                  photos_dlna_renderer_player_proxy_new_cb,
                                  self);
}


static gboolean
photos_dlna_renderer_init_finish (GAsyncInitable *initable,
                                  GAsyncResult   *result,
                                  GError        **error)
{
  g_return_val_if_fail (g_task_is_valid (result, G_OBJECT (initable)), FALSE);

  return g_task_propagate_pointer (G_TASK (result), error) != NULL;
}


PhotosDlnaRenderer*
photos_dlna_renderer_new_for_bus_finish (GAsyncResult *result,
                                         GError      **error)
{
  GObject *object, *source_object;
  GError *err = NULL;

  source_object = g_async_result_get_source_object (result);
  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, &err);
  g_object_unref (source_object);

  if (err != NULL)
    {
      g_clear_object (&object);
      g_propagate_error (error, err);
      return NULL;
    }

  return PHOTOS_DLNA_RENDERER (object);
}


static void
photos_dlna_renderer_async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = photos_dlna_renderer_init_async;
  iface->init_finish = photos_dlna_renderer_init_finish;
}


const gchar *
photos_dlna_renderer_get_object_path (PhotosDlnaRenderer *self)
{
  PhotosDlnaRendererPrivate *priv = self->priv;

  return priv->object_path;
}


DleynaRendererDevice *
photos_dlna_renderer_get_device (PhotosDlnaRenderer *self)
{
  PhotosDlnaRendererPrivate *priv = self->priv;

  return priv->device;
}


static void
photos_dlna_renderer_share_play_cb (GObject      *source_object,
                                    GAsyncResult *res,
                                    gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  PhotosDlnaRenderer *self;
  PhotosDlnaRendererPrivate *priv;
  PhotosBaseItem *item;
  GError *error = NULL;

  self = PHOTOS_DLNA_RENDERER (g_task_get_source_object (task));
  priv = self->priv;

  mpris_player_call_play_finish (MPRIS_PLAYER (source_object), res, &error);
  if (error != NULL)
    {
      g_warning ("Failed to call the Play method: %s", error->message);
      g_task_return_error (task, error);
      goto out;
    }

  item = g_object_get_data (G_OBJECT (task), "item");
  g_task_return_pointer (task, g_object_ref (item), g_object_unref);

out:
  g_object_unref (task);
}


static void
photos_dlna_renderer_share_open_uri_cb (GObject      *source_object,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  PhotosDlnaRenderer *self;
  PhotosDlnaRendererPrivate *priv;
  GError *error = NULL;

  self = PHOTOS_DLNA_RENDERER (g_task_get_source_object (task));
  priv = self->priv;

  mpris_player_call_open_uri_finish (MPRIS_PLAYER (source_object), res, &error);
  if (error != NULL)
    {
      g_warning ("Failed to call the OpenUri method: %s", error->message);
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  /* 3) Mpris.Player.Play() */
  mpris_player_call_play (priv->player,
                          g_task_get_cancellable (task),
                          photos_dlna_renderer_share_play_cb,
                          task);
}


static void
photos_dlna_renderer_share_host_file_cb (GObject      *source_object,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  PhotosDlnaRenderer *self;
  PhotosDlnaRendererPrivate *priv;
  PhotosBaseItem *item;
  gchar *hosted_url;
  GError *error = NULL;

  self = PHOTOS_DLNA_RENDERER (g_task_get_source_object (task));
  priv = self->priv;

  dleyna_push_host_call_host_file_finish (DLEYNA_PUSH_HOST (source_object), &hosted_url, res, &error);
  if (error != NULL)
    {
      g_warning ("Failed to call the HostFile method: %s", error->message);
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  item = g_object_get_data (G_OBJECT (task), "item");
  g_hash_table_replace (priv->urls_to_item, hosted_url, g_object_ref (item));
  g_object_notify (G_OBJECT (self), "shared-count");

  /* 2) Mpris.Player.OpenUri(hosted_url) */
  g_debug ("%s %s", __func__, hosted_url);
  mpris_player_call_open_uri (priv->player, hosted_url,
                              g_task_get_cancellable (task),
                              photos_dlna_renderer_share_open_uri_cb,
                              task);
}

void
photos_dlna_renderer_share (PhotosDlnaRenderer  *self,
                            PhotosBaseItem      *item,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  PhotosDlnaRendererPrivate *priv = self->priv;
  GTask *task;
  const gchar *uri;
  gchar *filename;
  GError *error = NULL;

  task = g_task_new (self, cancellable, callback, user_data);
  g_object_set_data_full (G_OBJECT (task), "item", g_object_ref (item), g_object_unref);

  uri = photos_base_item_get_uri (item);
  filename = g_filename_from_uri (uri, NULL, &error);
  if (error != NULL)
    {
      g_warning ("Unable to extract the local filename for the shared item: %s", error->message);
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  /* This will call a sequence of DBus methods to send the item to the DMR:
   * 1) DleynaRenderer.PushHost.HostFile (filename)
   *    → returns hosted_url, the HTTP URL published by the local webserver
   * 2) Mpris.Player.OpenUri(hosted_url)
   * 3) Mpris.Player.Play()
   *    → not really needed as OpenUri should automatically switch to Play to
   *      avoid races, but with dleyna-renderer v0.0.1-22-g6981acf it is still
   *      needed, see https://github.com/01org/dleyna-renderer/issues/78
   */

  /* 1) DleynaRenderer.PushHost.HostFile() */
  dleyna_push_host_call_host_file (priv->push_host, filename,
                                   cancellable,
                                   photos_dlna_renderer_share_host_file_cb,
                                   task);
  g_free (filename);
}


PhotosBaseItem *
photos_dlna_renderer_share_finish (PhotosDlnaRenderer *self,
                                   GAsyncResult       *res,
                                   GError            **error)
{
  g_return_val_if_fail (g_task_is_valid (res, G_OBJECT (self)), FALSE);

  return g_task_propagate_pointer (G_TASK (res), error);
}


static gboolean
photos_dlna_renderer_match_by_item_value (gpointer key,
                                          gpointer value,
                                          gpointer user_data)
{
  PhotosBaseItem *a = PHOTOS_BASE_ITEM (value);
  PhotosBaseItem *b = PHOTOS_BASE_ITEM (user_data);

  return g_strcmp0 (photos_base_item_get_id (a), photos_base_item_get_id (b)) == 0;
}


static void
photos_dlna_renderer_unshare_remove_file_cb (GObject      *source_object,
                                             GAsyncResult *res,
                                             gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (g_task_get_source_object (task));
  PhotosDlnaRendererPrivate *priv = self->priv;
  PhotosBaseItem *item;
  gboolean success = TRUE;
  GError *error = NULL;

  item = g_object_get_data (G_OBJECT (task), "item");
  g_hash_table_foreach_remove (priv->urls_to_item, photos_dlna_renderer_match_by_item_value, item);
  g_object_notify (G_OBJECT (self), "shared-count");

  dleyna_push_host_call_remove_file_finish (DLEYNA_PUSH_HOST (source_object), res, &error);
  if (error != NULL)
    {
      /* Assume that ignoring RemoveFile() errors is safe, since they are
       * likely caused by the file being already removed or the DBus service
       * having been restarted.  */
      g_warning ("Failed to call the RemoveFile method: %s", error->message);
      g_error_free (error);
      success = FALSE;
    }

  g_task_return_boolean (task, success);

out:
  g_object_unref (task);
}


void
photos_dlna_renderer_unshare (PhotosDlnaRenderer  *self,
                              PhotosBaseItem      *item,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
  PhotosDlnaRendererPrivate *priv = self->priv;
  GTask *task;
  const gchar *uri;
  gchar *filename;
  GError *error = NULL;

  task = g_task_new (self, cancellable, callback, user_data);
  g_object_set_data_full (G_OBJECT (task), "item", g_object_ref (item), g_object_unref);

  uri = photos_base_item_get_uri (item);
  filename = g_filename_from_uri (uri, NULL, &error);
  if (error != NULL)
    {
      g_warning ("Unable to extract the local filename for the shared item: %s", error->message);
      g_task_return_error (task, error);
      g_object_unref (task);
      return;
    }

  dleyna_push_host_call_remove_file (priv->push_host, filename, cancellable,
                                     photos_dlna_renderer_unshare_remove_file_cb, task);
  g_free (filename);
}


void
photos_dlna_renderer_unshare_finish (PhotosDlnaRenderer *self,
                                     GAsyncResult       *res,
                                     GError            **error)
{
  g_return_if_fail (g_task_is_valid (res, G_OBJECT (self)));

  g_task_propagate_boolean (G_TASK (res), error);
}


static void
photos_dlna_renderer_unshare_all_unshare_cb (GObject      *source_object,
                                             GAsyncResult *res,
                                             gpointer      user_data)
{
  PhotosDlnaRenderer *self = PHOTOS_DLNA_RENDERER (source_object);
  PhotosDlnaRendererPrivate *priv = self->priv;
  GTask *task = G_TASK (user_data);
  guint remaining;
  GError *error = NULL;

  /* decrement the remaining count */
  remaining = GPOINTER_TO_UINT (g_task_get_task_data (task));
  g_task_set_task_data (task, GUINT_TO_POINTER (--remaining), NULL);

  photos_dlna_renderer_unshare_finish (self, res, &error);

  if (error != NULL)
    g_warning ("Unable to unshare item: %s", error->message);

  if (remaining == 0)
    {
      g_task_return_boolean (task, TRUE);
      g_object_unref (task);
      return;
    }
}

void
photos_dlna_renderer_unshare_all (PhotosDlnaRenderer  *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  PhotosDlnaRendererPrivate *priv = self->priv;
  GTask *task;
  GList *items, *item;
  guint remaining;
  GError *error = NULL;

  task = g_task_new (self, cancellable, callback, user_data);

  items = g_hash_table_get_values (priv->urls_to_item);

  remaining = g_list_length (items);
  g_task_set_task_data (task, GUINT_TO_POINTER (remaining), NULL);

  for (item = items; item != NULL; item = g_list_next (item))
    photos_dlna_renderer_unshare (self, PHOTOS_BASE_ITEM (item->data), cancellable,
                                  photos_dlna_renderer_unshare_all_unshare_cb, task);

  g_list_free (items);
}


void
photos_dlna_renderer_unshare_all_finish (PhotosDlnaRenderer *self,
                                         GAsyncResult       *res,
                                         GError            **error)
{
  g_return_if_fail (g_task_is_valid (res, G_OBJECT (self)));

  g_task_propagate_boolean (G_TASK (res), error);
}


static void
photos_dlna_renderer_device_get_icon_cb (GObject      *source_object,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
  GTask *task = G_TASK (user_data);
  PhotosDlnaRenderer *self;
  GInputStream *icon_stream = NULL;
  GdkPixbuf *pixbuf = NULL;
  GVariant *icon_variant = NULL;
  GBytes *icon_bytes = NULL;
  const gchar *icon_data;
  gssize icon_data_size;
  gchar *mimetype = NULL;
  GError *error = NULL;

  self = PHOTOS_DLNA_RENDERER (g_task_get_source_object (task));

  /* The icon data is forced to be a GVariant since the GDBus bindings assume
   * bytestrings (type 'ay') to be nul-terminated and thus do not return the length
   * of the buffer */
  dleyna_renderer_device_call_get_icon_finish (DLEYNA_RENDERER_DEVICE (source_object),
                                               &icon_variant, &mimetype, res, &error);
  if (error != NULL)
    {
      g_warning ("Failed to call the GetIcon method: %s", error->message);
      g_task_return_error (task, error);
      goto out;
    }

  /* We know that the serialization of variant containing just a byte array
   * 'ay' is the byte  array itself */
  icon_bytes = g_variant_get_data_as_bytes (icon_variant);
  icon_data = g_bytes_get_data (icon_bytes, &icon_data_size);
  icon_stream = g_memory_input_stream_new_from_data (icon_data, icon_data_size, NULL);

  pixbuf = gdk_pixbuf_new_from_stream (icon_stream, g_task_get_cancellable (task), &error);
  if (error != NULL)
    {
      g_warning ("Failed to parse icon data: %s", error->message);
      g_task_return_error (task, error);
      goto out;
    }

  g_task_return_pointer (task, pixbuf, g_object_unref);

out:
  g_free (mimetype);
  g_clear_pointer (&icon_variant, g_variant_unref);
  g_clear_pointer (&icon_bytes, g_bytes_unref);
  g_clear_object (&icon_stream);
  g_object_unref (task);
}



const gchar *
photos_dlna_renderer_get_friendly_name (PhotosDlnaRenderer *self)
{
  PhotosDlnaRendererPrivate *priv = self->priv;

  return dleyna_renderer_device_get_friendly_name (priv->device);
}


void
photos_dlna_renderer_get_icon (PhotosDlnaRenderer  *self,
                               const gchar         *resolution,
                               const gchar         *requested_mimetype,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
  PhotosDlnaRendererPrivate *priv = self->priv;
  GTask *task;
  GError *error = NULL;

  task = g_task_new (self, cancellable, callback, user_data);

  dleyna_renderer_device_call_get_icon (priv->device, requested_mimetype, resolution,
                                        cancellable, photos_dlna_renderer_device_get_icon_cb,
                                        task);
}


GdkPixbuf *
photos_dlna_renderer_get_icon_finish (PhotosDlnaRenderer *self,
                                      GAsyncResult       *res,
                                      GError            **error)
{
  g_return_if_fail (g_task_is_valid (res, G_OBJECT (self)));

  return GDK_PIXBUF (g_task_propagate_pointer (G_TASK (res), error));
}


guint
photos_dlna_renderer_get_shared_count (PhotosDlnaRenderer *self)
{
  PhotosDlnaRendererPrivate *priv = self->priv;

  return g_hash_table_size (priv->urls_to_item);
}