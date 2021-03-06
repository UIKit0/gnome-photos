/* Eye Of Gnome - Jobs
 *
 * Copyright (C) 2006 The Free Software Foundation
 *
 * Author: Lucas Rocha <lucasr@gnome.org>
 *
 * Based on evince code (shell/ev-jobs.c) by:
 * 	- Martin Kretzschmar <martink@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "eog-uri-converter.h"
#include "eog-jobs.h"
#include "eog-job-queue.h"
#include "eog-image.h"
#include "eog-transform.h"
#include "photos-utils.h"

#include <gdk-pixbuf/gdk-pixbuf.h>

#define EOG_JOB_GET_PRIVATE(object) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((object), EOG_TYPE_JOB, EogJobPrivate))

G_DEFINE_TYPE (EogJob, eog_job, G_TYPE_OBJECT);
G_DEFINE_TYPE (EogJobLoad, eog_job_load, EOG_TYPE_JOB);
G_DEFINE_TYPE (EogJobTransform, eog_job_transform, EOG_TYPE_JOB);
G_DEFINE_TYPE (EogJobSave, eog_job_save, EOG_TYPE_JOB);
G_DEFINE_TYPE (EogJobSaveAs, eog_job_save_as, EOG_TYPE_JOB_SAVE);
G_DEFINE_TYPE (EogJobCopy, eog_job_copy, EOG_TYPE_JOB);

enum
{
	SIGNAL_FINISHED,
	SIGNAL_PROGRESS,
	SIGNAL_LAST_SIGNAL
};

static guint job_signals[SIGNAL_LAST_SIGNAL];

static void eog_job_copy_run      (EogJob *ejob);
static void eog_job_load_run 	  (EogJob *ejob);
static void eog_job_save_run      (EogJob *job);
static void eog_job_save_as_run   (EogJob *job);
static void eog_job_transform_run (EogJob *ejob);

static void eog_job_init (EogJob *job)
{
	/* NOTE: We need to allocate the mutex here so the ABI stays the same when it used to use g_mutex_new */
	job->mutex = g_malloc (sizeof (GMutex));
	g_mutex_init (job->mutex);
	job->progress = 0.0;
}

static void
eog_job_dispose (GObject *object)
{
	EogJob *job;

	job = EOG_JOB (object);

	if (job->error) {
		g_error_free (job->error);
		job->error = NULL;
	}

	if (job->mutex) {
		g_mutex_clear (job->mutex);
		g_free (job->mutex);
	}

	(* G_OBJECT_CLASS (eog_job_parent_class)->dispose) (object);
}

static void
eog_job_run_default (EogJob *job)
{
	g_critical ("Class \"%s\" does not implement the required run action",
		    G_OBJECT_CLASS_NAME (G_OBJECT_GET_CLASS (job)));
}

static void
eog_job_class_init (EogJobClass *class)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (class);

	oclass->dispose = eog_job_dispose;

	class->run = eog_job_run_default;

	job_signals [SIGNAL_FINISHED] =
		g_signal_new ("finished",
			      EOG_TYPE_JOB,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogJobClass, finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	job_signals [SIGNAL_PROGRESS] =
		g_signal_new ("progress",
			      EOG_TYPE_JOB,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogJobClass, progress),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__FLOAT,
			      G_TYPE_NONE, 1,
			      G_TYPE_FLOAT);
}

void
eog_job_finished (EogJob *job)
{
	g_return_if_fail (EOG_IS_JOB (job));

	g_signal_emit (job, job_signals[SIGNAL_FINISHED], 0);
}

/**
 * eog_job_run:
 * @job: the job to execute.
 *
 * Executes the job passed as @job. Usually there is no need to call this
 * on your own. Jobs should be executed by using the EogJobQueue.
 **/
void
eog_job_run (EogJob *job)
{
	EogJobClass *class;

	g_return_if_fail (EOG_IS_JOB (job));

	class = EOG_JOB_GET_CLASS (job);
	if (class->run)
		class->run (job);
	else
		eog_job_run_default (job);
}
static gboolean
notify_progress (gpointer data)
{
	EogJob *job = EOG_JOB (data);

	g_signal_emit (job, job_signals[SIGNAL_PROGRESS], 0, job->progress);

	return FALSE;
}

void
eog_job_set_progress (EogJob *job, float progress)
{
	g_return_if_fail (EOG_IS_JOB (job));
	g_return_if_fail (progress >= 0.0 && progress <= 1.0);

	g_mutex_lock (job->mutex);
	job->progress = progress;
	g_mutex_unlock (job->mutex);

	g_idle_add (notify_progress, job);
}

static void eog_job_load_init (EogJobLoad *job) { /* Do Nothing */ }

static void
eog_job_load_dispose (GObject *object)
{
	EogJobLoad *job;

	job = EOG_JOB_LOAD (object);

	if (job->image) {
		g_object_unref (job->image);
		job->image = NULL;
	}

	(* G_OBJECT_CLASS (eog_job_load_parent_class)->dispose) (object);
}

static void
eog_job_load_class_init (EogJobLoadClass *class)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (class);

	oclass->dispose = eog_job_load_dispose;
	EOG_JOB_CLASS (class)->run = eog_job_load_run;
}

EogJob *
eog_job_load_new (EogImage *image, EogImageData data)
{
	EogJobLoad *job;

	job = g_object_new (EOG_TYPE_JOB_LOAD, NULL);

	if (image) {
		job->image = g_object_ref (image);
	}
	job->data = data;

	return EOG_JOB (job);
}

static void
eog_job_load_run (EogJob *job)
{
	g_return_if_fail (EOG_IS_JOB_LOAD (job));

	if (job->error) {
	        g_error_free (job->error);
		job->error = NULL;
	}

	eog_image_load (EOG_IMAGE (EOG_JOB_LOAD (job)->image),
			EOG_JOB_LOAD (job)->data,
			job,
			&job->error);

	job->finished = TRUE;
}

static void eog_job_transform_init (EogJobTransform *job) { /* Do Nothing */ }

static void
eog_job_transform_dispose (GObject *object)
{
	EogJobTransform *job;

	job = EOG_JOB_TRANSFORM (object);

	if (job->trans) {
		g_object_unref (job->trans);
		job->trans = NULL;
	}

	g_list_foreach (job->images, (GFunc) g_object_unref, NULL);
	g_list_free (job->images);

	(* G_OBJECT_CLASS (eog_job_transform_parent_class)->dispose) (object);
}

static void
eog_job_transform_class_init (EogJobTransformClass *class)
{
	GObjectClass *oclass;

	oclass = G_OBJECT_CLASS (class);

	oclass->dispose = eog_job_transform_dispose;

	EOG_JOB_CLASS (class)->run = eog_job_transform_run;
}

EogJob *
eog_job_transform_new (GList *images, EogTransform *trans)
{
	EogJobTransform *job;

	job = g_object_new (EOG_TYPE_JOB_TRANSFORM, NULL);

	if (trans) {
		job->trans = g_object_ref (trans);
	} else {
		job->trans = NULL;
	}

	job->images = images;

	return EOG_JOB (job);
}

static gboolean
eog_job_transform_image_modified (gpointer data)
{
	g_return_val_if_fail (EOG_IS_IMAGE (data), FALSE);

	eog_image_modified (EOG_IMAGE (data));
	g_object_unref (G_OBJECT (data));

	return FALSE;
}

void
eog_job_transform_run (EogJob *ejob)
{
	EogJobTransform *job;
	GList *it;

	g_return_if_fail (EOG_IS_JOB_TRANSFORM (ejob));

	job = EOG_JOB_TRANSFORM (ejob);

	if (ejob->error) {
	        g_error_free (ejob->error);
		ejob->error = NULL;
	}

	for (it = job->images; it != NULL; it = it->next) {
		EogImage *image = EOG_IMAGE (it->data);

		if (job->trans == NULL) {
			eog_image_undo (image);
		} else {
			eog_image_transform (image, job->trans, ejob);
		}

		if (eog_image_is_modified (image) || job->trans == NULL) {
			g_object_ref (image);
			g_idle_add (eog_job_transform_image_modified, image);
		}
	}

	ejob->finished = TRUE;
}

static void eog_job_save_init (EogJobSave *job) { /* do nothing */ }

static void
eog_job_save_dispose (GObject *object)
{
	EogJobSave *job;

	job = EOG_JOB_SAVE (object);

	if (job->images) {
		g_list_foreach (job->images, (GFunc) g_object_unref, NULL);
		g_list_free (job->images);
		job->images = NULL;
	}

	(* G_OBJECT_CLASS (eog_job_save_parent_class)->dispose) (object);
}

static void
eog_job_save_class_init (EogJobSaveClass *class)
{
	G_OBJECT_CLASS (class)->dispose = eog_job_save_dispose;
	EOG_JOB_CLASS (class)->run = eog_job_save_run;
}

EogJob *
eog_job_save_new (GList *images)
{
	EogJobSave *job;

	job = g_object_new (EOG_TYPE_JOB_SAVE, NULL);

	job->images = images;
	job->current_image = NULL;

	return EOG_JOB (job);
}

static void
save_progress_handler (EogImage *image, gfloat progress, gpointer data)
{
	EogJobSave *job = EOG_JOB_SAVE (data);
	guint n_images = g_list_length (job->images);
	gfloat job_progress;

	job_progress = (job->current_pos / (gfloat) n_images) + (progress / n_images);

	eog_job_set_progress (EOG_JOB (job), job_progress);
}

static void
eog_job_save_run (EogJob *ejob)
{
	EogJobSave *job;
	GList *it;

	g_return_if_fail (EOG_IS_JOB_SAVE (ejob));

	job = EOG_JOB_SAVE (ejob);

	job->current_pos = 0;

	for (it = job->images; it != NULL; it = it->next, job->current_pos++) {
		EogImage *image = EOG_IMAGE (it->data);
		EogImageSaveInfo *save_info = NULL;
		gulong handler_id = 0;
		gboolean success = FALSE;

		job->current_image = image;

		/* Make sure the image doesn't go away while saving */
		eog_image_data_ref (image);

		if (!eog_image_has_data (image, EOG_IMAGE_DATA_ALL)) {
			EogImageMetadataStatus m_status;
			gint data2load = 0;

			m_status = eog_image_get_metadata_status (image);
			if (!eog_image_has_data (image, EOG_IMAGE_DATA_IMAGE)) {
				// Queue full read in this case
				data2load = EOG_IMAGE_DATA_ALL;
			} else if (m_status == EOG_IMAGE_METADATA_NOT_READ)
			{
				// Load only if we haven't read it yet
				data2load = EOG_IMAGE_DATA_EXIF
						| EOG_IMAGE_DATA_XMP;
			}

			if (data2load != 0) {
				eog_image_load (image,
						data2load,
						NULL,
						&ejob->error);
			}
		}

		handler_id = g_signal_connect (G_OBJECT (image),
					       "save-progress",
				               G_CALLBACK (save_progress_handler),
					       job);

		save_info = eog_image_save_info_new_from_image (image);

		success = eog_image_save_by_info (image,
						  save_info,
						  &ejob->error);

		if (save_info)
			g_object_unref (save_info);

		if (handler_id != 0)
			g_signal_handler_disconnect (G_OBJECT (image), handler_id);

		eog_image_data_unref (image);

		if (!success) break;
	}

	ejob->finished = TRUE;
}

static void eog_job_save_as_init (EogJobSaveAs *job) { /* do nothing */ }

static void eog_job_save_as_dispose (GObject *object)
{
	EogJobSaveAs *job = EOG_JOB_SAVE_AS (object);

	if (job->converter != NULL) {
		g_object_unref (job->converter);
		job->converter = NULL;
	}

	if (job->file != NULL) {
		g_object_unref (job->file);
		job->file = NULL;
	}

	(* G_OBJECT_CLASS (eog_job_save_as_parent_class)->dispose) (object);
}

static void
eog_job_save_as_class_init (EogJobSaveAsClass *class)
{
	G_OBJECT_CLASS (class)->dispose = eog_job_save_as_dispose;
	EOG_JOB_CLASS (class)->run = eog_job_save_as_run;
}

EogJob *
eog_job_save_as_new (GList *images, EogURIConverter *converter, GFile *file)
{
	EogJobSaveAs *job;

	g_assert (converter != NULL || g_list_length (images) == 1);

	job = g_object_new (EOG_TYPE_JOB_SAVE_AS, NULL);

	EOG_JOB_SAVE(job)->images = images;

	job->converter = converter ? g_object_ref (converter) : NULL;
	job->file = file ? g_object_ref (file) : NULL;

	return EOG_JOB (job);
}

static void
eog_job_save_as_run (EogJob *ejob)
{
	EogJobSave *job;
	EogJobSaveAs *saveas_job;
	GList *it;
	guint n_images;

	g_return_if_fail (EOG_IS_JOB_SAVE_AS (ejob));

	job = EOG_JOB_SAVE (ejob);

	n_images = g_list_length (job->images);

	saveas_job = EOG_JOB_SAVE_AS (job);

	job->current_pos = 0;

	for (it = job->images; it != NULL; it = it->next, job->current_pos++) {
		GdkPixbufFormat *format;
		EogImageSaveInfo *src_info, *dest_info;
		EogImage *image = EOG_IMAGE (it->data);
		gboolean success = FALSE;
		gulong handler_id = 0;

		job->current_image = image;

		eog_image_data_ref (image);

		if (!eog_image_has_data (image, EOG_IMAGE_DATA_ALL)) {
			EogImageMetadataStatus m_status;
			gint data2load = 0;

			m_status = eog_image_get_metadata_status (image);
			if (!eog_image_has_data (image, EOG_IMAGE_DATA_IMAGE)) {
				// Queue full read in this case
				data2load = EOG_IMAGE_DATA_ALL;
			} else if (m_status == EOG_IMAGE_METADATA_NOT_READ)
			{
				// Load only if we haven't read it yet
				data2load = EOG_IMAGE_DATA_EXIF
						| EOG_IMAGE_DATA_XMP;
			}

			if (data2load != 0) {
				eog_image_load (image,
						data2load,
						NULL,
						&ejob->error);
			}
		}


		g_assert (ejob->error == NULL);

		handler_id = g_signal_connect (G_OBJECT (image),
					       "save-progress",
				               G_CALLBACK (save_progress_handler),
					       job);

		src_info = eog_image_save_info_new_from_image (image);

		if (n_images == 1) {
			g_assert (saveas_job->file != NULL);

			format = photos_utils_get_pixbuf_format (saveas_job->file);

			dest_info = eog_image_save_info_new_from_file (saveas_job->file,
								       format);

		/* SaveAsDialog has already secured permission to overwrite */
			if (dest_info->exists) {
				dest_info->overwrite = TRUE;
			}
		} else {
			GFile *dest_file;
			gboolean result;

			result = eog_uri_converter_do (saveas_job->converter,
						       image,
						       &dest_file,
						       &format,
						       NULL);

			g_assert (result);

			dest_info = eog_image_save_info_new_from_file (dest_file,
								       format);
		}

		success = eog_image_save_as_by_info (image,
						     src_info,
						     dest_info,
						     &ejob->error);

		if (src_info)
			g_object_unref (src_info);

		if (dest_info)
			g_object_unref (dest_info);

		if (handler_id != 0)
			g_signal_handler_disconnect (G_OBJECT (image), handler_id);

		eog_image_data_unref (image);

		if (!success)
			break;
	}

	ejob->finished = TRUE;
}

static void eog_job_copy_init (EogJobCopy *job) { /* do nothing */};

static void
eog_job_copy_dispose (GObject *object)
{
	EogJobCopy *job = EOG_JOB_COPY (object);

	if (job->dest) {
		g_free (job->dest);
		job->dest = NULL;
	}

	(* G_OBJECT_CLASS (eog_job_copy_parent_class)->dispose) (object);
}

static void
eog_job_copy_class_init (EogJobCopyClass *class)
{
	G_OBJECT_CLASS (class)->dispose = eog_job_copy_dispose;
	EOG_JOB_CLASS (class)->run = eog_job_copy_run;
}

EogJob *
eog_job_copy_new (GList *images, const gchar *dest)
{
	EogJobCopy *job;

	g_assert (images != NULL && dest != NULL);

	job = g_object_new (EOG_TYPE_JOB_COPY, NULL);

	job->images = images;
	job->dest = g_strdup (dest);

	return EOG_JOB (job);
}

static void
eog_job_copy_progress_callback (goffset current_num_bytes,
				goffset total_num_bytes,
				gpointer user_data)
{
	gfloat job_progress;
	guint n_images;
	EogJobCopy *job;

	job = EOG_JOB_COPY (user_data);
	n_images = g_list_length (job->images);

	job_progress =  ((current_num_bytes / (gfloat) total_num_bytes) + job->current_pos)/n_images;

	eog_job_set_progress (EOG_JOB (job), job_progress);
}

void
eog_job_copy_run (EogJob *ejob)
{
	EogJobCopy *job;
	GList *it;
	GFile *src, *dest;
	gchar *filename, *dest_filename;

	g_return_if_fail (EOG_IS_JOB_COPY (ejob));

	job = EOG_JOB_COPY (ejob);
	job->current_pos = 0;

	for (it = job->images; it != NULL; it = g_list_next (it), job->current_pos++) {
		src = (GFile *) it->data;
		filename = g_file_get_basename (src);
		dest_filename = g_build_filename (job->dest, filename, NULL);
		dest = g_file_new_for_path (dest_filename);

		g_file_copy (src, dest,
			     G_FILE_COPY_OVERWRITE, NULL,
			     eog_job_copy_progress_callback, job,
			     &ejob->error);
		g_free (filename);
		g_free (dest_filename);
	}

	ejob->finished = TRUE;
}
