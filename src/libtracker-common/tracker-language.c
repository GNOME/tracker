/*
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2008, Nokia <ivan.frade@nokia.com>
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

#include <string.h>

#include <glib.h>

#ifdef HAVE_LIBSTEMMER
#include <libstemmer.h>
#endif /* HAVE_LIBSTEMMER */

#include "tracker-language.h"

typedef struct _TrackerLanguagePrivate TrackerLanguagePrivate;
typedef struct _Languages           Languages;

struct _TrackerLanguagePrivate {
	GHashTable    *stop_words;
	gboolean       enable_stemmer;
	gchar         *language_code;

	GMutex         stemmer_mutex;
	gpointer       stemmer;
};

struct _Languages {
	const gchar *code;
	const gchar *name;
};

static Languages all_langs[] = {
	{ "da", "Danish" },
	{ "nl", "Dutch" },
	{ "en", "English" },
	{ "fi", "Finnish" },
	{ "fr", "French" },
	{ "de", "German" },
	{ "hu", "Hungarian" },
	{ "it", "Italian" },
	{ "nb", "Norwegian" },
	{ "pt", "Portuguese" },
	{ "ru", "Russian" },
	{ "es", "Spanish" },
	{ "sv", "Swedish" },
	{ NULL, NULL },
};

/* GObject properties */
enum {
	PROP_0,

	PROP_ENABLE_STEMMER,
	PROP_STOP_WORDS,
	PROP_LANGUAGE_CODE,
};

static void         language_finalize          (GObject       *object);
static void         language_get_property      (GObject       *object,
                                                guint          param_id,
                                                GValue        *value,
                                                GParamSpec    *pspec);
static void         language_set_property      (GObject       *object,
                                                guint          param_id,
                                                const GValue  *value,
                                                GParamSpec    *pspec);

G_DEFINE_TYPE_WITH_PRIVATE (TrackerLanguage, tracker_language, G_TYPE_OBJECT);

static void
tracker_language_class_init (TrackerLanguageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize     = language_finalize;
	object_class->get_property = language_get_property;
	object_class->set_property = language_set_property;

	g_object_class_install_property (object_class,
	                                 PROP_ENABLE_STEMMER,
	                                 g_param_spec_boolean ("enable-stemmer",
	                                                       "Enable stemmer",
	                                                       "Enable stemmer",
	                                                       TRUE,
	                                                       G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));

	g_object_class_install_property (object_class,
	                                 PROP_STOP_WORDS,
	                                 g_param_spec_boxed ("stop-words",
	                                                     "Stop words",
	                                                     "Stop words",
	                                                     g_hash_table_get_type (),
	                                                     G_PARAM_READABLE));

	g_object_class_install_property (object_class,
	                                 PROP_LANGUAGE_CODE,
	                                 g_param_spec_string ("language-code",
	                                                      "Language code",
	                                                      "Language code",
	                                                      "en",
	                                                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT));
}

static void
tracker_language_init (TrackerLanguage *language)
{
	TrackerLanguagePrivate *priv;
#ifdef HAVE_LIBSTEMMER
	const gchar *stem_language;
#endif /* HAVE_LIBSTEMMER */

	priv = tracker_language_get_instance_private (language);

	priv->stop_words = g_hash_table_new_full (g_str_hash,
	                                          g_str_equal,
	                                          g_free,
	                                          NULL);
#ifdef HAVE_LIBSTEMMER
	g_mutex_init (&priv->stemmer_mutex);

	stem_language = tracker_language_get_name_by_code (NULL);
	priv->stemmer = sb_stemmer_new (stem_language, NULL);
#endif /* HAVE_LIBSTEMMER */
}

static void
language_finalize (GObject *object)
{
	TrackerLanguagePrivate *priv;

	priv = tracker_language_get_instance_private (TRACKER_LANGUAGE (object));

#ifdef HAVE_LIBSTEMMER
	if (priv->stemmer) {
		g_mutex_lock (&priv->stemmer_mutex);
		sb_stemmer_delete (priv->stemmer);
		g_mutex_unlock (&priv->stemmer_mutex);
	}
	g_mutex_clear (&priv->stemmer_mutex);
#endif /* HAVE_LIBSTEMMER */

	if (priv->stop_words) {
		g_hash_table_unref (priv->stop_words);
	}

	g_free (priv->language_code);

	(G_OBJECT_CLASS (tracker_language_parent_class)->finalize) (object);
}

static void
language_get_property (GObject    *object,
                       guint       param_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
	TrackerLanguagePrivate *priv;

	priv = tracker_language_get_instance_private (TRACKER_LANGUAGE (object));

	switch (param_id) {
	case PROP_ENABLE_STEMMER:
		g_value_set_boolean (value, priv->enable_stemmer);
		break;
	case PROP_STOP_WORDS:
		g_value_set_boxed (value, priv->stop_words);
		break;
	case PROP_LANGUAGE_CODE:
		g_value_set_string (value, priv->language_code);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
language_set_property (GObject      *object,
                       guint         param_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
	switch (param_id) {
	case PROP_ENABLE_STEMMER:
		tracker_language_set_enable_stemmer (TRACKER_LANGUAGE (object),
		                                     g_value_get_boolean (value));
		break;
	case PROP_LANGUAGE_CODE:
		tracker_language_set_language_code (TRACKER_LANGUAGE (object),
		                                    g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static gchar *
language_get_stopword_filename (const gchar *language_code)
{
	gchar *str;
	gchar *filename;
	const gchar *testpath;

	str = g_strconcat ("stopwords.", language_code, NULL);

	/* Look if the testpath for stopwords dictionary was set
	 *  (used during unit tests) */
	testpath = g_getenv ("TRACKER_LANGUAGE_STOP_WORDS_DIR");
	if (!testpath) {
		filename = g_build_filename (SHAREDIR,
		                             "tracker-3",
		                             "stop-words",
		                             str,
		                             NULL);
	} else {
		filename = g_build_filename (testpath,
		                             str,
		                             NULL);
	}

	g_free (str);
	return filename;
}

static void
language_add_stopwords (TrackerLanguage *language,
                        const gchar     *filename)
{
	TrackerLanguagePrivate *priv;
	GMappedFile          *mapped_file;
	GError               *error = NULL;
	gchar                *content;
	gchar               **words, **p;

	priv = tracker_language_get_instance_private (language);

	mapped_file = g_mapped_file_new (filename, FALSE, &error);
	if (error) {
		g_message ("Tracker couldn't read stopword file:'%s', %s",
		           filename, error->message);
		g_clear_error (&error);
		return;
	}

	content = g_mapped_file_get_contents (mapped_file);
	words = g_strsplit_set (content, "\n" , -1);

	g_mapped_file_unref (mapped_file);

	/* FIXME: Shouldn't clear the hash table first? */
	for (p = words; *p; p++) {
		g_hash_table_insert (priv->stop_words,
		                     g_strdup (g_strstrip (*p)),
		                     GINT_TO_POINTER (1));
	}

	g_strfreev (words);
}

static void
language_set_stopword_list (TrackerLanguage *language,
                            const gchar     *language_code)
{
#ifdef HAVE_LIBSTEMMER
	TrackerLanguagePrivate *priv;
	gchar *stem_language_lower;
	const gchar *stem_language;
#endif /* HAVE_LIBSTEMMER */

	gchar *stopword_filename;

	g_return_if_fail (TRACKER_IS_LANGUAGE (language));

	/* Set up stopwords list */
	/* g_message ("Setting up stopword list for language code:'%s'", language_code); */

	stopword_filename = language_get_stopword_filename (language_code);
	language_add_stopwords (language, stopword_filename);
	g_free (stopword_filename);

	if (!language_code || strcmp (language_code, "en") != 0) {
		stopword_filename = language_get_stopword_filename ("en");
		language_add_stopwords (language, stopword_filename);
		g_free (stopword_filename);
	}

#ifdef HAVE_LIBSTEMMER
	priv = tracker_language_get_instance_private (language);

	/* g_message ("Setting up stemmer for language code:'%s'", language_code); */

	stem_language = tracker_language_get_name_by_code (language_code);
	stem_language_lower = g_ascii_strdown (stem_language, -1);

	g_mutex_lock (&priv->stemmer_mutex);

	if (priv->stemmer) {
		sb_stemmer_delete (priv->stemmer);
	}

	priv->stemmer = sb_stemmer_new (stem_language_lower, NULL);
	if (!priv->stemmer) {
		g_message ("No stemmer could be found for language:'%s'",
		           stem_language_lower);
	}

	g_mutex_unlock (&priv->stemmer_mutex);

	g_free (stem_language_lower);
#endif /* HAVE_LIBSTEMMER */
}

/**
 * tracker_language_new:
 * @language_code: language code in ISO 639-1 format
 *
 * Creates a new #TrackerLanguage instance for the passed language code.
 *
 * Returns: a newly created #TrackerLanguage
 **/
TrackerLanguage *
tracker_language_new (const gchar *language_code)
{
	TrackerLanguage *language;

	language = g_object_new (TRACKER_TYPE_LANGUAGE,
	                         "language-code", language_code,
	                         NULL);

	return language;
}

/**
 * tracker_language_get_enable_stemmer:
 * @language: a #TrackerLanguage
 *
 * Returns whether words stemming is enabled for @language.
 *
 * Returns: %TRUE if word stemming is enabled.
 **/
gboolean
tracker_language_get_enable_stemmer (TrackerLanguage *language)
{
	TrackerLanguagePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), TRUE);

	priv = tracker_language_get_instance_private (language);

	return priv->enable_stemmer;
}

/**
 * tracker_language_get_stop_words:
 * @language: a #TrackerLanguage
 *
 * Returns the stop words for @language. Stop words are really common
 * words that are not worth to index for the language handled by @language.
 *
 * Returns: A #GHashTable with the stop words as the value, this memory
 *          is owned by @language and should not be modified nor freed.
 **/
GHashTable *
tracker_language_get_stop_words (TrackerLanguage *language)
{
	TrackerLanguagePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), NULL);

	priv = tracker_language_get_instance_private (language);

	return priv->stop_words;
}

/**
 * tracker_language_is_stop_word:
 * @language: a #TrackerLanguage
 * @word: a string containing a word
 *
 * Returns %TRUE if the given @word is in the list of stop words of the
 *  given @language.
 *
 * Returns: %TRUE if @word is a stop word. %FALSE otherwise.
 */
gboolean
tracker_language_is_stop_word (TrackerLanguage *language,
                               const gchar     *word)
{
	TrackerLanguagePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), FALSE);
	g_return_val_if_fail (word, FALSE);

	priv = tracker_language_get_instance_private (language);

	return g_hash_table_lookup (priv->stop_words, word) != NULL;
}

/**
 * tracker_language_get_language_code:
 * @language: a #TrackerLanguage
 *
 * Returns the language code in ISO 639-1 handled by @language.
 *
 * Returns: the language code.
 **/
const gchar *
tracker_language_get_language_code (TrackerLanguage *language)
{
	TrackerLanguagePrivate *priv;

	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), NULL);

	priv = tracker_language_get_instance_private (language);

	return priv->language_code;
}

/**
 * tracker_language_set_enable_stemmer:
 * @language: a #TrackerLanguage
 * @value: %TRUE to enable word stemming
 *
 * Enables or disables word stemming for @language.
 **/
void
tracker_language_set_enable_stemmer (TrackerLanguage *language,
                                     gboolean         value)
{
	TrackerLanguagePrivate *priv;

	g_return_if_fail (TRACKER_IS_LANGUAGE (language));

	priv = tracker_language_get_instance_private (language);

	priv->enable_stemmer = value;

	g_object_notify (G_OBJECT (language), "enable-stemmer");
}

/**
 * tracker_language_set_language_code:
 * @language: a #TrackerLanguage
 * @language_code: an ISO 639-1 language code
 *
 * Sets the @language to @language_code, a %NULL value will reset this
 * to "en" (English).
 **/
void
tracker_language_set_language_code (TrackerLanguage *language,
                                    const gchar     *language_code)
{
	TrackerLanguagePrivate *priv;

	g_return_if_fail (TRACKER_IS_LANGUAGE (language));

	priv = tracker_language_get_instance_private (language);

	g_free (priv->language_code);

	priv->language_code = g_strdup (language_code);

	if (!priv->language_code) {
		priv->language_code = g_strdup ("en");
	}

	language_set_stopword_list (language, priv->language_code);

	g_object_notify (G_OBJECT (language), "language-code");
}

/**
 * tracker_language_stem_word:
 * @language: a #TrackerLanguage
 * @word: string pointing to a word
 * @word_length: word ascii length
 *
 * If the stemmer is enabled, it will return the stem word for @word.
 * If it's disabled, it will return the passed word.
 *
 * Returns: a string with the processed word. This string must be
 *          freed with g_free()
 **/
gchar *
tracker_language_stem_word (TrackerLanguage *language,
                            const gchar     *word,
                            gint             word_length)
{
#ifdef HAVE_LIBSTEMMER
	TrackerLanguagePrivate *priv;
	const gchar *stem_word;
#endif /* HAVE_LIBSTEMMER */

	g_return_val_if_fail (TRACKER_IS_LANGUAGE (language), NULL);

	if (word_length < 0) {
		word_length = strlen (word);
	}

#ifdef HAVE_LIBSTEMMER
	priv = tracker_language_get_instance_private (language);

	if (!priv->enable_stemmer) {
		return g_strndup (word, word_length);
	}

	g_mutex_lock (&priv->stemmer_mutex);

	stem_word = (const gchar*) sb_stemmer_stem (priv->stemmer,
	                                            (guchar*) word,
	                                            word_length);

	g_mutex_unlock (&priv->stemmer_mutex);

	return g_strdup (stem_word);
#else  /* HAVE_LIBSTEMMER */
	return g_strndup (word, word_length);
#endif /* HAVE_LIBSTEMMER */
}

/**
 * tracker_language_get_name_by_code:
 * @language_code: a ISO 639-1 language code.
 *
 * Returns a human readable language name for the given
 * ISO 639-1 code, if supported by #TrackerLanguage
 *
 * Returns: the language name.
 **/
const gchar *
tracker_language_get_name_by_code (const gchar *language_code)
{
	gint i;

	if (!language_code || language_code[0] == '\0') {
		return "english";
	}

	for (i = 0; all_langs[i].code; i++) {
		if (g_str_has_prefix (language_code, all_langs[i].code)) {
			return all_langs[i].name;
		}
	}

	return "";
}
