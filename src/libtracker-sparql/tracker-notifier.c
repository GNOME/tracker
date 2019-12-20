/*
 * Copyright (C) 2016-2018 Red Hat Inc.
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

/**
 * SECTION: tracker-notifier
 * @short_description: Listen to changes in the Tracker database
 * @include: libtracker-sparql/tracker-sparql.h
 *
 * #TrackerNotifier is an object that receives notifications about
 * changes to the Tracker database. A #TrackerNotifier is created
 * through tracker_notifier_new(), passing the RDF types that are
 * relevant to the caller, and possible different #TrackerNotifierFlags
 * to change #TrackerNotifier behavior. After the notifier is created,
 * events can be listened for by connecting to the #TrackerNotifier::events
 * signal. This object was added in Tracker 1.12.
 *
 * #TrackerNotifier is tracker:id centric, the ID can be
 * obtained from every event through tracker_notifier_event_get_id().
 * The expected way to retrieving metadata is a query of the form:
 * |[<!-- language="SPARQL" -->
 * SELECT ?urn …
 * WHERE {
 *   ?urn a rdfs:Resource .
 *   …
 *   FILTER (tracker:id(?urn) = …)
 * }
 * ]|
 *
 * If the %TRACKER_NOTIFIER_FLAG_QUERY_URN flag is passed, the extra
 * metadata will be available through tracker_notifier_event_get_urn().
 *
 * # Known caveats # {#trackernotifier-caveats}
 *
 * * The %TRACKER_NOTIFIER_EVENT_DELETE events will be received after the
 *   resource has been deleted. At that time queries on those elements will
 *   not bring any metadata. Only the ID/URN obtained through the event
 *   remain meaningful.
 * * Notifications of files being moved across indexed folders will
 *   appear as %TRACKER_NOTIFIER_EVENT_UPDATE events, containing
 *   the new location (if requested). The older location is no longer
 *   known to Tracker, this may make tracking of elements in specific
 *   folders hard using solely the #TrackerNotifier/Tracker data
 *   available at event notification time.
 *
 * The recommendation to fix those is making the caller aware
 * of tracker:ids, querying those in the application SPARQL
 * queries so the client can search the formerly queried data for
 * matching IDs when #TrackerNotifier events happen. URNs are just
 * as effective as a matching mechanism, but more costly.
 */

#include "config.h"

#include "tracker-notifier.h"
#include "tracker-sparql-enum-types.h"
#include "tracker-generated-no-checks.h"
#include <libtracker-common/tracker-common.h>

typedef struct _TrackerNotifierPrivate TrackerNotifierPrivate;
typedef struct _TrackerNotifierEventCache TrackerNotifierEventCache;

struct _TrackerNotifierPrivate {
	TrackerSparqlConnection *connection;
	GDBusConnection *dbus_connection;
	TrackerNotifierFlags flags;
	gchar **expanded_classes;
	gchar **classes;
	guint graph_updated_signal_id;
	guint has_arg0_filter : 1;
};

struct _TrackerNotifierEventCache {
	gchar *class;
	TrackerNotifier *notifier;
	GSequence *sequence;
};

struct _TrackerNotifierEvent {
	gint8 type;
	gint64 id;
	const gchar *rdf_type; /* Belongs to cache */
	gchar *urn;
	guint ref_count;
};

enum {
	PROP_0,
	PROP_CLASSES,
	PROP_FLAGS,
	N_PROPS
};

enum {
	EVENTS,
	N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

static void tracker_notifier_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TrackerNotifier, tracker_notifier, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (TrackerNotifier)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                tracker_notifier_initable_iface_init))

static TrackerNotifierEvent *
tracker_notifier_event_new (gint64       id,
                            const gchar *rdf_type)
{
	TrackerNotifierEvent *event;

	event = g_new0 (TrackerNotifierEvent, 1);
	event->type = -1;
	/* The type string belongs to the cache, and lives longer */
	event->rdf_type = rdf_type;
	event->id = id;
	event->ref_count = 1;
	return event;
}

static TrackerNotifierEvent *
tracker_notifier_event_ref (TrackerNotifierEvent *event)
{
	g_atomic_int_inc (&event->ref_count);
	return event;
}

static void
tracker_notifier_event_unref (TrackerNotifierEvent *event)
{
	if (g_atomic_int_dec_and_test (&event->ref_count)) {
		g_free (event->urn);
		g_free (event);
	}
}

static gint
compare_event_cb (gconstpointer a,
                  gconstpointer b,
                  gpointer      user_data)
{
	const TrackerNotifierEvent *event1 = a, *event2 = b;
	return event1->id - event2->id;
}

static TrackerNotifierEventCache *
tracker_notifier_event_cache_new (TrackerNotifier *notifier,
                                  const gchar     *rdf_class)
{
	TrackerNotifierEventCache *event_cache;

	event_cache = g_new0 (TrackerNotifierEventCache, 1);
	event_cache->notifier = g_object_ref (notifier);
	event_cache->class = g_strdup (rdf_class);
	event_cache->sequence = g_sequence_new ((GDestroyNotify) tracker_notifier_event_unref);

	return event_cache;
}

static void
tracker_notifier_event_cache_free (TrackerNotifierEventCache *event_cache)
{
	g_sequence_free (event_cache->sequence);
	g_object_unref (event_cache->notifier);
	g_free (event_cache->class);
	g_free (event_cache);
}

/* This is always meant to return a pointer */
static TrackerNotifierEvent *
tracker_notifier_event_cache_get_event (TrackerNotifierEventCache *cache,
                                        gint64                     id)
{
	TrackerNotifierEvent *event = NULL, search;
	GSequenceIter *iter, *prev;

	search.id = id;
	iter = g_sequence_search (cache->sequence, &search,
	                          compare_event_cb, NULL);

	if (!g_sequence_iter_is_begin (iter)) {
		prev = g_sequence_iter_prev (iter);
		event = g_sequence_get (prev);
		if (event->id == id)
			return event;
	} else if (!g_sequence_iter_is_end (iter)) {
		event = g_sequence_get (iter);
		if (event->id == id)
			return event;
	}

	event = tracker_notifier_event_new (id, cache->class);
	g_sequence_insert_before (iter, event);

	return event;
}

static void
tracker_notifier_event_cache_push_event (TrackerNotifierEventCache *cache,
                                         gint64                     id,
                                         TrackerNotifierEventType   event_type)
{
	TrackerNotifierEvent *event;

	event = tracker_notifier_event_cache_get_event (cache, id);

	if (event->type < 0 || event_type != TRACKER_NOTIFIER_EVENT_UPDATE)
		event->type = event_type;
}

static void
handle_events (TrackerNotifier           *notifier,
               TrackerNotifierEventCache *cache,
               GVariantIter              *iter)
{
	gint32 type, resource;

	while (g_variant_iter_loop (iter, "(ii)", &type, &resource))
		tracker_notifier_event_cache_push_event (cache, resource, type);
}

static GPtrArray *
tracker_notifier_event_cache_take_events (TrackerNotifierEventCache *cache)
{
	TrackerNotifierEvent *event;
	GSequenceIter *iter, *next;
	GPtrArray *events;

	events = g_ptr_array_new_with_free_func ((GDestroyNotify) tracker_notifier_event_unref);

	iter = g_sequence_get_begin_iter (cache->sequence);

	while (!g_sequence_iter_is_end (iter)) {
		next = g_sequence_iter_next (iter);
		event = g_sequence_get (iter);

		if (event->type == -1) {
			/* This event turned out a NO-OP, just remove it */
			g_sequence_remove (iter);
		} else {
			g_ptr_array_add (events, tracker_notifier_event_ref (event));
			g_sequence_remove (iter);
		}

		iter = next;
	}

	if (events->len == 0) {
		g_ptr_array_unref (events);
		return NULL;
	}

	return events;
}

static TrackerNotifierEvent *
find_event_in_array (GPtrArray *events,
                     gint64     id,
                     gint      *idx)
{
	TrackerNotifierEvent *event;

	while (*idx < events->len) {
		event = g_ptr_array_index (events, *idx);
		(*idx)++;
		if (event->id == id)
			return event;
	}

	return NULL;
}

static gchar *
create_extra_info_query (TrackerNotifier *notifier,
                         GPtrArray       *events)
{
	TrackerNotifierPrivate *priv;
	GString *sparql, *filter;
	gboolean has_elements = FALSE;
	gint idx;

	priv = tracker_notifier_get_instance_private (notifier);
	filter = g_string_new (NULL);

	for (idx = 0; idx < events->len; idx++) {
		TrackerNotifierEvent *event;

		event = g_ptr_array_index (events, idx);

		if (has_elements)
			g_string_append_c (filter, ' ');

		g_string_append_printf (filter, "%" G_GINT64_FORMAT, event->id);
		has_elements = TRUE;
	}

	if (!has_elements) {
		g_string_free (filter, TRUE);
		return NULL;
	}

	sparql = g_string_new ("SELECT ?id ");

	if (priv->flags & TRACKER_NOTIFIER_FLAG_QUERY_URN)
		g_string_append (sparql, "tracker:uri(xsd:integer(?id)) ");

	g_string_append_printf (sparql,
	                        "{ VALUES ?id { %s } } "
	                        "ORDER BY ?id", filter->str);
	g_string_free (filter, TRUE);

	return g_string_free (sparql, FALSE);
}

static void
tracker_notifier_query_extra_info (TrackerNotifier *notifier,
                                   GPtrArray       *events)
{
	TrackerNotifierPrivate *priv;
	TrackerSparqlCursor *cursor;
	TrackerNotifierEvent *event;
	gchar *sparql;
	gint idx = 0, col;
	gint64 id;

	sparql = create_extra_info_query (notifier, events);
	if (!sparql)
		return;

	priv = tracker_notifier_get_instance_private (notifier);
	cursor = tracker_sparql_connection_query (priv->connection, sparql,
	                                          NULL, NULL);
	g_free (sparql);

	/* We rely here in both the GPtrArray and the query items being
	 * sorted by tracker:id, the former will be so because the way it's
	 * extracted from the GSequence, the latter because of the ORDER BY
	 * clause.
	 */
	while (tracker_sparql_cursor_next (cursor, NULL, NULL)) {
		col = 0;
		id = tracker_sparql_cursor_get_integer (cursor, col++);
		event = find_event_in_array (events, id, &idx);

		if (!event) {
			g_critical ("Queried for id %" G_GINT64_FORMAT " but it is not "
			            "found, bailing out", id);
			break;
		}

		if (priv->flags & TRACKER_NOTIFIER_FLAG_QUERY_URN)
			event->urn = g_strdup (tracker_sparql_cursor_get_string (cursor, col++, NULL));
	}

	g_object_unref (cursor);
}

static void
tracker_notifier_event_cache_flush_events (TrackerNotifierEventCache *cache)
{
	TrackerNotifier *notifier = cache->notifier;
	TrackerNotifierPrivate *priv = tracker_notifier_get_instance_private (notifier);
	GPtrArray *events;

	events = tracker_notifier_event_cache_take_events (cache);

	if (events) {
		if (priv->flags & TRACKER_NOTIFIER_FLAG_QUERY_URN)
			tracker_notifier_query_extra_info (notifier, events);

		g_signal_emit (notifier, signals[EVENTS], 0, events);
		g_ptr_array_unref (events);
	}
}

static void
graph_updated_cb (GDBusConnection *connection,
                  const gchar     *sender_name,
                  const gchar     *object_path,
                  const gchar     *interface_name,
                  const gchar     *signal_name,
                  GVariant        *parameters,
                  gpointer         user_data)
{
	TrackerNotifier *notifier = user_data;
	TrackerNotifierEventCache *cache;
	GVariantIter *events;
	const gchar *graph;

	g_variant_get (parameters, "(&sa(ii))", &graph, &events);

	cache = tracker_notifier_event_cache_new (notifier, NULL);
	handle_events (notifier, cache, events);
	g_variant_iter_free (events);

	tracker_notifier_event_cache_flush_events (cache);
	tracker_notifier_event_cache_free (cache);
}

static gboolean
expand_class_iris (TrackerNotifier  *notifier,
                   GCancellable     *cancellable,
                   GError          **error)
{
	TrackerNotifierPrivate *priv;
	TrackerSparqlCursor *cursor;
	GArray *expanded;
	GString *sparql;
	gint i, n_classes;

	priv = tracker_notifier_get_instance_private (notifier);

	if (!priv->classes) {
		priv->expanded_classes = NULL;
		return TRUE;
	}

	n_classes = g_strv_length (priv->classes);

	sparql = g_string_new ("SELECT ");
	for (i = 0; i < n_classes; i++)
		g_string_append_printf (sparql, "%s ", priv->classes[i]);
	g_string_append_printf (sparql, "{}");

	cursor = tracker_sparql_connection_query (priv->connection, sparql->str,
	                                          cancellable, error);
	g_string_free (sparql, TRUE);

	if (!cursor)
		return FALSE;
	if (!tracker_sparql_cursor_next (cursor, cancellable, error))
		return FALSE;

	expanded = g_array_new (TRUE, TRUE, sizeof (gchar*));

	for (i = 0; i < tracker_sparql_cursor_get_n_columns (cursor); i++) {
		gchar *str = g_strdup (tracker_sparql_cursor_get_string (cursor, i, NULL));
		g_array_append_val (expanded, str);
	}

	priv->expanded_classes = (gchar **) g_array_free (expanded, FALSE);
	g_object_unref (cursor);

	return TRUE;
}

static gboolean
tracker_notifier_initable_init (GInitable     *initable,
                                GCancellable  *cancellable,
                                GError       **error)
{
	TrackerNotifier *notifier = TRACKER_NOTIFIER (initable);
	TrackerDomainOntology *domain_ontology;
	TrackerNotifierPrivate *priv;
	gchar *dbus_name;

	priv = tracker_notifier_get_instance_private (notifier);
	priv->connection = tracker_sparql_connection_get (cancellable, error);
	if (!priv->connection)
		return FALSE;

	if (!expand_class_iris (notifier, cancellable, error))
		return FALSE;

	priv->dbus_connection = tracker_sparql_connection_get_dbus_connection ();
	if (!priv->dbus_connection)
		priv->dbus_connection = g_bus_get_sync (G_BUS_TYPE_SESSION, cancellable, error);
	if (!priv->dbus_connection)
		return FALSE;

	domain_ontology = tracker_domain_ontology_new (tracker_sparql_connection_get_domain (),
	                                               cancellable, error);
	if (!domain_ontology)
		return FALSE;

	dbus_name = tracker_domain_ontology_get_domain (domain_ontology, "Tracker1");

	priv->graph_updated_signal_id =
		g_dbus_connection_signal_subscribe (priv->dbus_connection,
		                                    dbus_name,
		                                    "org.freedesktop.Tracker1.Endpoint",
		                                    "GraphUpdated",
		                                    "/org/freedesktop/Tracker1/Endpoint",
		                                    NULL,
		                                    G_DBUS_SIGNAL_FLAGS_NONE,
		                                    graph_updated_cb,
		                                    initable, NULL);
	tracker_domain_ontology_unref (domain_ontology);
	g_free (dbus_name);

	return TRUE;
}

static void
tracker_notifier_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_notifier_initable_init;
}

static void
tracker_notifier_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	TrackerNotifier *notifier = TRACKER_NOTIFIER (object);
	TrackerNotifierPrivate *priv = tracker_notifier_get_instance_private (notifier);

	switch (prop_id) {
	case PROP_CLASSES:
		priv->classes = g_value_dup_boxed (value);
		break;
	case PROP_FLAGS:
		priv->flags = g_value_get_flags (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_notifier_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	TrackerNotifier *notifier = TRACKER_NOTIFIER (object);
	TrackerNotifierPrivate *priv = tracker_notifier_get_instance_private (notifier);

	switch (prop_id) {
	case PROP_CLASSES:
		g_value_set_boxed (value, priv->classes);
		break;
	case PROP_FLAGS:
		g_value_set_flags (value, priv->flags);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_notifier_finalize (GObject *object)
{
	TrackerNotifierPrivate *priv;

	priv = tracker_notifier_get_instance_private (TRACKER_NOTIFIER (object));

	if (priv->dbus_connection) {
		g_dbus_connection_signal_unsubscribe (priv->dbus_connection,
		                                      priv->graph_updated_signal_id);

		g_object_unref (priv->dbus_connection);
	}

	if (priv->connection)
		g_object_unref (priv->connection);

	g_strfreev (priv->expanded_classes);
	g_strfreev (priv->classes);

	G_OBJECT_CLASS (tracker_notifier_parent_class)->finalize (object);
}

static void
tracker_notifier_class_init (TrackerNotifierClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspecs[N_PROPS] = { 0 };

	object_class->set_property = tracker_notifier_set_property;
	object_class->get_property = tracker_notifier_get_property;
	object_class->finalize = tracker_notifier_finalize;

	/**
	 * TrackerNotifier::events:
	 * @self: The #TrackerNotifier
	 * @events: (element-type TrackerNotifierEvent): A #GPtrArray of #TrackerNotifierEvent
	 *
	 * Notifies of changes in the Tracker database.
	 */
	signals[EVENTS] =
		g_signal_new ("events",
		              TRACKER_TYPE_NOTIFIER, 0,
		              G_STRUCT_OFFSET (TrackerNotifierClass, events),
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE, 1,
		              G_TYPE_PTR_ARRAY | G_SIGNAL_TYPE_STATIC_SCOPE);

	/**
	 * TrackerNotifier:classes:
	 *
	 * RDF classes to listen notifications about.
	 */
	pspecs[PROP_CLASSES] =
		g_param_spec_boxed ("classes",
		                    "Classes",
		                    "Classes",
		                    G_TYPE_STRV,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS |
		                    G_PARAM_CONSTRUCT_ONLY);
	/**
	 * TrackerNotifier:flags:
	 *
	 * Flags affecting #TrackerNotifier behavior.
	 */
	pspecs[PROP_FLAGS] =
		g_param_spec_flags ("flags",
		                    "Flags",
		                    "Flags",
		                    TRACKER_TYPE_NOTIFIER_FLAGS,
		                    TRACKER_NOTIFIER_FLAG_NONE,
		                    G_PARAM_READWRITE |
		                    G_PARAM_STATIC_STRINGS |
		                    G_PARAM_CONSTRUCT_ONLY);

	g_object_class_install_properties (object_class, N_PROPS, pspecs);
}

static void
tracker_notifier_init (TrackerNotifier *notifier)
{
}

/**
 * tracker_notifier_new:
 * @classes: (array zero-terminated=1) (allow-none): Array of RDF classes to
 *           receive notifications from, or %NULL for all.
 * @flags: flags affecting the notifier behavior
 * @cancellable: Cancellable for the operation
 * @error: location for the possible resulting error.
 *
 * Creates a new notifier, events can be listened through the
 * TrackerNotifier::events signal.
 *
 * Returns: (nullable): a newly created #TrackerNotifier, %NULL on error.
 *
 * Since: 1.12
 **/
TrackerNotifier*
tracker_notifier_new (const gchar * const   *classes,
                      TrackerNotifierFlags   flags,
                      GCancellable          *cancellable,
                      GError               **error)
{
	return g_initable_new (TRACKER_TYPE_NOTIFIER,
	                       cancellable, error,
	                       "classes", classes,
	                       "flags", flags,
	                       NULL);
}

/**
 * tracker_notifier_event_get_event_type:
 * @event: A #TrackerNotifierEvent
 *
 * Returns the event type.
 *
 * Returns: The event type
 *
 * Since: 1.12
 **/
TrackerNotifierEventType
tracker_notifier_event_get_event_type (TrackerNotifierEvent *event)
{
	g_return_val_if_fail (event != NULL, -1);
	return event->type;
}

/**
 * tracker_notifier_event_get_id:
 * @event: A #TrackerNotifierEvent
 *
 * Returns the tracker:id of the element being notified upon.
 *
 * Returns: the resource ID
 *
 * Since: 1.12
 **/
gint64
tracker_notifier_event_get_id (TrackerNotifierEvent *event)
{
	g_return_val_if_fail (event != NULL, 0);
	return event->id;
}

/**
 * tracker_notifier_event_get_type:
 * @event: A #TrackerNotifierEvent
 *
 * Returns the RDF type that this notification event relates to, in their
 * expanded forms (for example,
 * <http://www.semanticdesktop.org/ontologies/2007/03/22/nfo#Audio>).
 *
 * A resource may have multiple RDF types. In the case of changes to a
 * resource with multiple types, one event will be notified for each
 * RDF type the notifier is subscribed to.
 *
 * For performance reasons, Tracker only sends notifications for events that
 * are explicitly marked with the tracker:notify property in their ontology.
 *
 * Returns: the RDF type of the element
 *
 * Since: 1.12
 **/
const gchar *
tracker_notifier_event_get_type (TrackerNotifierEvent *event)
{
	g_return_val_if_fail (event != NULL, NULL);
	return event->rdf_type;
}

/**
 * tracker_notifier_event_get_urn:
 * @event: A #TrackerNotifierEvent
 *
 * Returns the Uniform Resource Name of the element if the
 * notifier has the flag %TRACKER_NOTIFIER_FLAG_QUERY_URN enabled.
 *
 * This URN is an unique string identifier for the resource being
 * notified upon, typically of the form "urn:uuid:...".
 *
 * Returns: The element URN
 *
 * Since: 1.12
 **/
const gchar *
tracker_notifier_event_get_urn (TrackerNotifierEvent *event)
{
	g_return_val_if_fail (event != NULL, NULL);
	return event->urn;
}
