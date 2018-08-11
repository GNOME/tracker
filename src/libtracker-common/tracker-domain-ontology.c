/*
 * Copyright (C) 2017, Red Hat, Inc.
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
 *
 * Authors: Carlos Garnacho <carlosg@gnome.org>
 */

#include "config.h"

#include <string.h>
#include "tracker-domain-ontology.h"

typedef struct {
	/* DomainOntologies section */
	GFile *cache_location;
	GFile *journal_location;
	GFile *ontology_location;
	gchar *name;
	gchar *domain;
	gchar *ontology_name;
	gchar **miners;
} TrackerDomainOntologyPrivate;

enum {
	PROP_0,
	PROP_NAME
};

struct {
	const gchar *var;
	const gchar *(*func) (void);
} lookup_dirs[] = {
	{ "HOME", g_get_home_dir },
	{ "XDG_CACHE_HOME", g_get_user_cache_dir },
	{ "XDG_DATA_HOME", g_get_user_data_dir },
	{ "XDG_RUNTIME_DIR", g_get_user_runtime_dir },
};

struct {
	const gchar *var;
	GUserDirectory user_directory;
} lookup_special_dirs[] = {
	{ "XDG_DESKTOP_DIR", G_USER_DIRECTORY_DESKTOP },
	{ "XDG_DOCUMENTS_DIR", G_USER_DIRECTORY_DOCUMENTS },
	{ "XDG_DOWNLOAD_DIR", G_USER_DIRECTORY_DOWNLOAD },
	{ "XDG_MUSIC_DIR", G_USER_DIRECTORY_MUSIC },
	{ "XDG_PICTURES_DIR", G_USER_DIRECTORY_PICTURES },
	{ "XDG_PUBLICSHARE_DIR", G_USER_DIRECTORY_PUBLIC_SHARE },
	{ "XDG_VIDEOS_DIR", G_USER_DIRECTORY_VIDEOS },
};

#define DOMAIN_ONTOLOGY_SECTION "DomainOntology"

#define CACHE_KEY "CacheLocation"
#define JOURNAL_KEY "JournalLocation"
#define ONTOLOGY_KEY "OntologyLocation"
#define ONTOLOGY_NAME_KEY "OntologyName"
#define DOMAIN_KEY "Domain"
#define MINERS_KEY "Miners"

#define DEFAULT_RULE "default.rule"

static void tracker_domain_ontology_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TrackerDomainOntology, tracker_domain_ontology, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (TrackerDomainOntology)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tracker_domain_ontology_initable_iface_init))

static void
tracker_domain_ontology_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
	TrackerDomainOntology *domain_ontology;
	TrackerDomainOntologyPrivate *priv;

	domain_ontology = TRACKER_DOMAIN_ONTOLOGY (object);
	priv = tracker_domain_ontology_get_instance_private (domain_ontology);

	switch (prop_id) {
	case PROP_NAME:
		priv->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_domain_ontology_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
	TrackerDomainOntology *domain_ontology;
	TrackerDomainOntologyPrivate *priv;

	domain_ontology = TRACKER_DOMAIN_ONTOLOGY (object);
	priv = tracker_domain_ontology_get_instance_private (domain_ontology);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
tracker_domain_ontology_finalize (GObject *object)
{
	TrackerDomainOntology *domain_ontology;
	TrackerDomainOntologyPrivate *priv;

	domain_ontology = TRACKER_DOMAIN_ONTOLOGY (object);
	priv = tracker_domain_ontology_get_instance_private (domain_ontology);

	g_clear_object (&priv->cache_location);
	g_clear_object (&priv->journal_location);
	g_clear_object (&priv->ontology_location);
	g_free (priv->name);
	g_free (priv->domain);
	g_strfreev (priv->miners);

	G_OBJECT_CLASS (tracker_domain_ontology_parent_class)->finalize (object);
}

static void
tracker_domain_ontology_class_init (TrackerDomainOntologyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = tracker_domain_ontology_set_property;
	object_class->get_property = tracker_domain_ontology_get_property;
	object_class->finalize = tracker_domain_ontology_finalize;

	g_object_class_install_property (object_class,
	                                 PROP_NAME,
	                                 g_param_spec_string ("name",
	                                                      "Name",
	                                                      "Name",
	                                                      NULL,
	                                                      G_PARAM_READWRITE |
	                                                      G_PARAM_CONSTRUCT_ONLY));
}

static void
tracker_domain_ontology_init (TrackerDomainOntology *domain_ontology)
{
}

static const gchar *
lookup_dir (const gchar *variable,
            gsize        variable_len)
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS (lookup_dirs); i++) {
		if (strncmp (lookup_dirs[i].var, variable, variable_len) == 0) {
			return lookup_dirs[i].func ();
		}
	}

	for (i = 0; i < G_N_ELEMENTS (lookup_special_dirs); i++) {
		if (strncmp (lookup_special_dirs[i].var, variable, variable_len) == 0) {
			return g_get_user_special_dir (lookup_special_dirs[i].user_directory);
		}
	}

	return NULL;
}

static GFile *
key_file_get_location (GKeyFile     *key_file,
                       const gchar  *section,
                       const gchar  *key,
                       gboolean      essential,
                       gboolean      must_exist,
                       GError      **error)
{
	GError *inner_error = NULL;
	gchar *value;
	GFile *file;

	value = g_key_file_get_string (key_file, section, key, &inner_error);
	if (inner_error) {
		if (essential)
			g_propagate_error (error, inner_error);
		else
			g_error_free (inner_error);

		return NULL;
	}

	if (value[0] == '$') {
		const gchar *var_end, *prefix;
		gchar *path;

		/* This is a path relative from a xdg dir */
		var_end = strchr (value, '/');
		if (!var_end) {
			/* We must take $VAR/subdir values */
			g_set_error (error,
			             G_KEY_FILE_ERROR,
			             G_KEY_FILE_ERROR_INVALID_VALUE,
			             "Path in key '%s' can not consist solely of a variable",
			             key);
			g_free (value);
			return NULL;
		}

		prefix = lookup_dir (&value[1], (var_end - &value[1]));
		if (!prefix) {
			g_set_error (error,
			             G_KEY_FILE_ERROR,
			             G_KEY_FILE_ERROR_INVALID_VALUE,
			             "Unrecognized variable in '%s'", key);
			g_free (value);
			return NULL;
		}

		path = g_strconcat (prefix, var_end, NULL);
		file = g_file_new_for_path (path);
		g_free (path);
	} else {
		file = g_file_new_for_uri (value);
	}

	g_free (value);

	if (must_exist && file &&
	    g_file_query_file_type (file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
	                            NULL) != G_FILE_TYPE_DIRECTORY) {
		gchar *uri = g_file_get_uri (file);
		g_set_error (error,
		             G_KEY_FILE_ERROR,
		             G_KEY_FILE_ERROR_INVALID_VALUE,
		             "Uri '%s' is not a directory or does not exist", uri);
		g_free (uri);
		return NULL;
	}

	return file;
}

static gchar *
find_rule_in_data_dirs (const gchar *name)
{
	const gchar* const *data_dirs;
	gchar *path, *rule_name;
	guint i;

	data_dirs = g_get_system_data_dirs ();
	rule_name = g_strconcat (name, ".rule", NULL);

	for (i = 0; data_dirs[i] != NULL; i++) {
		path = g_build_filename (data_dirs[i],
		                         "tracker", "domain-ontologies",
		                         rule_name, NULL);
		if (g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
			g_free (rule_name);
			return path;
		}

		g_free (path);
	}

	g_free (rule_name);

	return NULL;
}

static gboolean
tracker_domain_ontology_initable_init (GInitable     *initable,
                                       GCancellable  *cancellable,
                                       GError       **error)
{
	TrackerDomainOntology *domain_ontology;
	TrackerDomainOntologyPrivate *priv;
	GError *inner_error = NULL;
	GKeyFile *key_file = NULL;
	gchar *path, *path_for_tests;

	domain_ontology = TRACKER_DOMAIN_ONTOLOGY (initable);
	priv = tracker_domain_ontology_get_instance_private (domain_ontology);

	if (priv->name && priv->name[0] == '/') {
		if (!g_file_test (priv->name, G_FILE_TEST_IS_REGULAR)) {
			inner_error = g_error_new (G_KEY_FILE_ERROR,
			                           G_KEY_FILE_ERROR_NOT_FOUND,
			                           "Could not find rule at '%s'",
			                           priv->name);
			goto end;
		}

		path = g_strdup (priv->name);
	} else if (priv->name) {
		path = find_rule_in_data_dirs (priv->name);

		if (!path) {
			inner_error = g_error_new (G_KEY_FILE_ERROR,
			                           G_KEY_FILE_ERROR_NOT_FOUND,
			                           "Could not find rule '%s' in data dirs",
			                           priv->name);
			goto end;
		}
	} else {
		path = g_build_filename (SHAREDIR, "tracker", "domain-ontologies",
		                         DEFAULT_RULE, NULL);

		if (!g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
			/* This is only for uninstalled tests */
			path_for_tests = g_strdup (g_getenv ("TRACKER_TEST_DOMAIN_ONTOLOGY_RULE"));

			if (path_for_tests == NULL) {
				g_error ("Unable to find default domain ontology rule %s", path);
			}

			g_free (path);
			path = path_for_tests;
		}
	}

	key_file = g_key_file_new ();
	g_key_file_load_from_file (key_file, path, G_KEY_FILE_NONE, &inner_error);
	g_free (path);

	if (inner_error)
		goto end;

	priv->domain = g_key_file_get_string (key_file, DOMAIN_ONTOLOGY_SECTION,
	                                      DOMAIN_KEY, &inner_error);
	if (inner_error)
		goto end;

	priv->cache_location =
		key_file_get_location (key_file, DOMAIN_ONTOLOGY_SECTION,
		                       CACHE_KEY, TRUE, FALSE, &inner_error);
	if (inner_error)
		goto end;

	priv->journal_location =
		key_file_get_location (key_file, DOMAIN_ONTOLOGY_SECTION,
		                       JOURNAL_KEY, FALSE, FALSE, &inner_error);
	if (inner_error)
		goto end;

	priv->ontology_location =
		key_file_get_location (key_file, DOMAIN_ONTOLOGY_SECTION,
		                       ONTOLOGY_KEY, FALSE, TRUE, &inner_error);
	if (inner_error)
		goto end;

	priv->ontology_name = g_key_file_get_string (key_file, DOMAIN_ONTOLOGY_SECTION,
	                                             ONTOLOGY_NAME_KEY, NULL);
	priv->miners = g_key_file_get_string_list (key_file, DOMAIN_ONTOLOGY_SECTION,
	                                           MINERS_KEY, NULL, NULL);

	/* Consistency check, we need one of OntologyLocation and OntologyName,
	 * no more, no less.
	 */
	if ((priv->ontology_name && priv->ontology_location) ||
	    (!priv->ontology_name && !priv->ontology_location)) {
		inner_error = g_error_new (G_KEY_FILE_ERROR,
		                           G_KEY_FILE_ERROR_INVALID_VALUE,
		                           "One of OntologyLocation and OntologyName must be provided");
	}

	/* Build ontology location from name if necessary */
	if (!priv->ontology_location) {
		gchar *ontology_path;

		if (g_getenv ("TRACKER_DB_ONTOLOGIES_DIR") != NULL) {
			/* Override for use only by testcases */
			priv->ontology_location = g_file_new_for_path (g_getenv ("TRACKER_DB_ONTOLOGIES_DIR"));
		} else {
			ontology_path = g_build_filename (SHAREDIR, "tracker", "ontologies",
			                                  priv->ontology_name, NULL);

			if (!g_file_test (ontology_path, G_FILE_TEST_IS_DIR)) {
				g_error ("Unable to find ontologies in the configured location %s", ontology_path);
			}

			priv->ontology_location = g_file_new_for_path (ontology_path);

			g_free (ontology_path);
		}
	}

end:
	if (key_file)
		g_key_file_free (key_file);

	if (inner_error) {
		g_propagate_error (error, inner_error);
		return FALSE;
	}

	return TRUE;
}

static void
tracker_domain_ontology_initable_iface_init (GInitableIface *iface)
{
	iface->init = tracker_domain_ontology_initable_init;
}

TrackerDomainOntology *
tracker_domain_ontology_new (const gchar   *domain_name,
                             GCancellable  *cancellable,
                             GError       **error)
{
	return g_initable_new (TRACKER_TYPE_DOMAIN_ONTOLOGY,
	                       cancellable, error,
	                       "name", domain_name,
	                       NULL);
}

GFile *
tracker_domain_ontology_get_cache (TrackerDomainOntology *domain_ontology)
{
	TrackerDomainOntologyPrivate *priv;

	priv = tracker_domain_ontology_get_instance_private (domain_ontology);
	return priv->cache_location;
}

GFile *
tracker_domain_ontology_get_journal (TrackerDomainOntology *domain_ontology)
{
	TrackerDomainOntologyPrivate *priv;

	priv = tracker_domain_ontology_get_instance_private (domain_ontology);
	return priv->journal_location;
}

GFile *
tracker_domain_ontology_get_ontology (TrackerDomainOntology *domain_ontology)
{
	TrackerDomainOntologyPrivate *priv;

	priv = tracker_domain_ontology_get_instance_private (domain_ontology);
	return priv->ontology_location;
}

gchar *
tracker_domain_ontology_get_domain (TrackerDomainOntology *domain_ontology,
                                    const gchar           *suffix)
{
	TrackerDomainOntologyPrivate *priv;

	priv = tracker_domain_ontology_get_instance_private (domain_ontology);
	if (suffix)
		return g_strconcat (priv->domain, ".", suffix, NULL);
	else
		return g_strconcat (priv->domain, NULL);
}

gboolean
tracker_domain_ontology_uses_miner (TrackerDomainOntology *domain_ontology,
                                    const gchar           *suffix)
{
	TrackerDomainOntologyPrivate *priv;
	guint i;

	g_return_val_if_fail (suffix != NULL, FALSE);

	priv = tracker_domain_ontology_get_instance_private (domain_ontology);

	if (!priv->miners)
		return FALSE;

	for (i = 0; priv->miners[i] != NULL; i++) {
		if (strcmp (priv->miners[i], suffix) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}
