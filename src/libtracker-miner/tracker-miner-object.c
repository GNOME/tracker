/*
 * Copyright (C) 2009, Nokia <ivan.frade@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include "config.h"

#include <math.h>

#include <glib/gi18n.h>

#include <libtracker-common/tracker-dbus.h>
#include <libtracker-common/tracker-type-utils.h>

#include "tracker-miner-object.h"

/* Here we use ceil() to eliminate decimal points beyond what we're
 * interested in, which is 2 decimal places for the progress. The
 * ceil() call will also round up the last decimal place.
 *
 * The 0.49 value is used for rounding correctness, because ceil()
 * rounds up if the number is > 0.0.
 */
#define PROGRESS_ROUNDED(x) ((x) < 0.01 ? 0.00 : (ceil (((x) * 100) - 0.49) / 100))

#ifdef MINER_STATUS_ENABLE_TRACE
#warning Miner status traces are enabled
#define trace(message, ...) g_debug (message, ##__VA_ARGS__)
#else
#define trace(...)
#endif /* MINER_STATUS_ENABLE_TRACE */

/**
 * SECTION:tracker-miner-object
 * @short_description: Abstract base class for data miners
 * @include: libtracker-miner/tracker-miner.h
 *
 * #TrackerMiner is an abstract base class to help developing data miners
 * for tracker-store, being an abstract class it doesn't do much by itself,
 * but provides the basic signaling and control over the actual indexing
 * task.
 *
 * #TrackerMiner implements the #GInitable interface, and thus, all objects of
 * types inheriting from #TrackerMiner must be initialized with g_initable_init()
 * just after creation (or directly created with g_initable_new()).
 **/

struct _TrackerMinerPrivate {
	TrackerSparqlConnection *connection;
	gboolean started;
	gint n_pauses;
	gchar *status;
	gdouble progress;
	gint remaining_time;
	gint availability_cookie;
	guint update_id;
};

enum {
	PROP_0,
	PROP_STATUS,
	PROP_PROGRESS,
	PROP_REMAINING_TIME,
	PROP_CONNECTION
};

enum {
	STARTED,
	STOPPED,
	PAUSED,
	RESUMED,
	PROGRESS,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void       miner_set_property           (GObject                *object,
                                                guint                   param_id,
                                                const GValue           *value,
                                                GParamSpec             *pspec);
static void       miner_get_property           (GObject                *object,
                                                guint                   param_id,
                                                GValue                 *value,
                                                GParamSpec             *pspec);
static void       miner_finalize               (GObject                *object);
static void       miner_initable_iface_init    (GInitableIface         *iface);
static gboolean   miner_initable_init          (GInitable              *initable,
                                                GCancellable           *cancellable,
                                                GError                **error);

/**
 * tracker_miner_error_quark:
 *
 * Gives the caller the #GQuark used to identify #TrackerMiner errors
 * in #GError structures. The #GQuark is used as the domain for the error.
 *
 * Returns: the #GQuark used for the domain of a #GError.
 *
 * Since: 0.8
 **/
G_DEFINE_QUARK (TrackerMinerError, tracker_miner_error)

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (TrackerMiner, tracker_miner, G_TYPE_OBJECT,
                                  G_ADD_PRIVATE (TrackerMiner)
                                  G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                         miner_initable_iface_init));

static void
tracker_miner_class_init (TrackerMinerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = miner_set_property;
	object_class->get_property = miner_get_property;
	object_class->finalize     = miner_finalize;

	/**
	 * TrackerMiner::started:
	 * @miner: the #TrackerMiner
	 *
	 * the ::started signal is emitted in the miner
	 * right after it has been started through
	 * tracker_miner_start().
	 *
	 * Since: 0.8
	 **/
	signals[STARTED] =
		g_signal_new ("started",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerClass, started),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 0);
	/**
	 * TrackerMiner::stopped:
	 * @miner: the #TrackerMiner
	 *
	 * the ::stopped signal is emitted in the miner
	 * right after it has been stopped through
	 * tracker_miner_stop().
	 *
	 * Since: 0.8
	 **/
	signals[STOPPED] =
		g_signal_new ("stopped",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerClass, stopped),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 0);
	/**
	 * TrackerMiner::paused:
	 * @miner: the #TrackerMiner
	 *
	 * the ::paused signal is emitted whenever
	 * there is any reason to pause, either
	 * internal (through tracker_miner_pause()) or
	 * external (through DBus, see #TrackerMinerManager).
	 *
	 * Since: 0.8
	 **/
	signals[PAUSED] =
		g_signal_new ("paused",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerClass, paused),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 0);
	/**
	 * TrackerMiner::resumed:
	 * @miner: the #TrackerMiner
	 *
	 * the ::resumed signal is emitted whenever
	 * all reasons to pause have disappeared, see
	 * tracker_miner_resume() and #TrackerMinerManager.
	 *
	 * Since: 0.8
	 **/
	signals[RESUMED] =
		g_signal_new ("resumed",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerClass, resumed),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 0);
	/**
	 * TrackerMiner::progress:
	 * @miner: the #TrackerMiner
	 * @status: miner status
	 * @progress: a #gdouble indicating miner progress, from 0 to 1.
	 * @remaining_time: a #gint indicating the reamaining processing time, in
	 * seconds.
	 *
	 * the ::progress signal will be emitted by TrackerMiner implementations
	 * to indicate progress about the data mining process. @status will
	 * contain a translated string with the current miner status and @progress
	 * will indicate how much has been processed so far. @remaining_time will
	 * give the number expected of seconds to finish processing, 0 if the
	 * value cannot be estimated, and -1 if its not applicable.
	 *
	 * Since: 0.12
	 **/
	signals[PROGRESS] =
		g_signal_new ("progress",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (TrackerMinerClass, progress),
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE, 3,
		              G_TYPE_STRING,
		              G_TYPE_DOUBLE,
		              G_TYPE_INT);

	g_object_class_install_property (object_class,
	                                 PROP_STATUS,
	                                 g_param_spec_string ("status",
	                                                      "Status",
	                                                      "Translatable string with status description",
	                                                      "Idle",
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	g_object_class_install_property (object_class,
	                                 PROP_PROGRESS,
	                                 g_param_spec_double ("progress",
	                                                      "Progress",
	                                                      "Miner progress",
	                                                      0.0,
	                                                      1.0,
	                                                      0.0,
	                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
	                                 PROP_REMAINING_TIME,
	                                 g_param_spec_int ("remaining-time",
	                                                   "Remaining time",
	                                                   "Estimated remaining time to finish processing",
	                                                   -1,
	                                                   G_MAXINT,
	                                                   -1,
	                                                   G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	/**
	 * TrackerMiner:connection:
	 *
	 * The SPARQL connection to use. For compatibility reasons, if not set
	 * at construct time, one shall be obtained through
	 * tracker_sparql_connection_get().
	 *
	 * Since: 2.0
	 **/
	g_object_class_install_property (object_class,
	                                 PROP_CONNECTION,
	                                 g_param_spec_object ("connection",
	                                                      "Connection",
	                                                      "SPARQL Connection",
	                                                      TRACKER_SPARQL_TYPE_CONNECTION,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
}

static void
miner_initable_iface_init (GInitableIface *iface)
{
	iface->init = miner_initable_init;
}

static gboolean
miner_initable_init (GInitable     *initable,
                     GCancellable  *cancellable,
                     GError       **error)
{
	TrackerMiner *miner = TRACKER_MINER (initable);
	GError *inner_error = NULL;

	if (!miner->priv->connection) {
		/* Try to get SPARQL connection... */
		miner->priv->connection = tracker_sparql_connection_get (NULL, &inner_error);
	}

	if (!miner->priv->connection) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	return TRUE;
}

static void
tracker_miner_init (TrackerMiner *miner)
{
	miner->priv = tracker_miner_get_instance_private (miner);
}

static gboolean
miner_update_progress_cb (gpointer data)
{
	TrackerMiner *miner = data;

	trace ("(Miner:'%s') UPDATE PROGRESS SIGNAL", G_OBJECT_TYPE_NAME (miner));

	g_signal_emit (miner, signals[PROGRESS], 0,
	               miner->priv->status,
	               miner->priv->progress,
	               miner->priv->remaining_time);

	miner->priv->update_id = 0;

	return FALSE;
}

static void
miner_set_property (GObject      *object,
                    guint         prop_id,
                    const GValue *value,
                    GParamSpec   *pspec)
{
	TrackerMiner *miner = TRACKER_MINER (object);

	/* Quite often, we see status of 100% and still have
	 * status messages saying Processing... which is not
	 * true. So we use an idle timeout to help that situation.
	 * Additionally we can't force both properties are correct
	 * with the GObject API, so we have to do some checks our
	 * selves. The g_object_bind_property() API also isn't
	 * sufficient here.
	 */

	switch (prop_id) {
	case PROP_STATUS: {
		const gchar *new_status;

		new_status = g_value_get_string (value);

		trace ("(Miner:'%s') Set property:'status' to '%s'",
		       G_OBJECT_TYPE_NAME (miner),
		       new_status);

		if (miner->priv->status && new_status &&
		    strcmp (miner->priv->status, new_status) == 0) {
			/* Same, do nothing */
			break;
		}

		g_free (miner->priv->status);
		miner->priv->status = g_strdup (new_status);

		/* Check progress matches special statuses */
		if (new_status != NULL) {
			if (g_ascii_strcasecmp (new_status, "Initializing") == 0 &&
			    miner->priv->progress != 0.0) {
				trace ("(Miner:'%s') Set progress to 0.0 from status:'Initializing'",
				       G_OBJECT_TYPE_NAME (miner));
				miner->priv->progress = 0.0;
			} else if (g_ascii_strcasecmp (new_status, "Idle") == 0 &&
			           miner->priv->progress != 1.0) {
				trace ("(Miner:'%s') Set progress to 1.0 from status:'Idle'",
				       G_OBJECT_TYPE_NAME (miner));
				miner->priv->progress = 1.0;
			}
		}

		if (miner->priv->update_id == 0) {
			miner->priv->update_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
			                                          miner_update_progress_cb,
			                                          miner,
			                                          NULL);
		}

		break;
	}
	case PROP_PROGRESS: {
		gdouble new_progress;

		new_progress = PROGRESS_ROUNDED (g_value_get_double (value));
		trace ("(Miner:'%s') Set property:'progress' to '%2.2f' (%2.2f before rounded)",
		         G_OBJECT_TYPE_NAME (miner),
		         new_progress,
		         g_value_get_double (value));

		/* NOTE: We don't round the current progress before
		 * comparison because we use the rounded value when
		 * we set it last.
		 *
		 * Only notify 1% changes
		 */
		if (new_progress == miner->priv->progress) {
			/* Same, do nothing */
			break;
		}

		miner->priv->progress = new_progress;

		/* Check status matches special progress values */
		if (new_progress == 0.0) {
			if (miner->priv->status == NULL ||
			    g_ascii_strcasecmp (miner->priv->status, "Initializing") != 0) {
				trace ("(Miner:'%s') Set status:'Initializing' from progress:0.0",
				       G_OBJECT_TYPE_NAME (miner));
				g_free (miner->priv->status);
				miner->priv->status = g_strdup ("Initializing");
			}
		} else if (new_progress == 1.0) {
			if (miner->priv->status == NULL ||
			    g_ascii_strcasecmp (miner->priv->status, "Idle") != 0) {
				trace ("(Miner:'%s') Set status:'Idle' from progress:1.0",
				       G_OBJECT_TYPE_NAME (miner));
				g_free (miner->priv->status);
				miner->priv->status = g_strdup ("Idle");
			}
		}

		if (miner->priv->update_id == 0) {
			miner->priv->update_id = g_idle_add_full (G_PRIORITY_HIGH_IDLE,
			                                          miner_update_progress_cb,
			                                          miner,
			                                          NULL);
		}

		break;
	}
	case PROP_REMAINING_TIME: {
		gint new_remaining_time;

		new_remaining_time = g_value_get_int (value);
		if (new_remaining_time != miner->priv->remaining_time) {
			/* Just set the new remaining time, don't notify it */
			miner->priv->remaining_time = new_remaining_time;
		}
		break;
	}
	case PROP_CONNECTION: {
		miner->priv->connection = g_value_dup_object (value);
		break;
	}
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
miner_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
	TrackerMiner *miner = TRACKER_MINER (object);

	switch (prop_id) {
	case PROP_STATUS:
		g_value_set_string (value, miner->priv->status);
		break;
	case PROP_PROGRESS:
		g_value_set_double (value, miner->priv->progress);
		break;
	case PROP_REMAINING_TIME:
		g_value_set_int (value, miner->priv->remaining_time);
		break;
	case PROP_CONNECTION:
		g_value_set_object (value, miner->priv->connection);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * tracker_miner_start:
 * @miner: a #TrackerMiner
 *
 * Tells the miner to start processing data.
 *
 * Since: 0.8
 **/
void
tracker_miner_start (TrackerMiner *miner)
{
	g_return_if_fail (TRACKER_IS_MINER (miner));

	if (miner->priv->started == FALSE) {
		miner->priv->started = TRUE;
		g_signal_emit (miner, signals[STARTED], 0);
	}
}

/**
 * tracker_miner_stop:
 * @miner: a #TrackerMiner
 *
 * Tells the miner to stop processing data.
 *
 * Since: 0.8
 **/
void
tracker_miner_stop (TrackerMiner *miner)
{
	g_return_if_fail (TRACKER_IS_MINER (miner));

	if (miner->priv->started == TRUE) {
		miner->priv->started = FALSE;
		g_signal_emit (miner, signals[STOPPED], 0);
	}
}

/**
 * tracker_miner_is_started:
 * @miner: a #TrackerMiner
 *
 * Returns #TRUE if the miner has been started.
 *
 * Returns: #TRUE if the miner is already started.
 *
 * Since: 0.8
 **/
gboolean
tracker_miner_is_started (TrackerMiner *miner)
{
	g_return_val_if_fail (TRACKER_IS_MINER (miner), TRUE);

	return miner->priv->started;
}

/**
 * tracker_miner_is_paused:
 * @miner: a #TrackerMiner
 *
 * Returns #TRUE if the miner is paused.
 *
 * Returns: #TRUE if the miner is paused.
 *
 * Since: 0.10
 **/
gboolean
tracker_miner_is_paused (TrackerMiner *miner)
{
	g_return_val_if_fail (TRACKER_IS_MINER (miner), TRUE);

	return miner->priv->n_pauses > 0;
}

/**
 * tracker_miner_pause:
 * @miner: a #TrackerMiner
 *
 * Asks @miner to pause. This call may be called multiple times,
 * but #TrackerMiner::paused will only be emitted the first time.
 * The same number of tracker_miner_resume() calls are expected
 * in order to resume operations.
 **/
void
tracker_miner_pause (TrackerMiner *miner)
{
	gint previous;

	g_return_if_fail (TRACKER_IS_MINER (miner));

	previous = g_atomic_int_add (&miner->priv->n_pauses, 1);

	if (previous == 0)
		g_signal_emit (miner, signals[PAUSED], 0);
}

/**
 * tracker_miner_resume:
 * @miner: a #TrackerMiner
 *
 * Asks the miner to resume processing. This needs to be called
 * as many times as tracker_miner_pause() calls were done
 * previously. This function will return #TRUE when the miner
 * is actually resumed.
 *
 * Returns: #TRUE if the miner resumed its operations.
 **/
gboolean
tracker_miner_resume (TrackerMiner *miner)
{
	g_return_val_if_fail (TRACKER_IS_MINER (miner), FALSE);
	g_return_val_if_fail (miner->priv->n_pauses > 0, FALSE);

	if (g_atomic_int_dec_and_test (&miner->priv->n_pauses)) {
		g_signal_emit (miner, signals[RESUMED], 0);
		return TRUE;
	}

	return FALSE;
}

/**
 * tracker_miner_get_connection:
 * @miner: a #TrackerMiner
 *
 * Gets the #TrackerSparqlConnection initialized by @miner
 *
 * Returns: (transfer none): a #TrackerSparqlConnection.
 *
 * Since: 0.10
 **/
TrackerSparqlConnection *
tracker_miner_get_connection (TrackerMiner *miner)
{
	return miner->priv->connection;
}

static void
miner_finalize (GObject *object)
{
	TrackerMiner *miner = TRACKER_MINER (object);

	if (miner->priv->update_id != 0) {
		g_source_remove (miner->priv->update_id);
	}

	g_free (miner->priv->status);

	if (miner->priv->connection) {
		g_object_unref (miner->priv->connection);
	}

	G_OBJECT_CLASS (tracker_miner_parent_class)->finalize (object);
}
